/* ランチャーのメニューの配色と形(Phase 22a 追記、Phase 22d で見直し)。実機(launcher.cpp)と Linux(hosts/linux/main.c)で共有する。
 * スプラッシュのロゴ(docs/images/kybotos.png)の色から作った: 濃緑 #1f3d2e / クリーム #f3f1e4 / 若葉 #8fd18b / 中緑 #3e6648。
 * Phase 22d: アプリ(metronome / mp3player。wasm-apps/appui の theme)と揃え、焼き付きを避けて背景を黒にした。
 * 題名の下に中緑の細線、行は暗い緑の地 + 左端に中緑の印、`Settings` はヘッダ右に若葉の文字だけで置く。
 * 値は RGB888、座標は論理座標(320×240)。 */
#pragma once

#define MENU_BG_RGB888            0x000000 /* 背景: 黒(Phase 22d。22a では濃緑 0x183c29) */
#define MENU_TITLE_RGB888         0xf3f1e4 /* 題名: クリーム */
#define MENU_RULE_RGB888          0x3e6648 /* 題名の下の線: 中緑 */
#define MENU_STATUS_RGB888        0x5f7466 /* 状態行: くすんだ緑(アプリの手引きと同じ) */
#define MENU_SETTINGS_TEXT_RGB888 0x8fd18b /* ヘッダ右の Settings: 若葉 */
#define MENU_SETTINGS_HI_RGB888   0xf3f1e4 /* 同・押下 / ホバー: クリーム */
#define MENU_APP_BG_RGB888        0x101a14 /* アプリの行: 暗い緑(mp3player の一覧の行と同じ) */
#define MENU_APP_HI_RGB888        0x24483a /* 同・押下 / ホバー */
#define MENU_APP_MARK_RGB888      0x3e6648 /* 同・左端の印: 中緑 */
#define MENU_APP_TEXT_RGB888      0xf3f1e4 /* 同・文字: クリーム */

/* 形(両ホスト共通) */
#define MENU_TITLE_X     10
#define MENU_TITLE_Y     8
#define MENU_RULE_Y      28 /* 題名の下の線(高さ 2) */
#define MENU_RULE_H      2
#define MENU_SETTINGS_RIGHT 310 /* Settings の文字の右端 */
#define MENU_SETTINGS_HIT_X 220 /* Settings の当たり判定: x 220〜320、y 0〜MENU_RULE_Y */
#define MENU_ROW_X       10
#define MENU_ROW_W       300
#define MENU_ROW_Y0      38 /* 1 行目のアプリ */
#define MENU_ROW_H       24
#define MENU_ROW_GAP     4
#define MENU_ROW_MARK_W  4  /* 左端の印 */
#define MENU_ROW_TEXT_X  12 /* 行の左端から文字まで */
#define MENU_STATUS_Y    220
