#!/usr/bin/env bash
# scripts/badge-shots-linux.sh — Linux ホストで電池のバッジ(右上)の見た目を撮る(Phase 25a。docs/workflow.md §3.6 の一部)。
#
#   badge-shots-linux.sh <タスク名>      → captures/<タスク名>/<画面>-<模擬値>.png
#
# KYBOTOS_SIM_BATT の模擬値(`<0..100>` = 電池、`usb<0..100>` = USB 給電の白抜き)を変えて、メニュー / metronome / mp3player を撮る。
# 前提: hosts/linux/build/kybotos_host がビルド済み、DISPLAY がある。座標・文字の前提: shared/battery_badge.h の右上。
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
TASK=${1:?usage: $0 <task-name>}
OUT=$REPO/captures/$TASK; mkdir -p "$OUT"
export DISPLAY=${DISPLAY:-:0}
shot() {  # <名前> <模擬値> [<wasm>]
  local name=$1 sim=$2 wasm=${3:-}
  local fifo=$OUT/cmd.fifo; rm -f "$fifo"; mkfifo "$fifo"
  ( cd "$REPO/hosts/linux" && KYBOTOS_CMD_FIFO=$fifo KYBOTOS_SIM_BATT=$sim exec ./build/kybotos_host $wasm > "$OUT/host-$name.log" 2>&1 ) &
  exec 3<>"$fifo"
  sleep 2.5
  KYBOTOS_CMD_FIFO=$fifo UI_CAPTURE_DIR=$OUT "$REPO/scripts/ui-linux.sh" shot "$name"
  echo texts >&3; echo stop >&3; sleep 0.8; exec 3>&-; wait 2>/dev/null; rm -f "$fifo"
}
shot menu-usb98    usb98
shot menu-64       64
shot menu-15       15
shot menu-usb15    usb15
shot menu-100      100
shot menu-none     ""
shot metronome-64      64      ../../wasm-apps/metronome/metronome.wasm
shot metronome-usb98   usb98   ../../wasm-apps/metronome/metronome.wasm
shot mp3player-usb100  usb100  ../../wasm-apps/mp3player/mp3player.wasm
shot mp3player-15      15      ../../wasm-apps/mp3player/mp3player.wasm
ls "$OUT"/*.png
