#!/usr/bin/env python3
"""scripts/wav_summary.py — WAV を統計で要約し、複数の録音を並べて比べる(Phase 23)。

usage: python3 scripts/wav_summary.py [--dur SEC] WAV [WAV ...]
  各 WAV の**最初のオンセットから SEC 秒**(既定 8)を対象に、次を 1 行ずつ出す:
    peak(絶対値の最大)、rms、オンセットの数(wav_onsets.detect: 2ms ビン + 不応期)、
    帯域エネルギー(goertzel。60 / 110 / 190 / 1200 / 2000 / 6000 Hz、rms で正規化しない生の値の対数 dB)。
  左チャンネルだけを見る(Linux ホストの WAV は左右同じ)。

用途: 実際に鳴らした WAV は、同じコードでも録るたびにブロックの位相が変わってサンプル単位では一致しない
(docs/results/phase23.md 0-0)。そこで「同じ音が同じ数だけ、同じ大きさで鳴っているか」をこの要約で比べる
(docs/workflow.md §3.8 の「数値で判定できる」領域)。同じコードの 2 回の録音のばらつきを先に見て、それを物差しにする。
"""
import math
import struct
import sys
import wave

sys.path.insert(0, __import__('os').path.dirname(__file__))
from wav_onsets import detect  # noqa: E402

FREQS = (60, 110, 190, 1200, 2000, 6000)


def read_left(path):
    w = wave.open(path, 'rb')
    ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    assert sw == 2
    raw = w.readframes(n)
    s = struct.unpack('<%dh' % (len(raw) // 2), raw)
    return list(s[0::ch]), rate


def goertzel_db(x, rate, f):
    k = 2.0 * math.cos(2.0 * math.pi * f / rate)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + k * s1 - s2
        s2, s1 = s1, s0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return 10.0 * math.log10(p / len(x) + 1e-9)


def summarize(path, dur):
    x, rate = read_left(path)
    first = next((i for i, v in enumerate(x) if v), 0)
    seg = x[first:first + int(rate * dur)]
    onsets, _ = detect([abs(v) for v in seg], rate)
    peak = max(abs(v) for v in seg)
    rms = math.sqrt(sum(v * v for v in seg) / len(seg))
    bands = [goertzel_db(seg, rate, f) for f in FREQS]
    return first, len(seg) / rate, peak, rms, len(onsets), bands


def main():
    args = sys.argv[1:]
    dur = 8.0
    if args and args[0] == '--dur':
        dur = float(args[1])
        args = args[2:]
    print('%-40s %8s %6s %6s %8s %6s  %s' % ('wav', 'first', 'sec', 'peak', 'rms', 'onsets',
                                            '  '.join('%5dHz' % f for f in FREQS)))
    for p in args:
        first, sec, peak, rms, n, bands = summarize(p, dur)
        print('%-40s %8d %6.2f %6d %8.1f %6d  %s' % (p.split('/')[-1], first, sec, peak, rms, n,
                                                    '  '.join('%7.2f' % b for b in bands)))


if __name__ == '__main__':
    main()
