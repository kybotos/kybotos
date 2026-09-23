#!/usr/bin/env bash
# scripts/linux-regress.sh — Linux ホストの回帰(docs/workflow.md §3.7 の手順をそのままスクリプトにしたもの。Phase 21d)。
#
#   linux-regress.sh TASK     → captures/TASK/regress/<app>.log、1 行 1 アプリの要約。全 PASS なら exit 0
#
# 対象は scripts/device-regress.conf の APPS と同じ 6 本。各アプリを起動 → pgrep で起動を確かめる →
# 5 秒保持 → pid と一致するウィンドウへ ESC → 終了を待つ → ログで判定(started / stopped 各 1 以上、警告 0)。
# **highmark(WAMR プール消費)も出す**ので、前フェーズの値と突き合わせる。
# **実機の回帰(device-regress.sh)と同時に走らせない**(§3.4)。
# 実行: ./scripts/hpane.sh run unix-build "<repo>/scripts/linux-regress.sh <タスク名>" 300000
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
TASK=$1
OUT=$REPO/captures/$TASK/regress; mkdir -p "$OUT"
export DISPLAY=${DISPLAY:-:0}
# shellcheck disable=SC1091
. "$REPO/scripts/device-regress.conf"
fail=0
for app in $APPS; do
  # ホストはこのスクリプトの子として起動する(ペインへ send すると、このスクリプトを走らせているペインに割り込む)
  (cd "$REPO/hosts/linux" && ./build/midibox_host "../../wasm-apps/$app/$app.wasm" > "$OUT/$app.log" 2>&1 &)
  for _ in $(seq 1 60); do pgrep -x midibox_host >/dev/null && break; sleep 0.25; done
  sleep 5
  pid=$(pgrep -x midibox_host | head -1); WIN=""
  for w in $(xdotool search --name "MidiAppBox WASM host"); do
    [ "$(xdotool getwindowpid "$w" 2>/dev/null)" = "$pid" ] && WIN=$w
  done
  [ -n "$WIN" ] && xdotool key --window "$WIN" Escape
  for _ in $(seq 1 60); do pgrep -x midibox_host >/dev/null || break; sleep 0.25; done
  L=$OUT/$app.log
  s=$(grep -c 'app started' "$L"); t=$(grep -c 'app stopped' "$L"); w=$(grep -cE 'no free slot|WARN|ERROR' "$L")
  hm=$(grep -o 'highmark=[0-9]*' "$L" | tail -1)
  if [ "$s" -ge 1 ] && [ "$t" -ge 1 ] && [ "$w" -eq 0 ]; then r=PASS; else r=FAIL; fail=1; fi
  echo "$app started=$s stopped=$t warn=$w $hm $r"
done
if pgrep -x midibox_host >/dev/null; then echo "leftover midibox_host"; fail=1; fi
exit $fail
