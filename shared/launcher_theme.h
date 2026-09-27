/* ランチャーのメニューの配色(Phase 22a 追記)。実機(launcher.cpp)と Linux(hosts/linux/main.c)で共有する。
 * スプラッシュのロゴ(docs/images/kybotos.png)の色から作った: 濃緑 #1f3d2e / クリーム #f3f1e4 / 若葉 #8fd18b / 中緑 #3e6648。
 * 背景はスプラッシュの余白と同じ色(RGB565 に丸めた値)にして、ロゴからメニューへ地続きに見せる。
 * 値は RGB888。 */
#pragma once

#define MENU_BG_RGB888            0x183c29 /* 背景: スプラッシュと同じ濃緑(SPLASH_LOGO_BG_RGB888) */
#define MENU_TITLE_RGB888         0xf3f1e4 /* 題名: クリーム */
#define MENU_STATUS_RGB888        0x8fa894 /* 状態行: くすんだ緑 */
#define MENU_SETTINGS_BG_RGB888   0x24483a /* Settings の行: 背景より少し明るい緑 */
#define MENU_SETTINGS_HI_RGB888   0x3e6648 /* 同・押下 / ホバー */
#define MENU_SETTINGS_TEXT_RGB888 0x8fd18b /* 同・文字: 若葉 */
#define MENU_APP_BG_RGB888        0x3e6648 /* アプリの行: 中緑 */
#define MENU_APP_HI_RGB888        0x629565 /* 同・押下 / ホバー */
#define MENU_APP_TEXT_RGB888      0xf3f1e4 /* 同・文字: クリーム */
