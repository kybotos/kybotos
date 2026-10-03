#!/usr/bin/env bash
# scripts/metronome-shots.sh — Linux ホストで metronome を動かし、操作しながら画面を撮る(Phase 22b)。
#
#   metronome-shots.sh <タスク名> <手順>...
#
# 手順は 1 つずつ引数で渡す(docs/workflow.md §3.6 の「UI を操作しながら撮る場合」をまとめたもの):
#   "shot <名前>"        静止画 → captures/<タスク名>/<名前>.png
#   "tap X Y" / "hold X Y MS" / "drag X Y DX DY MS"   scripts/ui-linux.sh へ(FIFO の経路)
#   "sleep <秒>"         待つ
#   "texts"              画面の文字をホストのログへ(最後にまとめて出す)
# 例: metronome-shots.sh phase22b-step1 "shot stop" "tap 300 38" "sleep 1.3" "shot play" texts
#
# ホストは自分の子として、コマンドの入口(KYBOTOS_CMD_FIFO)つきで起動する。最後に FIFO へ `stop` を書いて終了し、
# ログ(captures/<タスク名>/host.log)の `CMD: text` 行と警告を出す。**手動の確認と撮影用**(回帰は linux-regress.sh)。
# 前提のレイアウト(wasm-apps/metronome/src/lib.rs): ▶ / ■ の当たり判定はステータス行(y 26〜50)の x 240〜320。
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
TASK=${1:?usage: metronome-shots.sh <task> <step>...}
shift
OUT=$REPO/captures/$TASK
mkdir -p "$OUT"
export DISPLAY=${DISPLAY:-:0}
export KYBOTOS_CMD_FIFO=$OUT/cmd.fifo
rm -f "$KYBOTOS_CMD_FIFO"
mkfifo "$KYBOTOS_CMD_FIFO"
exec 3<>"$KYBOTOS_CMD_FIFO"

(cd "$REPO/hosts/linux" && exec ./build/kybotos_host ../../wasm-apps/metronome/metronome.wasm > "$OUT/host.log" 2>&1) &
host=$!
for _ in $(seq 1 50); do grep -q "app started" "$OUT/host.log" && break; sleep 0.1; done
sleep 0.5

for step in "$@"; do
    verb=${step%% *}; rest=${step#"$verb"}; rest=${rest# }
    case "$verb" in
        shot)  "$REPO/scripts/screen-still.sh" "$OUT" "$rest" ;;
        tap|hold|drag) "$REPO/scripts/ui-linux.sh" "$verb" $rest ;;
        sleep) sleep "$rest" ;;
        texts) echo texts >&3; sleep 0.3 ;;
        *) echo "unknown step: $step" >&2 ;;
    esac
done

echo stop >&3
for _ in $(seq 1 30); do kill -0 "$host" 2>/dev/null || break; sleep 0.1; done
kill "$host" 2>/dev/null
wait "$host" 2>/dev/null
exec 3>&-
rm -f "$KYBOTOS_CMD_FIFO"

grep -E "CMD: text |no free slot|WARN|ERROR|app st" "$OUT/host.log"
