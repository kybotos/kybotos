#!/usr/bin/env python3
"""scripts/v6_latency.py — 内蔵音源と MIDI OUT(外部音源)の発音のずれ(roadmap U-22 の V6)を測る(Phase 21e)。

usage: python3 scripts/v6_latency.py WAV [OFFSET_MS]
  WAV       = ステレオの録音。**L = 実機の内蔵スピーカー(マイク)、R = 外部音源(ライン)**
  OFFSET_MS = 外部音源の Note On を内蔵音より何 ms 後に予約したか(既定 250 = 120bpm で 480 tick)

測り方(docs/results/phase21e.md「U-22 の V6」): 同じ tick に置くと 1 本の録音で 2 つの音が重なって分けられないので、
**外部音源の Note On を内蔵音の既知の量だけ後ろに予約**し、R の立ち上がりごとに「L の直前の立ち上がりとの間隔 − OFFSET」を出す。
正なら MIDI OUT(外部音源)の方が遅い。両方とも同じスケジューラの tick 予約なので、差は経路(内蔵音源のブロック丸め /
DIN 送信 / 外部音源の応答)の差だけになる。**マイク側は空気中の伝搬(10cm で約 0.3ms)を含む。**

立ち上がりは 0.5ms 分解能の包絡線で、チャネルごとのピークの REL(既定 25%)を最初に超えた点。
- **R の不応期は 300ms**: ME-1 の Snare は約 100ms 後に 2 つ目のアタックがあり、それもしきい値を超える(Phase 21e の実測)。
- **L は内蔵の全サウンドを拾う**(D06 なら CHH が 8 分ごと)ので、R から OFFSET 戻った位置の ±125ms で**いちばん大きい**立ち上がり
  (= Snare + CHH)を相手にする。
"""
import struct
import sys
import wave

REL = 0.25
REFRACTORY = 0.100


def channels(path):
    w = wave.open(path, 'rb')
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    assert ch == 2 and w.getsampwidth() == 2
    s = struct.unpack('<%dh' % (n * 2), w.readframes(n))
    return [abs(v) for v in s[0::2]], [abs(v) for v in s[1::2]], rate


def onsets(x, rate, refractory=REFRACTORY):
    b = max(1, rate // 2000)  # 0.5ms
    env = [max(x[i:i + b]) for i in range(0, len(x) - b, b)]
    peak = max(env) or 1
    th = peak * REL
    out, last, below = [], -1e9, True
    for k, v in enumerate(env):
        t = k * b / rate
        if v >= th and below and t - last >= refractory:
            out.append(t)
            last, below = t, False
        elif v < th * 0.3:
            below = True
    return out, peak


def main():
    path = sys.argv[1]
    offset = float(sys.argv[2]) / 1000 if len(sys.argv) > 2 else 0.250
    left, right, rate = channels(path)
    lo, lp = onsets(left, rate)
    ro, rp = onsets(right, rate, refractory=0.300)
    loud = {t: max(left[int(t * rate):int((t + 0.03) * rate)]) for t in lo}
    diffs = []
    for r in ro:
        cands = [l for l in lo if abs((r - l) - offset) < 0.125]
        if not cands:
            continue
        l = max(cands, key=lambda t: loud[t])
        diffs.append((r, (r - l - offset) * 1000))
    print(f'L(internal) onsets={len(lo)} peak={lp}  R(external) onsets={len(ro)} peak={rp}  pairs={len(diffs)}')
    if not diffs:
        return
    d = sorted(v for _, v in diffs)
    n = len(d)
    mean = sum(d) / n
    sd = (sum((v - mean) ** 2 for v in d) / n) ** 0.5
    print(f'external - internal - {offset * 1000:.0f}ms: mean {mean:+.2f} ms  median {d[n // 2]:+.2f}  '
          f'min {d[0]:+.2f}  max {d[-1]:+.2f}  sd {sd:.2f}  (n={n})')
    hist = {}
    for v in d:
        k = int(v // 2) * 2
        hist[k] = hist.get(k, 0) + 1
    for k in sorted(hist):
        print(f'  {k:+4d}..{k + 2:+4d} ms {"#" * hist[k]}')


if __name__ == '__main__':
    main()
