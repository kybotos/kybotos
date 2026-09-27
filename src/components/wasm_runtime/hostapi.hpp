#pragma once

#include <cstdint>

#include "lvgl.h"   // Phase 21b: メニュー画面に判定を付けるため

namespace wasmrt {

// ホスト API (module "env") を WAMR に登録する。wasm_runtime_full_init 後、
// instantiate より前に呼ぶこと(runtime_init() が呼ぶ)。
bool hostapi_register_natives();

// マスター設定を開く(Phase 21b。メニューの `Settings` 行から)
void hostapi_masterui_open();

// メニュー画面に上端スワイプの判定を付ける(Phase 21b)
void hostapi_masterui_attach_menu(lv_obj_t* menu_screen);

// アプリ用の LVGL スクリーンを新規作成してアクティブにする(スロットも初期化)。
// アプリ起動直前に呼ぶ。LVGL タスクまたは lvgl_port_lock 下から。
void hostapi_app_screen_create();

// アプリ用スクリーンを破棄する。先に別スクリーン(メニュー)をロードしてから
// 呼ぶこと(アクティブなスクリーンは削除できないため)。
void hostapi_app_screen_destroy();

// Phase 22: アプリ画面の文字スロットを列挙する(シリアルコンソールの `texts`)。
// アプリ画面が無ければ -1、あれば emit を呼んだ件数を返す。emit は LVGL のロック下で
// 呼ばれるので、ログを出す程度に留めること。空文字のスロットも渡す。
using TextEmitFn = void (*)(int32_t x, int32_t y, uint32_t rgb, const char* text);
int hostapi_dump_texts(TextEmitFn emit);

// オーディオを停止し状態を STOPPED に戻す(Phase 6B ライフサイクル契約)。
// アプリ起動直前と破棄時に wasm_runtime が呼ぶ。
void hostapi_audio_reset();

} // namespace wasmrt
