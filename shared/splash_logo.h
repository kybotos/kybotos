/* 起動時のスプラッシュのロゴ(Phase 22a)。scripts/gen_splash_logo.py の生成物(手で直さない)。
 * 元画像: docs/images/kybotos.png */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPLASH_LOGO_W 240
#define SPLASH_LOGO_H 240
#define SPLASH_LOGO_BG_RGB888 0x183c29 /* ロゴの背景色。画面の余白をこの色で塗る */

/* RGB565(ホストのバイト順の uint16)、行優先 */
extern const uint16_t splash_logo_rgb565[SPLASH_LOGO_W * SPLASH_LOGO_H];

#ifdef __cplusplus
}
#endif
