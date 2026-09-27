#pragma once

#include <cstddef>
#include <cstddef>

namespace wasmrt {

// アプリ配置ディレクトリ
constexpr const char* kAppsDir = "/sdcard/apps";

// SD をマウントし kAppsDir を用意する。ディレクトリが無ければ作成し、
// 埋め込みのサンプルアプリ(.wasm)を書き込む(初回セットアップ)。
// 失敗時は false を返し status にメッセージを入れる。
// FATFS を使うため十分なスタック(8KB 以上)のタスクから呼ぶこと。
bool launcher_prepare_sd(char* status, size_t status_len);

// 起動時のスプラッシュ(ロゴ)を表示する(Phase 22a)。LVGL の開始後に 1 回だけ呼ぶ。
// 最初の launcher_show が、表示から最短 1.5 秒待ってメニューに切り替え、スプラッシュを消す。
void launcher_show_splash();

// メニュー画面を(初回は作成して)表示する。kAppsDir を再スキャンして
// .wasm の一覧を(拡張子を取った名前で)出す。status_msg は状態行に表示(nullptr なら変更しない)。
// lvgl_port_lock を取るのでどのタスクからでも呼べる。スプラッシュが出ていればその残り時間だけ待つ。
void launcher_show(const char* status_msg);

// 名前(拡張子なし可)で kAppsDir 内のアプリを起動する。メニューのタップを
// 経由しないので、LVGL タスク以外(シリアルコマンド等)から呼んでよい。
// 起動できたら true。err には失敗理由(静的文字列)が入る。
bool launcher_launch_by_name(const char* name, const char** err, char* path_out,
                             size_t path_out_len);

// wasmrt::app_start に渡す停止コールバック。メニューへ復帰する。
void launcher_on_app_stopped(const char* error);

// 5C 検証用: mp3player.wasm の起動→停止を 10 サイクル回して free heap をログし、
// 壊れた .wasm のロードエラー処理も確認する(CONFIG_KYBOTOS_WASM_CYCLE_TEST)。
// launcher_prepare_sd 成功後、十分なスタックのタスクから呼ぶこと。
void launcher_run_cycle_test();

} // namespace wasmrt
