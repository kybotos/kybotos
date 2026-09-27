#!/usr/bin/env bash
# scripts/linux-regress.sh — Linux ホストの回帰(docs/workflow.md §3.7 の手順をそのままスクリプトにしたもの。Phase 21d、Phase 22 で改訂)。
#
#   linux-regress.sh [--conf <path>] TASK
#     → captures/TASK/regress/<app>.log、1 行 1 アプリの要約。全 PASS なら exit 0
#
# 対象は scripts/device-regress.conf の APPS(実機の回帰と同じ)。`.wasm` は wasm-apps/<app>/<app>.wasm、
# conf の APP_WASM[<app>] があればそのパス。この repo の外のアプリを足して回すときは、この conf を source して
# APPS と APP_WASM を足した conf を --conf で渡す。
#
# 各アプリを **KYBOTOS_CMD_FIFO つきで**起動し(Phase 22。xdotool は使わない)、`app started` を待つ →
# conf の SCENARIO[<app>] があればシナリオ(scripts/regress-scenario.sh。実機と同じ手順)、無ければ
# HOLD_OVERRIDE[<app>] / HOLD_SEC 秒保持 → FIFO の `stop` で止める → ログで判定
# (started / stopped 各 1 以上、警告 0、シナリオ合格)。**highmark(WAMR プール消費)も出す**ので、前フェーズの値と突き合わせる。
# 反復(REPEAT_RUNS)は実機だけ(こちらは heap を見ないので 1 回)。
# MP3 が無ければ、実機のファームが SD に置くのと同じ 3 ファイルを hosts/linux/sdcard/music へコピーする(mp3player のシナリオ用)。
# **実機の回帰(device-regress.sh)と同時に走らせない**(§3.4)。
# 実行: ./scripts/hpane.sh run unix-build "<repo>/scripts/linux-regress.sh <タスク名>" 300000
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
CONF=$REPO/scripts/device-regress.conf
if [ "${1:-}" = "--conf" ]; then CONF=$2; shift 2; fi
TASK=$1
OUT=$REPO/captures/$TASK/regress; mkdir -p "$OUT"
export DISPLAY=${DISPLAY:-:0}
# shellcheck disable=SC1091
. "$CONF" || { echo "cannot read conf: $CONF" >&2; exit 2; }
declare -p APP_WASM >/dev/null 2>&1 || declare -A APP_WASM=()
declare -p HOLD_OVERRIDE >/dev/null 2>&1 || declare -A HOLD_OVERRIDE=()
declare -p SCENARIO >/dev/null 2>&1 || declare -A SCENARIO=()
# shellcheck source=regress-scenario.sh
. "$REPO/scripts/regress-scenario.sh"
SCN_PREFIX='CMD: '
scn_send() { printf '%s\n' "$1" >&3; }

MUSIC=$REPO/hosts/linux/sdcard/music
if [ ! -d "$MUSIC" ]; then
  mkdir -p "$MUSIC" && cp "$REPO"/src/components/wasm_runtime/assets/*.mp3 "$MUSIC"/
  echo "note: copied the sample MP3 files to $MUSIC"
fi

FIFO=$OUT/cmd.fifo
fail=0
for app in $APPS; do
  # ホストはこのスクリプトの子として起動する(ペインへ send すると、このスクリプトを走らせているペインに割り込む)
  wasm=${APP_WASM[$app]:-$REPO/wasm-apps/$app/$app.wasm}
  L=$OUT/$app.log
  t0=$SECONDS
  rm -f "$FIFO"; mkfifo "$FIFO"
  (cd "$REPO/hosts/linux" && KYBOTOS_CMD_FIFO=$FIFO exec ./build/kybotos_host "$wasm" > "$L" 2>&1) &
  hostpid=$!
  exec 3<>"$FIFO"  # 読み書き両用で開く(書き込み専用だと、ホストが起動に失敗したとき読み手を待って固まる)
  SCN_LOG=$L
  for _ in $(seq 1 60); do grep -q 'app started' "$L" 2>/dev/null && break; sleep 0.25; done
  scn="-"
  if [ -n "${SCENARIO[$app]:-}" ]; then
    run_scenario "$app" "${SCENARIO[$app]}"
    scn=$SCN_RESULT
    if [[ "$scn" != PASS* ]]; then
      echo "  $app scenario: $scn"; printf '    %s\n' "$SCN_LAST_TEXTS"
    fi
  else
    sleep "${HOLD_OVERRIDE[$app]:-${HOLD_SEC:-5}}"
  fi
  scn_send stop
  for _ in $(seq 1 60); do kill -0 "$hostpid" 2>/dev/null || break; sleep 0.25; done
  if kill -0 "$hostpid" 2>/dev/null; then echo "  $app did not stop; killing"; kill "$hostpid"; fi
  exec 3>&-
  s=$(grep -c 'app started' "$L"); t=$(grep -c 'app stopped' "$L"); w=$(grep -cE 'no free slot|WARN|ERROR' "$L")
  hm=$(grep -o 'highmark=[0-9]*' "$L" | tail -1)
  if [ "$s" -ge 1 ] && [ "$t" -ge 1 ] && [ "$w" -eq 0 ] && { [ "$scn" = "-" ] || [[ "$scn" == PASS* ]]; }; then
    r=PASS
  else
    r=FAIL; fail=1
  fi
  echo "$app started=$s stopped=$t warn=$w $hm scenario=$scn $((SECONDS - t0))s $r"
done
rm -f "$FIFO"
if pgrep -x kybotos_host >/dev/null; then echo "leftover kybotos_host"; fail=1; fi
exit $fail
