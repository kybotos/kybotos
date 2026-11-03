/* 画面の右上に常に出す電池の残量のバッジ(Phase 25a)。実機(src/components/wasm_runtime/battery_badge.cpp)と
 * Linux ホスト(hosts/linux/hostapi_sdl.c。電池が無いので模擬値)で共有する形と配色。値は RGB888、座標は論理座標(320×240)。
 *
 * 形: 右端 BADGE_RIGHT に右寄せ、ヘッダの文字と同じ行(y BADGE_TEXT_Y)。電池で動いているときはクリームの文字、
 * USB 給電のときは白抜き(地をクリームで塗り、文字を暗くする)。BADGE_LOW_PCT 以下は赤(USB のときは赤の白抜き)。
 * 予約する領域は x BADGE_RESERVE_X〜320(`100%` の白抜きの地の左端が約 277)。アプリのヘッダ右の部品はここを避ける。 */
#pragma once

#include <stdio.h>

#define BADGE_RIGHT        316 /* 文字の右端 */
#define BADGE_TEXT_Y       5   /* 文字の y(アプリのヘッダの題名と同じ) */
#define BADGE_BOX_Y        3   /* 白抜きの地の上端 */
#define BADGE_BOX_H        21
#define BADGE_BOX_PAD_L    3   /* 白抜きの地の左の余白(地は画面の右端 320 まで) */
#define BADGE_RESERVE_X    276 /* 予約する領域の左端 */
#define BADGE_LOW_PCT      20  /* 以下は赤 */

#define BADGE_RGB_TEXT     0xf3f1e4 /* 電池: クリーム */
#define BADGE_RGB_LOW      0xf06060 /* 20% 以下: 赤(メニューの状態行の赤と同じ系統) */
#define BADGE_RGB_INV_TEXT 0x101a14 /* 白抜きの文字: 暗い緑(メニューの行の地) */

static inline int battery_badge_text(char* buf, size_t n, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return snprintf(buf, n, "%d%%", pct);
}
