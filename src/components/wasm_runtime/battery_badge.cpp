#include "battery_badge.hpp"
#include "board_pins.hpp"

#if KB_HAS_BATT_SENSE
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "battery.hpp"
#include "battery_badge.h"  // shared/

namespace wasmrt {

namespace {
constexpr uint32_t kUpdateMs = 2000;  // 電池の値の更新(約 1.6 秒)より少し遅くてよい

lv_obj_t* s_label = nullptr;
bool s_suppressed = false;   // スクリーンセーバー / 消灯の間 true(LVGL タスクの文脈で読み書きする)

void update_cb(lv_timer_t*)
{
    battery::Reading r;
    if (s_suppressed || !battery::Get(&r)) {
        lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[8];
    battery_badge_text(buf, sizeof(buf), r.pct);
    lv_label_set_text(s_label, buf);

    const bool low = r.pct <= BADGE_LOW_PCT;
    const uint32_t accent = low ? BADGE_RGB_LOW : BADGE_RGB_TEXT;
    if (battery::ExternalPower()) {
        // USB 給電: 白抜き(地を塗って文字を暗くする)
        lv_obj_set_style_bg_opa(s_label, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_label, lv_color_hex(accent), 0);
        lv_obj_set_style_text_color(s_label, lv_color_hex(BADGE_RGB_INV_TEXT), 0);
    } else {
        lv_obj_set_style_bg_opa(s_label, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_label, lv_color_hex(accent), 0);
    }
    lv_obj_remove_flag(s_label, LV_OBJ_FLAG_HIDDEN);
}
}  // namespace

void battery_badge_suppress(bool suppress)
{
    s_suppressed = suppress;
    if (!s_label) return;
    if (suppress) lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
    else          update_cb(nullptr);   // 復帰したらすぐ出す
}

void battery_badge_init()
{
    if (s_label) return;
    lvgl_port_lock(0);
    s_label = lv_label_create(lv_layer_top());
    lv_obj_remove_flag(s_label, LV_OBJ_FLAG_CLICKABLE);   // タッチは下のアプリ / メニューへ通す
    lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);          // 最初の値が来るまで出さない
    lv_obj_set_style_pad_left(s_label, BADGE_BOX_PAD_L, 0);
    lv_obj_set_style_pad_right(s_label, 320 - BADGE_RIGHT, 0);
    lv_obj_set_style_pad_top(s_label, BADGE_TEXT_Y - BADGE_BOX_Y, 0);
    lv_obj_set_style_pad_bottom(s_label, BADGE_BOX_H - (BADGE_TEXT_Y - BADGE_BOX_Y) - 16, 0);  // 14pt の行高 16px を想定
    lv_obj_align(s_label, LV_ALIGN_TOP_RIGHT, 0, BADGE_BOX_Y);
    lv_label_set_text(s_label, "");
    lv_timer_create(update_cb, kUpdateMs, nullptr);
    lvgl_port_unlock();
}

} // namespace wasmrt

#else  // 電池の検知が無いボード: 何も作らない

namespace wasmrt {
void battery_badge_init() {}
void battery_badge_suppress(bool) {}
} // namespace wasmrt

#endif
