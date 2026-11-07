#include "power_key.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include <inttypes.h>

static const char* TAG = "PWR_KEY";

PowerKey::PowerKey(PowerKeyConfig cfg) noexcept : cfg_(cfg) {}

inline void PowerKey::gpio_conf(gpio_num_t pin, gpio_mode_t mode) noexcept {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, mode);
}

bool PowerKey::read_raw() const noexcept {
    int lvl = gpio_get_level(cfg_.key_pin);
    return cfg_.key_active_low ? (lvl == 0) : (lvl != 0);
}

bool PowerKey::debounce_read() noexcept {
    const bool raw = read_raw();
    const TickType_t now = xTaskGetTickCount();

    if (raw != raw_prev_) {
        raw_prev_    = raw;
        raw_edge_ts_ = now;
    }
    if (pdTICKS_TO_MS(now - raw_edge_ts_) >= cfg_.debounce_ms) {
        pressed_stable_ = raw;
    }
    return pressed_stable_;
}

void PowerKey::cut_latch_and_maybe_sleep() noexcept {
    if (shutdown_issued_) return;
    shutdown_issued_ = true;

    // 1) Drive self-hold LOW (cut power path)
    gpio_set_level(cfg_.latch_pin, 0);

    // 2) Optional: hold level during deep sleep to prevent accidental wake
    if (cfg_.use_deepsleep_hold) {
        gpio_hold_en(cfg_.latch_pin);
        gpio_deep_sleep_hold_en();
        ESP_LOGW(TAG, "Shutdown (>= %" PRIu32 " ms) -> latch LOW & deep sleep", cfg_.hold_ms);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_deep_sleep_start(); // not expected to return
    } else {
        ESP_LOGW(TAG, "Shutdown (>= %" PRIu32 " ms) -> latch LOW", cfg_.hold_ms);
    }
}

// Phase 26 (hold_always), at hold_ms while the key is still pressed: stop the app, darken the screen and
// drop the latch, like the Waveshare (latch LOW + deep sleep while pressed). The battery path stays on
// through the key itself while it is pressed, so the power actually goes when the key is released.
void PowerKey::power_off_while_pressed() noexcept {
    ESP_LOGW(TAG, "PWR: long press >= %" PRIu32 " ms -> power off (cut on release)", cfg_.hold_ms);
    if (on_power_off_) on_power_off_(power_off_arg_);   // stop the app and wait, backlight off
    gpio_set_level(cfg_.latch_pin, 0);
    ESP_LOGW(TAG, "PWR: hold OFF");
}

// Released after power_off_while_pressed() and still running usb_check_ms later: external power (USB)
// keeps the board up. Restore the hold so the battery charges, and bring the screen back.
void PowerKey::restore_after_release() noexcept {
    vTaskDelay(pdMS_TO_TICKS(cfg_.usb_check_ms));
    gpio_set_level(cfg_.latch_pin, 1);
    ESP_LOGW(TAG, "PWR: still powered %" PRIu32 " ms after release (external power) -> hold ON again",
             cfg_.usb_check_ms);
    if (on_power_restore_) on_power_restore_(power_restore_arg_);
}

void PowerKey::init() noexcept {
    // Input pin
    gpio_conf(cfg_.key_pin, GPIO_MODE_INPUT);
    if (cfg_.use_internal_pullup && cfg_.key_active_low) {
        gpio_set_pull_mode(cfg_.key_pin, GPIO_PULLUP_ONLY);
    } else {
        gpio_set_pull_mode(cfg_.key_pin, GPIO_FLOATING);
    }

    // No self-hold latch (latch_pin = NC): the key gives back / forced home only.
    // battery_mode_ stays false, so the long press never powers off.
    if (cfg_.latch_pin == GPIO_NUM_NC) {
        battery_mode_ = false;
        ESP_LOGI(TAG, "Key on GPIO%d without latch: back / force-home only", (int)cfg_.key_pin);
        raw_prev_       = read_raw();
        raw_edge_ts_    = xTaskGetTickCount();
        pressed_stable_ = raw_prev_;
        press_start_    = 0;
        shutdown_issued_= false;
        return;
    }

    // Self-hold pin
    gpio_conf(cfg_.latch_pin, GPIO_MODE_OUTPUT);

    // Phase 26 (CrowPanel + I/O board): hold at every boot, as early as possible. The key must be held
    // until here when starting on battery; on USB the hold is what lets the charger reach the battery.
    if (cfg_.hold_always) {
        gpio_set_level(cfg_.latch_pin, 1);
        battery_mode_ = read_raw();   // key held at boot = started on battery (informational)
        ESP_LOGI(TAG, "PWR: hold ON at boot (%s)",
                 battery_mode_ ? "key pressed: battery start" : "no key: external power start");
        raw_prev_       = read_raw();
        raw_edge_ts_    = xTaskGetTickCount();
        pressed_stable_ = raw_prev_;
        press_start_    = 0;
        shutdown_issued_= false;
        return;
    }

    // Start with latch=LOW to stabilize
    gpio_set_level(cfg_.latch_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    // If booted by key press (battery mode), set latch=HIGH to enable self-hold
    if (read_raw()) {
        battery_mode_ = true;
        gpio_set_level(cfg_.latch_pin, 1);
        ESP_LOGI(TAG, "Battery mode: latch ON (key pressed at boot)");
    } else {
        battery_mode_ = false; // external power
        ESP_LOGI(TAG, "External power: key control disabled");
    }

    // Initialize debounce
    raw_prev_       = read_raw();
    raw_edge_ts_    = xTaskGetTickCount();
    pressed_stable_ = raw_prev_;
    press_start_    = 0;
    shutdown_issued_= false;
}

void PowerKey::poll() noexcept {
    // Monitor the key in both battery and external-power modes so that
    // short-press events work; the long-press power-off remains
    // battery-mode-only (external power cannot be cut via the latch anyway).
    if (shutdown_issued_) return;

    const bool pressed = debounce_read();
    const TickType_t now = xTaskGetTickCount();

    if (pressed) {
        // Only count presses that started from an observed released state,
        // so the boot-time press (battery power-on) is not misdetected.
        if (press_armed_ && press_start_ == 0) {
            press_start_ = now; // start of stable press
        }
        if (cfg_.hold_always && press_start_ != 0 && !power_off_pending_) {
            if (pdTICKS_TO_MS(now - press_start_) >= cfg_.hold_ms) {
                power_off_pending_ = true;
                power_off_while_pressed();
            }
        } else if (!cfg_.hold_always && battery_mode_ && press_start_ != 0) {
            const uint32_t held_ms = pdTICKS_TO_MS(now - press_start_);
            if (held_ms >= cfg_.hold_ms) {
                // Power off immediately when threshold is reached
                cut_latch_and_maybe_sleep();
            }
        }
    } else {
        if (power_off_pending_) {
            power_off_pending_ = false;
            press_start_ = 0;
            restore_after_release();   // reached only on external power
        } else if (press_start_ != 0) {
            const uint32_t held_ms = pdTICKS_TO_MS(now - press_start_);
            if (held_ms <= cfg_.short_press_max_ms) {
                if (on_short_press_) on_short_press_(short_press_arg_);
            } else if (held_ms >= cfg_.force_home_min_ms) {
                // Phase 18a: forced home. The short press is the app's "back"
                // key, so a stuck app needs an escape hatch that does not ask
                // the app. In battery mode hold_ms (power off) fires first,
                // while still pressed, so this only sees 1000..2000 ms there.
                if (on_force_home_) on_force_home_(force_home_arg_);
            }
            press_start_ = 0;
        }
        press_armed_ = true; // released state observed; next press counts
    }
}

void PowerKey::task_trampoline(void* arg) noexcept {
    auto* self = static_cast<PowerKey*>(arg);
    const TickType_t pd = pdMS_TO_TICKS(self->cfg_.poll_period_ms);
    for (;;) {
        self->poll();
        vTaskDelay(pd);
    }
}

void PowerKey::start_task(UBaseType_t prio, uint32_t stack) noexcept {
    if (task_) return;
    xTaskCreate(&PowerKey::task_trampoline, "pwr_key_loop", stack, this, prio, &task_);
}
