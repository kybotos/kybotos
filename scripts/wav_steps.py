#!/usr/bin/env python3
"""scripts/wav_steps.py — 途切れない音(全ステップ ON など)の検証(Phase 21d)。

16 分ごとに「直前の谷 / 直後の山」の比を追い、立ち上がりの無いステップ(欠け)を探す。
しきい値交差(wav_onsets.py)は音が途切れないと発音を区切れないため、こちらを使う。
usage: python3 scripts/wav_steps.py WAV BPM STEPS_PER_BAR"""
import sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from wav_onsets import read
path, bpm, spb = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
m, r = read(path)
b = int(r * 0.002)
env = [max(m[i:i + b]) for i in range(0, len(m) - b, b)]
dt = 0.002
step = 60.0 / bpm / 4
t = next(k for k, v in enumerate(env) if v > 1000) * dt
rises = []
while t / dt + 20 < len(env):
    k0 = int((t - 0.02) / dt)
    best, bk = 0, None
    for k in range(k0, k0 + 20):
        pre = max(min(env[k - 5:k]), 50)  # 無音の直後でも比が発散しないように
        post = max(env[k:k + 8])
        if post / pre > best:
            best, bk = post / pre, k
    if best < 1.3:  # 立ち上がりが無い = 再生の終わり(か欠け)
        break
    rises.append(best)
    t = bk * dt + step
n = len(rises)
print(f'steps={n} bars={n / spb:.2f} min_rise={min(rises):.2f} max_rise={max(rises):.2f}')
by = [[rises[i] for i in range(k, n, spb)] for k in range(spb)]
print('mean rise by step:', ' '.join(f'{sum(x) / len(x):.1f}' for x in by if x))
