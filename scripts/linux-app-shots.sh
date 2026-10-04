#!/usr/bin/env bash
# scripts/linux-app-shots.sh — Linux ホストでアプリを動かし、操作しながら画面を撮る(Phase 22b の metronome-shots.sh を
# Phase 22c で任意のアプリ向けにしたもの。metronome-shots.sh はこれを呼ぶ)。
#
#   linux-app-shots.sh <app.wasm> <タスク名> <手順>...
#
# 手順は 1 つずつ引数で渡す(docs/workflow.md §3.6 の「UI を操作しながら撮る場合」をまとめたもの):
#   "shot <名前>"        静止画 → captures/<タスク名>/<名前>.png
#   "tap X Y" / "hold X Y MS" / "drag X Y DX DY MS"   scripts/ui-linux.sh へ(FIFO の経路)
#   "sleep <秒>"         待つ
#   "texts"              画面の文字をホストのログへ(最後にまとめて出す)
#   "async <手順>"       手順を裏で走らせてすぐ次へ(長押しの最中を撮るとき。例 "async hold 100 110 1500" "sleep 1" "shot armed")
# 環境変数 KYBOTOS_WAV_OUT=<path> を付けると、ミキサの出力を WAV に録る(§3.8)。
# 例: linux-app-shots.sh wasm-apps/mp3player/mp3player.wasm phase22c-step1 "shot stop" "tap 300 38" "sleep 1" "shot play" texts
#
# ホストは自分の子として、コマンドの入口(KYBOTOS_CMD_FIFO)つきで起動する。最後に FIFO へ `stop` を書いて終了し、
# ログ(captures/<タスク名>/host.log)の `CMD: text` 行と警告を出す。**手動の確認と撮影用**(回帰は linux-regress.sh)。
# 座標はアプリのレイアウトに依存する(各アプリの仕様書 docs/apps/<app>/spec.md)。
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
WASM=${1:?usage: linux-app-shots.sh <app.wasm> <task> <step>...}
TASK=${2:?usage: linux-app-shots.sh <app.wasm> <task> <step>...}
shift 2
case "$WASM" in /*) ;; *) WASM=$PWD/$WASM ;; esac
OUT=$REPO/captures/$TASK
mkdir -p "$OUT"
export DISPLAY=${DISPLAY:-:0}
export KYBOTOS_CMD_FIFO=$OUT/cmd.fifo
rm -f "$KYBOTOS_CMD_FIFO"
mkfifo "$KYBOTOS_CMD_FIFO"
exec 3<>"$KYBOTOS_CMD_FIFO"

(cd "$REPO/hosts/linux" && exec ./build/kybotos_host "$WASM" > "$OUT/host.log" 2>&1) &
host=$!
for _ in $(seq 1 50); do grep -q "app started" "$OUT/host.log" && break; sleep 0.1; done
sleep 0.5

run_step() {
    local step=$1 verb rest
    verb=${step%% *}; rest=${step#"$verb"}; rest=${rest# }
    case "$verb" in
        shot)  "$REPO/scripts/screen-still.sh" "$OUT" "$rest" ;;
        tap|hold|drag) "$REPO/scripts/ui-linux.sh" "$verb" $rest ;;
        sleep) sleep "$rest" ;;
        texts) echo texts >&3; sleep 0.3 ;;
        async) run_step "$rest" & ;;
        *) echo "unknown step: $step" >&2 ;;
    esac
}
for step in "$@"; do run_step "$step"; done
# 裏で走らせた手順だけを待つ(引数なしの wait はホストまで待ってしまう)
bg=$(jobs -p | grep -v "^$host\$")
[ -n "$bg" ] && wait $bg 2>/dev/null

echo stop >&3
for _ in $(seq 1 30); do kill -0 "$host" 2>/dev/null || break; sleep 0.1; done
kill "$host" 2>/dev/null
wait "$host" 2>/dev/null
exec 3>&-
rm -f "$KYBOTOS_CMD_FIFO"

grep -E "CMD: text |no free slot|WARN|ERROR|app st" "$OUT/host.log"
