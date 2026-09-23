#!/usr/bin/env python3
"""scripts/wav_onsets.py — WAV のオンセットを検出し、打点から計算した期待時刻と突き合わせる(Phase 21d)。

usage: python3 scripts/wav_onsets.py WAV BPM "M1:steps1|M2:steps2|..."
  M = 拍子(例 3/4)、steps = その小節で鳴る 16 分ステップ(カンマ区切り、全サウンドの和集合)
  例: "4/4:0,4,8,12|3/4:0,4,8"
docs/workflow.md §3.8: 2ms ビンの包絡線 + 不応期つきのしきい値交差。
不応期は 115ms(ステップ間隔 125ms 未満。Kick の長い減衰が 90〜120ms 後にもう一度しきい値をまたぐため)
"""
import struct
import sys
import wave


def read(path):
    w = wave.open(path, 'rb')
    ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    assert sw == 2
    s = struct.unpack('<%dh' % (len(raw) // 2), raw)
    mono = [max(abs(s[i]), abs(s[i + 1])) if ch == 2 else abs(s[i]) for i in range(0, len(s), ch)]
    return mono, rate


def detect(mono, rate, bin_ms=2, rel=0.08, refractory_ms=115):
    b = int(rate * bin_ms / 1000)
    env = [max(mono[i:i + b]) for i in range(0, len(mono) - b, b)]
    peak = max(env) or 1
    th = peak * rel
    out, last = [], -1e9
    below = True
    for k, v in enumerate(env):
        t = k * bin_ms / 1000
        if v >= th and below and (t - last) * 1000 >= refractory_ms:
            out.append(t)
            last = t
            below = False
        elif v < th * 0.5:
            below = True
    return out, peak


def expected(spec, bpm, dur):
    step = 60.0 / bpm / 4
    bars = []
    for part in spec.split('|'):
        m, st = part.split(':')
        num, den = map(int, m.split('/'))
        steps = min(num * 16 // den, 24)
        bars.append((steps, [int(x) for x in st.split(',') if x != '']))
    out, t = [], 0.0
    period = sum(s for s, _ in bars) * step
    while t < dur:
        for steps, hits in bars:
            for h in hits:
                if h < steps and t + h * step < dur:
                    out.append(t + h * step)
            t += steps * step
    return out, period


def main():
    path, bpm, spec = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    mono, rate = read(path)
    got, peak = detect(mono, rate)
    if not got:
        print('no onsets'); return
    t0 = got[0]
    rel = [t - t0 for t in got]
    exp, period = expected(spec, bpm, rel[-1] + 0.01)
    # WAV の時計はわずかに進む(Linux ホストで約 0.2%)ので、**直前に一致した発音を基準に**追従する。
    # さらに**録音側でオーディオのバッファが 1 つ落ちる(約 23ms 詰まる)**ことがあるので、±15ms で見つからなければ
    # ±40ms まで広げて「跳び」として数える(跳びはアプリの打点の予約ではなく録音側の現象。docs/workflow.md §3.8)
    tol, wide = 0.015, 0.040
    off, used, errs, matched, jumps = 0.0, set(), [], 0, []
    for e in exp:
        for w in (tol, wide):
            best = None
            for k, g in enumerate(rel):
                if k not in used and abs(e + off - g) <= w and (best is None or abs(e + off - g) < abs(e + off - rel[best])):
                    best = k
            if best is not None:
                break
        if best is not None:
            d = rel[best] - (e + off)
            if abs(d) > tol:
                jumps.append((rel[best], d))
            else:
                errs.append(abs(d))
            off = rel[best] - e
            used.add(best)
            matched += 1
    extra = [g for k, g in enumerate(rel) if k not in used]
    print(f'peak={peak} onsets={len(got)} first={t0:.3f}s span={rel[-1]:.3f}s')
    print(f'period(expected)={period:.3f}s expected={len(exp)} matched={matched} missed={len(exp) - matched} extra={len(extra)}'
          + (f' max_err={max(errs) * 1000:.1f}ms' if errs else ''))
    if jumps:
        print('jumps (recording side):', ' '.join(f'{t:.3f}s({d * 1000:+.0f}ms)' for t, d in jumps))
    if extra:
        print('extra at:', ' '.join(f'{x:.3f}' for x in extra[:10]))


if __name__ == '__main__':
    main()
