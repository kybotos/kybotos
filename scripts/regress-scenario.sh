# scripts/regress-scenario.sh — 回帰のシナリオ(アプリ内の操作と表示の確認)の実行部(Phase 22)。
#
# device-regress.sh(実機、シリアルコンソール)と linux-regress.sh(Linux、KYBOTOS_CMD_FIFO)が source する。
# docs/workflow.md §3.4 / §3.7 の一部。違いは「コマンドを送る関数」と「応答が出るログ」だけで、呼ぶ側が決める:
#   SCN_LOG     応答が出るログファイル(実機 = monitor.log、Linux = アプリのログ)
#   SCN_PREFIX  応答行の接頭辞(実機 = 'KBCMD: '、Linux = 'CMD: ')
#   scn_send    コマンドを 1 行送る関数(引数 = コマンド)
#
# シナリオは conf の SCENARIO[<app>] に `;` 区切りで書く。座標はアプリの論理座標(320x240)で、
# 実機と Linux で同じシナリオを使う:
#   wait S                 S 秒待つ(小数可)
#   tap X Y / hold X Y MS / drag X Y DX DY MS
#                          タッチを注入する(応答 `<verb> done` を待つ)
#   key back               戻るキー(実機 = 電源キー短押し、Linux = BACKSPACE)
#   expect[@秒] <文字列>   `texts` を 0.5 秒ごとに取り、どれかの行が <文字列> を含むまで待つ(既定 5 秒)。
#                          行は `text X Y RRGGBB <表示>` の `text ` より後ろなので、色も含めて書ける
#                          (例: `expect 40c0ff rgb`)
#
# run_scenario <app> <シナリオ> が 0 を返せば合格。結果は SCN_RESULT(表に出す短い文字列)、
# 不合格のときは SCN_LAST_TEXTS(最後に取った画面の文字)にも残す。

SCN_RESULT=""
SCN_LAST_TEXTS=""

_scn_lines() { [ -f "$SCN_LOG" ] && wc -l < "$SCN_LOG" || echo 0; }

# _scn_collect <開始行> <正規表現> <秒>: 開始行より後ろで、接頭辞つきの応答行が正規表現に一致するまで待つ。
# 一致したら 0 を返し、開始行より後ろの応答行(接頭辞を外したもの)を stdout に出す
_scn_collect() {
    local from="$1" pat="$2" timeout="$3" deadline lines
    deadline=$(( $(date +%s%N) / 1000000 + timeout * 1000 ))
    while :; do
        lines=$(tail -n "+$((from + 1))" "$SCN_LOG" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' |
                grep -a -F "$SCN_PREFIX" | sed "s/.*$SCN_PREFIX//")
        if printf '%s\n' "$lines" | grep -q -E "$pat"; then
            printf '%s\n' "$lines"
            return 0
        fi
        [ $(( $(date +%s%N) / 1000000 )) -ge "$deadline" ] && return 1
        sleep 0.1
    done
}

# _scn_cmd <コマンド> <応答の正規表現> <秒>: 送って応答を待つ。応答行を stdout に出す
_scn_cmd() {
    local mark
    mark=$(_scn_lines)
    scn_send "$1"
    _scn_collect "$mark" "$2" "$3"
}

# 画面の文字を取る(`text ` の後ろだけを出す)。取れなければ 1
scn_texts() {
    local out
    out=$(_scn_cmd texts '^texts (done|idle)' 5) || return 1
    printf '%s\n' "$out" | sed -n 's/^text //p'
}

run_scenario() {
    local app="$1" scenario="$2" step n=0 verb rest out want timeout deadline
    SCN_RESULT=""; SCN_LAST_TEXTS=""
    local -a steps
    IFS=';' read -r -a steps <<<"$scenario"
    for step in "${steps[@]}"; do
        step=$(printf '%s' "$step" | sed 's/^ *//; s/ *$//')
        [ -z "$step" ] && continue
        n=$((n + 1))
        verb=${step%% *}; rest=${step#"$verb"}; rest=${rest# }
        case "$verb" in
            wait)
                sleep "$rest" ;;
            tap|hold|drag)
                if ! out=$(_scn_cmd "$step" "^$verb (done|err|idle)" 15) ||
                   ! printf '%s\n' "$out" | grep -q "^$verb done"; then
                    SCN_RESULT="FAIL(手順 $n: $step → $(printf '%s\n' "$out" | grep "^$verb" | tail -1))"
                    return 1
                fi ;;
            key)
                if ! out=$(_scn_cmd "$step" '^key (ok|idle|err)' 10) ||
                   ! printf '%s\n' "$out" | grep -q '^key ok'; then
                    SCN_RESULT="FAIL(手順 $n: $step → $(printf '%s\n' "$out" | grep '^key' | tail -1))"
                    return 1
                fi ;;
            expect|expect@*)
                timeout=5
                [ "$verb" != expect ] && timeout=${verb#expect@}
                want=$rest
                deadline=$(( $(date +%s) + timeout ))
                while :; do
                    SCN_LAST_TEXTS=$(scn_texts)
                    printf '%s\n' "$SCN_LAST_TEXTS" | grep -q -F -- "$want" && break
                    if [ "$(date +%s)" -ge "$deadline" ]; then
                        SCN_RESULT="FAIL(手順 $n: $want が ${timeout} 秒以内に出ない)"
                        return 1
                    fi
                    sleep 0.5
                done ;;
            *)
                SCN_RESULT="FAIL(手順 $n: 不明な手順 '$step')"
                return 1 ;;
        esac
    done
    SCN_RESULT="PASS($n 手順)"
    return 0
}
