#pragma once
// 画面の右上に常に出す電池の残量のバッジ(Phase 25a)。形と配色は shared/battery_badge.h(Linux と共有)。
// `lv_layer_top()` に置くのでメニューでもアプリの中でも出る。クリックは受けない(タッチは下へ通る)。
// 電池の検知が無いボード(KB_HAS_BATT_SENSE 0)では何も作らない。
namespace wasmrt {

// LVGL が動いてから 1 回呼ぶ(lvgl_port_lock を内部で取る)。約 2 秒ごとに battery::Get を読んで更新する(新しいタスクは作らない)。
void battery_badge_init();

// スクリーンセーバー(減光)と画面消灯の間はバッジを消す(焼き付き防止。ユーザー要件 2026-11-03)。
// screensaver.cpp が状態の変わり目に呼ぶ(true = 消す、false = 元に戻す)。lvgl_port_lock を保持した状態で呼ぶこと。
void battery_badge_suppress(bool suppress);

} // namespace wasmrt
