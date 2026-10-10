#!/usr/bin/env bash
# fw.sh — ボードを指定して実機のファームをビルド / フラッシュ / モニタする(Phase 24)。
#
#   scripts/fw.sh <board> build
#   scripts/fw.sh <board> flash
#   scripts/fw.sh <board> monitor [<ログのパス(ホスト側。repo の中)>]
#   scripts/fw.sh <board> info          ビルドディレクトリ・sdkconfig・ポートを表示するだけ
#   scripts/fw.sh list                  ボードの一覧(src/boards/)
#
# docs/workflow.md §3.2 の docker の形(README のタグの生イメージを都度起動、export.sh を明示 source)を、
# ボードの名前からビルドディレクトリ・sdkconfig・ポートを決めて組み立てるだけのラッパ。
# 実行は hpane 経由(build / flash は run、monitor は常駐なので send):
#   ./scripts/hpane.sh run esp32-build "<repo>/scripts/fw.sh crowpanel_adv28 build" 1800000
#   ./scripts/hpane.sh send esp32-monitor "<repo>/scripts/fw.sh crowpanel_adv28 monitor <repo>/captures/<タスク名>/monitor.log"
#
# ボードとディレクトリ:
#   既定のボード(waveshare_lcd28)は今までどおり src/build と src/sdkconfig(§3.2 の生のコマンドと同じものを使う)。
#   それ以外のボードは src/build-<board>/ に、sdkconfig もその中。sdkconfig は「無いときだけ」defaults から作られるので、
#   ボードごとに分けないと前のボードの設定が残る(src/CMakeLists.txt は食い違うと止まる)。
#
# 環境変数:
#   KYBOTOS_PORT_<BOARD>  そのボードのポート(<BOARD> は大文字。例 KYBOTOS_PORT_CROWPANEL_ADV28)。
#                         2 枚をつなぐと /dev/ttyACM0 / 1 の順番が挿す順で変わるので、/dev/serial/by-id/... を勧める
#                         (by-id の名前には MAC が入る。値は各自の環境なので repo には書かない)
#   KYBOTOS_PORT          上が無いときのポート(既定 /dev/ttyACM0)
#   KYBOTOS_DEV_APPS      1 / 0 で -DKYBOTOS_DEV_APPS=ON / OFF を渡す。未設定なら渡さない(キャッシュの値のまま。§3.4)
#
# flash は、ビルドディレクトリの sdkconfig のボードが <board> と一致しなければ止まる(焼き間違いの防止)。
# 起動ログの最初の行 `APP: board: <board>` でも確かめられる。
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5"
DEFAULT_BOARD=waveshare_lcd28

usage() { sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }

[ $# -ge 1 ] || usage
if [ "$1" = list ]; then ls "$REPO/src/boards"; exit 0; fi
BOARD="$1"; ACTION="${2:-}"; [ -n "$ACTION" ] || usage
[ -f "$REPO/src/boards/$BOARD/sdkconfig.defaults" ] || { echo "fw.sh: unknown board '$BOARD' (scripts/fw.sh list)" >&2; exit 2; }

if [ "$BOARD" = "$DEFAULT_BOARD" ]; then
    BUILD_DIR=build; SDKCONFIG=sdkconfig
else
    BUILD_DIR="build-$BOARD"; SDKCONFIG="build-$BOARD/sdkconfig"
fi
IDF_ARGS="-B $BUILD_DIR -DSDKCONFIG=$SDKCONFIG -DKYBOTOS_BOARD=$BOARD"
case "${KYBOTOS_DEV_APPS:-}" in
    1) IDF_ARGS="$IDF_ARGS -DKYBOTOS_DEV_APPS=ON" ;;
    0) IDF_ARGS="$IDF_ARGS -DKYBOTOS_DEV_APPS=OFF" ;;
esac

PORT_VAR="KYBOTOS_PORT_$(echo "$BOARD" | tr 'a-z' 'A-Z')"
PORT="${!PORT_VAR:-${KYBOTOS_PORT:-/dev/ttyACM0}}"

DOCKER="docker run --rm -v $REPO:/workspaces/kybotos -w /workspaces/kybotos/src $IMAGE"
port_args() {
    # docker の --device には実体を渡す(by-id のシンボリックリンクを解決する)
    local dev; dev="$(readlink -f "$PORT")"
    [ -c "$dev" ] || { echo "fw.sh: port $PORT ($dev) not found" >&2; exit 1; }
    if [ -z "${!PORT_VAR:-}" ]; then
        echo "fw.sh: warning: $PORT_VAR is not set; using $PORT (check that it is the $BOARD board)" >&2
    fi
    DEV="$dev"
    DOCKER_TTY="docker run --rm -it -v $REPO:/workspaces/kybotos -w /workspaces/kybotos/src --device=$dev --group-add $(stat -c '%g' "$dev") $IMAGE"
}
check_board() {
    local f="$REPO/src/$SDKCONFIG" got
    [ -f "$f" ] || { echo "fw.sh: $f not found (build first)" >&2; exit 1; }
    got="$(sed -n 's/^CONFIG_KYBOTOS_BOARD_NAME="\(.*\)"$/\1/p' "$f")"
    [ "$got" = "$BOARD" ] || { echo "fw.sh: $f is for board '$got', not '$BOARD'" >&2; exit 1; }
}

case "$ACTION" in
    info)
        echo "board=$BOARD build_dir=src/$BUILD_DIR sdkconfig=src/$SDKCONFIG port=$PORT ($PORT_VAR=${!PORT_VAR:-unset})"
        echo "idf.py $IDF_ARGS" ;;
    build)
        $DOCKER bash -c "source /opt/esp-idf/export.sh && idf.py $IDF_ARGS build" ;;
    flash)
        check_board; port_args
        $DOCKER_TTY bash -c "source /opt/esp-idf/export.sh && idf.py $IDF_ARGS -p $DEV flash" ;;
    monitor)
        check_board; port_args
        LOG="${3:-}"
        if [ -n "$LOG" ]; then
            mkdir -p "$(dirname "$LOG")"
            C_LOG="/workspaces/kybotos/$(realpath --relative-to="$REPO" "$LOG")"
            $DOCKER_TTY bash -c "source /opt/esp-idf/export.sh && PYTHONUNBUFFERED=1 idf.py -B $BUILD_DIR -p $DEV monitor | tee $C_LOG"
        else
            $DOCKER_TTY bash -c "source /opt/esp-idf/export.sh && idf.py -B $BUILD_DIR -p $DEV monitor"
        fi ;;
    *) usage ;;
esac
