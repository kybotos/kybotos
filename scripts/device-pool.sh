#!/usr/bin/env bash
# scripts/device-pool.sh — 実機で 1 本のアプリの WAMR プール消費を測る(Phase 21d で整理)。
#
#   device-pool.sh TASK APP     → captures/TASK/pool-APP.log に monitor のログ、最後に `wamr pool` の行を出す
#
# docs/workflow.md §3.4 の手順そのもの: **モニタを起動してボードをリセットし、`MBCMD: ready` の後に
# シリアルから `run APP` を 1 回だけ送る**(highmark が正しく出るのは起動後に最初にロードしたアプリだけ)。
# 終わったら `stop` を送るが、**モニタは esp32-monitor ペインに残る**。続けて device-regress.sh を
# 走らせる前に止めること(§3.4 の注意)。
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
TASK=$1; APP=$2
IMG=ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5
DEV=/dev/ttyACM0
[ -e "$DEV" ] || { echo "device-pool: $DEV が無い(実機が接続されていない)" >&2; exit 1; }
mkdir -p "$REPO/captures/$TASK"; LOG=$REPO/captures/$TASK/pool-$APP.log; rm -f "$LOG"
"$REPO/scripts/hpane.sh" send esp32-monitor "docker run --rm -it -v $REPO:/workspaces/MidiAppBox -w /workspaces/MidiAppBox/src --device=$DEV --group-add $(stat -c '%g' $DEV) $IMG bash -c 'source /opt/esp-idf/export.sh && PYTHONUNBUFFERED=1 idf.py -p $DEV monitor | tee /workspaces/MidiAppBox/captures/$TASK/pool-$APP.log'"
for _ in $(seq 1 120); do grep -aq "MBCMD: ready" "$LOG" 2>/dev/null && break; sleep 0.5; done
grep -aq "MBCMD: ready" "$LOG" || { echo "device-pool: console did not come up" >&2; exit 1; }
"$REPO/scripts/hpane.sh" send esp32-monitor "run $APP"
for _ in $(seq 1 40); do grep -aq "wamr pool\|failed" "$LOG" && break; sleep 0.5; done
sleep 2
"$REPO/scripts/hpane.sh" send esp32-monitor "heap"; sleep 1
"$REPO/scripts/hpane.sh" send esp32-monitor "stop"; sleep 2
grep -a "wamr pool\|failed\|MBCMD: heap" "$LOG" | tail -3
