#pragma once
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_types.h"   // i2c_port_t, I2C_NUM_0, etc.
#include "driver/spi_common.h"  // SPI3_HOST

// Board selection (Phase 24). The board is chosen at build time by the Kconfig choice
// KYBOTOS_BOARD (src/main/Kconfig.projbuild), which src/boards/<board>/sdkconfig.defaults sets.
// Each board describes its pins and features in one header under boards/.
// Adding a board: docs/results/phase24.md 0-d f / docs/workflow.md.

// Touch controller kinds for KB_TOUCH_IC
#define KB_TOUCH_IC_NONE   0   // no driver yet: the bus is only scanned (touch injection still works)
#define KB_TOUCH_IC_CST328 1
#define KB_TOUCH_IC_FT6336 2   // FocalTech FT6336U (FT6x36), 0x38, 8-bit registers (Phase 24a)

#if defined(CONFIG_KYBOTOS_BOARD_WAVESHARE_LCD28)
#include "boards/waveshare_lcd28.h"
#elif defined(CONFIG_KYBOTOS_BOARD_CROWPANEL_ADV28)
#include "boards/crowpanel_adv28.h"
#else
#error "No board selected (CONFIG_KYBOTOS_BOARD_*)"
#endif

// A board may have the power key without the self-hold latch (KB_HAS_POWER_KEY 1, KB_HAS_POWER_LATCH 0):
// the key then gives back / forced home only, and the long press does not power off.
#ifndef KB_HAS_POWER_KEY
#define KB_HAS_POWER_KEY KB_HAS_POWER_LATCH
#endif
#if KB_HAS_POWER_KEY && !KB_HAS_POWER_LATCH && !defined(PIN_PWR_LATCH)
#define PIN_PWR_LATCH GPIO_NUM_NC
#endif

// When the self-hold goes up (Phase 26). 0 (Waveshare): only when the key is held at boot (battery power);
// on external power the latch is left alone and the long press does not power off.
// 1 (CrowPanel + I/O board): always at boot, because the charger reaches the battery only through the
// latched switch. The long press then powers off after the key is released, and if the board is still
// running 1 s after the latch went down (USB power), the latch goes back up.
#ifndef KB_PWR_HOLD_ALWAYS
#define KB_PWR_HOLD_ALWAYS 0
#endif

// Battery voltage sensing (Phase 25). A board with a resistor divider from the battery to an ADC pin sets
// KB_HAS_BATT_SENSE 1, PIN_BAT_ADC and KB_BAT_DIV_MUL (battery voltage = ADC voltage x KB_BAT_DIV_MUL).
// Phase 26a: KB_BATT_SRC says where the value comes from. KB_BATT_SRC_ADC (default) is the divider above;
// KB_BATT_SRC_FUEL_GAUGE is a MAX17048 fuel gauge at KB_FG_I2C_ADDR on the touch I2C bus (CrowPanel + I/O board).
#define KB_BATT_SRC_ADC        1
#define KB_BATT_SRC_FUEL_GAUGE 2
#ifndef KB_HAS_BATT_SENSE
#define KB_HAS_BATT_SENSE 0
#endif
#ifndef KB_BATT_SRC
#define KB_BATT_SRC KB_BATT_SRC_ADC
#endif
#if KB_HAS_BATT_SENSE && KB_BATT_SRC == KB_BATT_SRC_ADC && !defined(KB_BAT_DIV_MUL)
#error "KB_HAS_BATT_SENSE 1 needs PIN_BAT_ADC and KB_BAT_DIV_MUL in the board description"
#endif
#if KB_HAS_BATT_SENSE && KB_BATT_SRC == KB_BATT_SRC_FUEL_GAUGE && !defined(KB_FG_I2C_ADDR)
#error "KB_BATT_SRC_FUEL_GAUGE needs KB_FG_I2C_ADDR in the board description"
#endif

// Phase 26a: before the touch I2C bus is created, clock SCL (up to 9 times) and send a STOP if SDA is held low.
// For boards with a device on that bus that keeps running across an ESP32 reset (the battery-powered fuel gauge).
#ifndef KB_I2C_BUS_RECOVER
#define KB_I2C_BUS_RECOVER 0
#endif

#ifndef KB_AMP_EN_ON_LEVEL
#define KB_AMP_EN_ON_LEVEL 1
#endif

// --- LCD parameters (both boards: 240x320 panel used as 320x240) ---
static constexpr int LCD_H_RES = 240;
static constexpr int LCD_V_RES = 320;
