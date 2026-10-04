#!/usr/bin/env bash
# scripts/linux-launcher-shots.sh — Linux ホストのランチャーの起動からメニューまでを撮る(Phase 22a)。
#
#   linux-launcher-shots.sh <タスク名>
#
# ホストをランチャーのモード(引数なし = ../../wasm-apps を並べる)で自分の子として起動し、
#   1. 起動 0.5 秒後: スプラッシュ          → captures/<タスク名>/splash.png
#   2. 起動 2.5 秒後: メニュー              → captures/<タスク名>/menu.png
#   3. メニューの 1 行目のアプリ(metronome)をタップ → 起動を待って撮る → captures/<タスク名>/app.png
#   4. ESC でアプリを止め、もう一度 ESC で終了
# 起動音(Phase 22a 追記)はミキサの出力を captures/<タスク名>/boot.wav に録り(KYBOTOS_WAV_OUT)、最後に
# 鳴っていた区間(ピークの 5% を超えた最初と最後)とピークを出す。
# ログは captures/<タスク名>/host.log。**手動の確認用**(docs/workflow.md §3.6)。メニュー画面には
# コマンドの入口(FIFO)が効かないので、タップは ui-linux.sh の xdotool の経路を使う(§1-8 の「手動の確認と撮影」)。
# 前提のレイアウト(shared/launcher_theme.h。Phase 22d): Settings はヘッダ右(x 220〜320、y 0〜28)、1 行目のアプリ y=38..62。
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
TASK=${1:?usage: linux-launcher-shots.sh <task>}
OUT=$REPO/captures/$TASK
mkdir -p "$OUT"
export DISPLAY=${DISPLAY:-:0}
unset KYBOTOS_CMD_FIFO

(cd "$REPO/hosts/linux" && KYBOTOS_WAV_OUT="$OUT/boot.wav" exec ./build/kybotos_host > "$OUT/host.log" 2>&1) &
host=$!

sleep 0.5
"$REPO/scripts/screen-still.sh" "$OUT" splash
sleep 2
"$REPO/scripts/screen-still.sh" "$OUT" menu

UI_CAPTURE_DIR=$OUT "$REPO/scripts/ui-linux.sh" tap 100 50
for _ in $(seq 1 30); do grep -q "app started" "$OUT/host.log" && break; sleep 0.1; done
sleep 0.5
"$REPO/scripts/screen-still.sh" "$OUT" app

"$REPO/scripts/ui-linux.sh" key Escape
sleep 0.5
"$REPO/scripts/ui-linux.sh" key Escape
for _ in $(seq 1 30); do kill -0 "$host" 2>/dev/null || break; sleep 0.1; done
kill "$host" 2>/dev/null
wait "$host" 2>/dev/null

cat "$OUT/host.log"
python3 - "$OUT/boot.wav" <<'PY'
import sys, wave, array
w = wave.open(sys.argv[1]); rate = w.getframerate()
a = array.array("h", w.readframes(w.getnframes()))[::w.getnchannels()]
peak = max((abs(v) for v in a), default=0)
on = [i for i, v in enumerate(a) if peak and abs(v) > peak * 0.05]
if on:
    print(f"boot sound: {on[0] / rate:.3f}s - {on[-1] / rate:.3f}s, peak {peak}")
else:
    print("boot sound: silent")
PY
grep -q "app started: .*metronome" "$OUT/host.log" && grep -q "app stopped" "$OUT/host.log"
