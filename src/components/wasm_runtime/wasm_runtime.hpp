#pragma once

namespace wasmrt {

// ---- Phase 5: ランタイム常駐+アプリライフサイクル(ホスト所有) ----

// 起動時に一度: WAMR full_init + ホスト API 登録。以後 destroy しない。
bool runtime_init();

// アプリ停止時コールバック。error は正常停止なら nullptr、異常なら静的文字列。
// アプリ実行スレッドから呼ばれる(LVGL を触るなら lv_async_call 経由にすること)。
using AppStoppedCb = void (*)(const char* error);

// SD 上の .wasm を専用スレッドでロード・実行する。
// app_init() → 100ms 周期で app_tick() → app_request_stop() で停止、
// (export されていれば)app_exit() を呼んでから破棄する。
// 成功=スレッド起動で true(ロード失敗等は on_stopped(error) で通知)。
bool app_start(const char* path, AppStoppedCb on_stopped);

// 実行中アプリに停止を要求する(非同期。停止完了は on_stopped で通知)。
void app_request_stop();

// Phase 18a: 戻るキー(電源キー短押し)を実行中アプリへ渡すよう要求する。
// 実際の配送は app_tick の切れ目で行われ、アプリが任意 export app_key() を
// 持たない / 0 を返した場合は app_request_stop() と同じ停止になる。
// 割り込み・小スタックのタスクから呼べる(atomic フラグを立てるだけ)。
void app_request_key_back();

// Phase 18a: 強制ホーム(キー 1〜2 秒の長押し)。アプリに聞かずに停止する。
// app_request_stop() との違いはログ 1 行だけ(切り分け用)。
void app_request_force_home();

bool app_is_running();

} // namespace wasmrt
