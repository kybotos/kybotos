#!/usr/bin/env bash
# scripts/ui-linux.sh — Linux ホスト(SDL ウィンドウ)の UI 操作と撮影(Phase 21d で整理)。
#
#   ui-linux.sh tap   X Y            合成クリック(1 回)
#   ui-linux.sh hold  X Y MS         押して MS ミリ秒待って離す(長押し)
#   ui-linux.sh drag  X Y DX DY MS   実ポインタで押す → MS 待つ → 8 段で (DX, DY) 動かす → 離す
#                                    (MS = 0 ならスワイプ / 編集状態のドラッグ、600 以上なら長押し + ドラッグ)
#   ui-linux.sh key   KEY            キー送信(Escape = 強制終了、BackSpace = 戻る)
#   ui-linux.sh shot  NAME           静止画を $UI_CAPTURE_DIR/NAME.png に撮る(既定 captures/ui)
#
# 座標は**アプリの論理座標(320x240)**。ウィンドウは kybotos_host の pid と一致するものを選ぶ
# (docs/workflow.md §3.6)。合成クリックは要求したウィンドウ座標がそのまま届く(×2 = WINDOW_SCALE)。
# **ドラッグは実ポインタを動かすので絶対座標**で、`mousemove --window 0 0` → `getmouselocation` で
# クライアント原点を較正してから動かす(docs/lessons.md Phase 21b)。
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${UI_CAPTURE_DIR:-$REPO/captures/ui}
export DISPLAY=${DISPLAY:-:0}
pid=$(pgrep -x kybotos_host | head -1)
[ -z "$pid" ] && { echo "ui-linux: kybotos_host is not running" >&2; exit 1; }
WIN=""
for w in $(xdotool search --name "Kybotos host"); do
  [ "$(xdotool getwindowpid "$w" 2>/dev/null)" = "$pid" ] && WIN=$w
done
[ -z "$WIN" ] && { echo "ui-linux: window not found" >&2; exit 1; }
msleep() { sleep "$(printf '%d.%03d' $(($1 / 1000)) $(($1 % 1000)))"; }
case "${1:-}" in
  tap)  xdotool mousemove --window "$WIN" $(($2 * 2)) $(($3 * 2)); sleep 0.15
        xdotool click --window "$WIN" 1; sleep 0.4 ;;
  hold) xdotool mousemove --window "$WIN" $(($2 * 2)) $(($3 * 2)); sleep 0.15
        xdotool mousedown --window "$WIN" 1; msleep "$4"
        xdotool mouseup --window "$WIN" 1; sleep 0.4 ;;
  drag) xdotool mousemove --window "$WIN" 0 0; sleep 0.1
        eval "$(xdotool getmouselocation --shell)"; OX=$X; OY=$Y
        sx=$((OX + $2 * 2)); sy=$((OY + $3 * 2))
        xdotool mousemove "$sx" "$sy"; sleep 0.15; xdotool mousedown 1; msleep "$6"
        for k in 1 2 3 4 5 6 7 8; do
          xdotool mousemove $((sx + $4 * 2 * k / 8)) $((sy + $5 * 2 * k / 8)); sleep 0.12
        done
        sleep 0.3; xdotool mouseup 1; sleep 0.4 ;;
  key)  xdotool key --window "$WIN" "$2"; sleep 0.4 ;;
  shot) mkdir -p "$OUT"; "$REPO/scripts/screen-still.sh" "$OUT" "$2" >/dev/null 2>&1; echo "$OUT/$2.png" ;;
  *)    sed -n '2,16p' "$0"; exit 2 ;;
esac
