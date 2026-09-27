#!/usr/bin/env python3
"""起動時のスプラッシュのロゴを RGB565 の C 配列にする(Phase 22a)。

用途: docs/images/kybotos.png を縮めて shared/splash_logo.c / splash_logo.h を書き出す。
      実機(LVGL の lv_image_dsc_t)と Linux ホスト(SDL のテクスチャ)が同じ配列を使う。
      生成物はコミットする(ビルドに Python / Pillow の依存を足さないため)。
使い方: python3 scripts/gen_splash_logo.py [<png>] [<一辺の px>]
        既定は docs/images/kybotos.png、240(画面 320x240 の高さいっぱい)。
        ロゴの画像を差し替えたら、これを実行して生成物をコミットする。
前提: Pillow。背景は単色で、左上の画素を背景色とみなす(左右の余白をその色で塗るため)。
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs/images/kybotos.png")
    side = int(sys.argv[2]) if len(sys.argv) > 2 else 240

    im = Image.open(src).convert("RGB").resize((side, side), Image.LANCZOS)
    # 余白の色はロゴと同じく RGB565 に丸めた値にする(24bit のままだと Linux でロゴとの境目が見える)
    r, g, b = im.getpixel((0, 0))
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    bg = (((r5 << 3) | (r5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) | ((b5 << 3) | (b5 >> 2))

    px = []
    for (r, g, b) in im.getdata():
        px.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

    rel = os.path.relpath(src, ROOT)
    header = os.path.join(ROOT, "shared/splash_logo.h")
    body = os.path.join(ROOT, "shared/splash_logo.c")

    with open(header, "w") as f:
        f.write(f"""/* 起動時のスプラッシュのロゴ(Phase 22a)。scripts/gen_splash_logo.py の生成物(手で直さない)。
 * 元画像: {rel} */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define SPLASH_LOGO_W {side}
#define SPLASH_LOGO_H {side}
#define SPLASH_LOGO_BG_RGB888 0x{bg:06x} /* ロゴの背景色。画面の余白をこの色で塗る */

/* RGB565(ホストのバイト順の uint16)、行優先 */
extern const uint16_t splash_logo_rgb565[SPLASH_LOGO_W * SPLASH_LOGO_H];

#ifdef __cplusplus
}}
#endif
""")

    with open(body, "w") as f:
        f.write(f"/* scripts/gen_splash_logo.py の生成物(手で直さない)。元画像: {rel} */\n")
        f.write('#include "splash_logo.h"\n\n')
        f.write("const uint16_t splash_logo_rgb565[SPLASH_LOGO_W * SPLASH_LOGO_H] = {\n")
        for i in range(0, len(px), 16):
            f.write("    " + ", ".join(f"0x{v:04x}" for v in px[i:i + 16]) + ",\n")
        f.write("};\n")

    print(f"wrote {os.path.relpath(header, ROOT)} / {os.path.relpath(body, ROOT)}: "
          f"{side}x{side}, bg #{bg:06x}, {len(px) * 2} bytes")


if __name__ == "__main__":
    main()
