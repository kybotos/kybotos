#!/usr/bin/env python3
"""scripts/metronome_clicks.py — metronome の音を録った WAV から、クリックの時刻・間隔・種類を出す(Phase 22b)。

    metronome_clicks.py <wav> [秒数の上限]

Linux ホストのミキサの出力(KYBOTOS_WAV_OUT。docs/workflow.md §3.8)を読み、包絡線がピークの 10% を
超えた所をクリックの立ち上がりとして拾う。**音の長さで小節頭(Metronome Bell、note 34、150ms)と
他の拍(Metronome Click、note 33、60ms)を見分ける**(長さが 100ms 以上なら小節頭 = `B`、未満なら `c`)。

出力は 1 行 1 クリック(時刻、前のクリックからの間隔 ms、その間隔から求めた BPM 相当、種類)。最後に
種類の並び(`Bccc Bccc ...`)を出すので、小節をやり直したか(拍の途中でベルが来る)が一目で分かる。
注意: BPM 相当は 4 分音符を 1 拍としたときの値(x/8 では 2 倍に出る)。
"""
import array
import sys
import wave


def main():
    path = sys.argv[1]
    limit = float(sys.argv[2]) if len(sys.argv) > 2 else None
    w = wave.open(path)
    rate, ch = w.getframerate(), w.getnchannels()
    n = w.getnframes()
    if limit is not None:
        n = min(n, int(limit * rate))
    a = array.array("h", w.readframes(n))[::ch]
    peak = max((abs(v) for v in a), default=0)
    if peak == 0:
        print("silent")
        return
    on = peak * 0.10
    # 包絡線: 1ms の窓の最大値
    win = max(1, rate // 1000)
    env = [max(abs(v) for v in a[i:i + win]) for i in range(0, len(a), win)]
    clicks = []  # (開始 ms, 長さ ms)
    i = 0
    while i < len(env):
        if env[i] > on:
            j = i
            quiet = 0
            # 20ms 続けて閾値を下回ったら終わり
            while j < len(env) and quiet < 20:
                quiet = quiet + 1 if env[j] <= on * 0.3 else 0
                j += 1
            clicks.append((i, j - 20 - i))
            i = j
        else:
            i += 1
    prev = None
    kinds = []
    for t, d in clicks:
        kind = "B" if d >= 100 else "c"
        kinds.append(kind)
        if prev is None:
            print(f"{t / 1000:8.3f}s  {'':>8}  {'':>7}  {kind} ({d}ms)")
        else:
            iv = t - prev
            print(f"{t / 1000:8.3f}s  {iv:6d}ms  {60000 / iv:7.1f}  {kind} ({d}ms)")
        prev = t
    print("pattern:", " ".join("".join(kinds).replace("B", " B").split()))


if __name__ == "__main__":
    main()
