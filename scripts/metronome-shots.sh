#!/usr/bin/env bash
# scripts/metronome-shots.sh — Linux ホストで metronome を動かし、操作しながら画面を撮る(Phase 22b)。
#
#   metronome-shots.sh <タスク名> <手順>...
#
# 手順の書式は scripts/linux-app-shots.sh(Phase 22c で任意のアプリ向けにそちらへ移した)。
# 環境変数 METRONOME_WAV=<path> を付けると、ミキサの出力を WAV に録る(KYBOTOS_WAV_OUT。§3.8)。
# 例: metronome-shots.sh phase22b-step1 "shot stop" "tap 300 38" "sleep 1.3" "shot play" texts
# 前提のレイアウト(wasm-apps/metronome/src/lib.rs): ▶ / ■ の当たり判定はステータス行(y 26〜50)の x 240〜320。
REPO=$(cd "$(dirname "$0")/.." && pwd)
[ -n "${METRONOME_WAV:-}" ] && export KYBOTOS_WAV_OUT=$METRONOME_WAV
exec "$REPO/scripts/linux-app-shots.sh" "$REPO/wasm-apps/metronome/metronome.wasm" "$@"
