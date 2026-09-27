/* 起動音(Phase 22a 追記)。scripts/gen_boot_sound.py の生成物(手で直さない)。
 * 元の音: docs/sounds/kybotos.mp4、レベル 50% */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_SOUND_RATE 44100
#define BOOT_SOUND_FRAMES 28224 /* 640 ms */

/* 16bit 符号付き、モノラル */
extern const int16_t boot_sound_pcm[BOOT_SOUND_FRAMES];

#ifdef __cplusplus
}
#endif
