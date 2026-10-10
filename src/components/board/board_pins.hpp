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

#ifndef KB_AMP_EN_ON_LEVEL
#define KB_AMP_EN_ON_LEVEL 1
#endif

// --- LCD parameters (both boards: 240x320 panel used as 320x240) ---
static constexpr int LCD_H_RES = 240;
static constexpr int LCD_V_RES = 320;
