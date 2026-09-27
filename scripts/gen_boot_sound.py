#!/usr/bin/env python3
"""起動音を 16bit PCM の C 配列にする(Phase 22a 追記)。

用途: docs/sounds/kybotos.mp4 を 44.1kHz モノラルの int16 に変換して shared/boot_sound.c / boot_sound.h を書き出す。
      実機(src/components/audio のミキサ)と Linux ホスト(hostapi_sdl.c のミキサ)が同じ配列を、
      スプラッシュを出したときに 1 回鳴らす。生成物はコミットする(ビルドに ffmpeg の依存を足さないため)。
使い方: python3 scripts/gen_boot_sound.py [<音声ファイル>] [<レベル %>]
        既定は docs/sounds/kybotos.mp4、レベル 50%。音を差し替えたら、これを実行して生成物をコミットする。
        レベルは元の音に掛ける倍率(鳴るときにはさらにマスター音量が掛かる)。
        既定の 50% は、100% では大きいというユーザーの試聴の結果から(Phase 22a 追記)。
前提: ffmpeg(形式は問わない。ffmpeg が読めれば mp3 / wav / m4a でもよい)。
"""
import array
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RATE = 44100  # 両ホストのミキサのレート(audio.cpp の kMixRate、hostapi_sdl.c の CLICK_RATE)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs/sounds/kybotos.mp4")
    level = int(sys.argv[2]) if len(sys.argv) > 2 else 50
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", src, "-vn", "-ac", "1", "-ar", str(RATE),
         "-f", "s16le", "-acodec", "pcm_s16le", "-"],
        check=True, stdout=subprocess.PIPE).stdout
    pcm = array.array("h")
    pcm.frombytes(raw)
    if sys.byteorder != "little":
        pcm.byteswap()
    pcm = array.array("h", (v * level // 100 for v in pcm))

    rel = os.path.relpath(src, ROOT)
    header = os.path.join(ROOT, "shared/boot_sound.h")
    body = os.path.join(ROOT, "shared/boot_sound.c")

    with open(header, "w") as f:
        f.write(f"""/* 起動音(Phase 22a 追記)。scripts/gen_boot_sound.py の生成物(手で直さない)。
 * 元の音: {rel}、レベル {level}% */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define BOOT_SOUND_RATE {RATE}
#define BOOT_SOUND_FRAMES {len(pcm)} /* {len(pcm) * 1000 // RATE} ms */

/* 16bit 符号付き、モノラル */
extern const int16_t boot_sound_pcm[BOOT_SOUND_FRAMES];

#ifdef __cplusplus
}}
#endif
""")

    with open(body, "w") as f:
        f.write(f"/* scripts/gen_boot_sound.py の生成物(手で直さない)。元の音: {rel} */\n")
        f.write('#include "boot_sound.h"\n\n')
        f.write("const int16_t boot_sound_pcm[BOOT_SOUND_FRAMES] = {\n")
        for i in range(0, len(pcm), 16):
            f.write("    " + ", ".join(str(v) for v in pcm[i:i + 16]) + ",\n")
        f.write("};\n")

    peak = max(abs(v) for v in pcm) if pcm else 0
    print(f"wrote {os.path.relpath(header, ROOT)} / {os.path.relpath(body, ROOT)}: "
          f"{len(pcm)} frames ({len(pcm) * 1000 // RATE} ms), {len(pcm) * 2} bytes, level {level}%, peak {peak}")


if __name__ == "__main__":
    main()
