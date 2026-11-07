#pragma once
#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 1) Move Config outside the class (name unchanged is fine)
struct PowerKeyConfig {
    gpio_num_t key_pin            = GPIO_NUM_6;  // Power button input
    gpio_num_t latch_pin          = GPIO_NUM_7;  // Self-hold control
    bool       key_active_low     = true;        // Active-low (pressed = 0)
    bool       use_internal_pullup= true;        // Recommend true when active-low
    uint32_t   debounce_ms        = 50;          // Debounce
    uint32_t   hold_ms            = 2000;        // Long-press shutdown threshold (ms)
    uint32_t   short_press_max_ms = 500;         // Release within this = short press
    uint32_t   force_home_min_ms  = 1000;        // Release after this = forced home (Phase 18a)
    uint32_t   poll_period_ms     = 10;          // Poll period when using a task
    bool       use_deepsleep_hold = true;        // Keep level during deep sleep after OFF
    // Phase 26 (KB_PWR_HOLD_ALWAYS): latch HIGH at every boot; the long press drops it while pressed
    // (the power goes on release) and restores it if still powered usb_check_ms after release. No deep sleep.
    bool       hold_always        = false;
    uint32_t   usb_check_ms       = 1000;
};

class PowerKey final {
public:
    using Config = PowerKeyConfig; // Add compatibility alias

  // 2) Keep default argument as-is
    explicit PowerKey(PowerKeyConfig cfg = PowerKeyConfig{}) noexcept;

    void init() noexcept;
    void poll() noexcept;
    void start_task(UBaseType_t prio = 5, uint32_t stack = 2048) noexcept;

    bool is_battery_mode()  const noexcept { return battery_mode_; }
    bool shutdown_issued()  const noexcept { return shutdown_issued_; }

    // Short-press callback (fires on release before short_press_max_ms).
    // Runs on the power-key task (small stack) — keep the callback tiny
    // (set a flag / atomic; do NOT call LVGL etc. directly).
    using ShortPressCb = void (*)(void*);
    void set_on_short_press(ShortPressCb cb, void* arg) noexcept {
        on_short_press_ = cb;
        short_press_arg_ = arg;
    }

    // Forced-home callback (Phase 18a). Fires on release when the key was held
    // for at least force_home_min_ms (battery mode cuts power at hold_ms while
    // still pressed, so that path wins there). Same constraints as above:
    // runs on the power-key task, keep it tiny.
    void set_on_force_home(ShortPressCb cb, void* arg) noexcept {
        on_force_home_ = cb;
        force_home_arg_ = arg;
    }

    // hold_always only. on_power_off: at hold_ms while still pressed, before the latch goes LOW
    // (stop the app, darken the screen). on_power_restore: after release, when the board is still
    // powered (external power) and the latch went back up. Both run on the power-key task and may block.
    void set_on_power_off(ShortPressCb cb, void* arg) noexcept {
        on_power_off_ = cb;
        power_off_arg_ = arg;
    }
    void set_on_power_restore(ShortPressCb cb, void* arg) noexcept {
        on_power_restore_ = cb;
        power_restore_arg_ = arg;
    }

    const PowerKeyConfig& config() const noexcept { return cfg_; }
    void set_config(const PowerKeyConfig& cfg) noexcept { cfg_ = cfg; }

private:
    static void task_trampoline(void* arg) noexcept;
    bool read_raw() const noexcept;
    bool debounce_read() noexcept;
    void cut_latch_and_maybe_sleep() noexcept;
    void power_off_while_pressed() noexcept;
    void restore_after_release() noexcept;
    static inline void gpio_conf(gpio_num_t pin, gpio_mode_t mode) noexcept;

private:
    PowerKeyConfig cfg_{};       // Store the externalized type as a member
    bool       battery_mode_      = false;
    bool       pressed_stable_    = false;
    bool       raw_prev_          = false;
    bool       press_armed_       = false;  // Seen released state (guards boot-held key)
    TickType_t raw_edge_ts_       = 0;
    TickType_t press_start_       = 0;
    ShortPressCb on_short_press_  = nullptr;
    void*      short_press_arg_   = nullptr;
    ShortPressCb on_force_home_   = nullptr;
    void*      force_home_arg_    = nullptr;
    ShortPressCb on_power_off_    = nullptr;
    void*      power_off_arg_     = nullptr;
    ShortPressCb on_power_restore_ = nullptr;
    void*      power_restore_arg_ = nullptr;
    bool       power_off_pending_ = false;  // hold_always: powered off at hold_ms, waiting for release
    volatile bool shutdown_issued_ = false;
    TaskHandle_t task_            = nullptr;
};
