#pragma once
#include <cstdint>
#include "lvgl.h"
#include "driver/i2c_master.h"

class Touch {
public:
    // Receive LVGL v9 display handle and register pointer input
    void init(lv_display_t* disp);
};

// Phase 22: タッチの注入(シリアルコンソールの tap / hold / drag から使う)。
// 座標は LVGL の論理座標(= アプリの座標、320x240)。LVGL の読み取り(indev_read_cb)の段で
// 差し込むので、指と同じ経路(LVGL → マスター設定の関所 → アプリのキュー)を通る。
// 注入中(press から、release を LVGL が一度読むまで)は実タッチを読まない。
// 押している時間は呼び出し側が刻む(LVGL の読み取り周期より長く保つこと)。
namespace touch_inject {
void press(int16_t x, int16_t y);  // 押す。押したまま呼べば移動
void release();                    // 離す(最後に press した座標で)
} // namespace touch_inject

// Phase 26a: the I2C bus the touch driver created (I2C_TOUCH_PORT), so that other devices on the same bus
// (the I/O board's fuel gauge) can be added with i2c_master_bus_add_device(). nullptr before Touch::init().
// The ESP-IDF master driver serialises transactions between devices on one bus.
i2c_master_bus_handle_t touch_i2c_bus();
