/*
 * コマンドの入口(Phase 22)。実機のシリアルコンソールと同じ語彙で、回帰のシナリオから UI を操作する。
 *
 * 環境変数 KYBOTOS_CMD_FIFO=<path> があれば、その FIFO を非ブロッキングで開いて 1 行 1 コマンドを読む
 * (無ければ何もしない)。応答は stdout の `CMD: ...` 行(行ごとに fflush)。
 *
 *   ping                    → CMD: pong
 *   tap X Y                 → CMD: tap done X Y       (80ms 押して離す)
 *   hold X Y MS             → CMD: hold done X Y MS
 *   drag X Y DX DY MS       → CMD: drag done ...      (押す → MS 待つ → 8 段 × 40ms で動かす → 離す)
 *   key back                → CMD: key ok back        (BACKSPACE と同じ = 実機の電源キー短押し)
 *   texts                   → CMD: text X Y RRGGBB <文字> × N、CMD: texts done N
 *   stop                    → CMD: stop ok            (ESC と同じ = アプリに聞かずに止める)
 *
 * 座標はアプリの論理座標(320x240)。タッチはマウスと同じ入口(host_sdl_push_touch / _move)へ注入するので、
 * マスター設定の関所も通る。アプリが動いていないときは tap / hold / drag / key / texts / stop に `idle` を返す。
 * 時間のかかる操作は main ループの 1 周ごとに進め(ループを止めない)、終わるまで次の行を読まない。
 */
#pragma once
#include <stdbool.h>

typedef enum {
    CMD_ACTION_NONE = 0,
    CMD_ACTION_KEY_BACK, /* main が key_back_req を立てる */
    CMD_ACTION_STOP,     /* main が ESC と同じ停止をする */
} cmd_action_t;

void cmd_fifo_open(void);
void cmd_fifo_close(void);
/* main ループの 1 周ごとに呼ぶ。app_running はアプリが動いているか */
cmd_action_t cmd_fifo_poll(bool app_running);
