// Board description: Waveshare ESP32-S3-Touch-LCD-2.8 (ST7789 + CST328 + PCM5101)
// Selected by CONFIG_KYBOTOS_BOARD_WAVESHARE_LCD28 (board_pins.hpp). Phase 24.
// The pins and values below are the ones the firmware used before Phase 24
// (moved here from board_pins.hpp, touch.cpp, audio.hpp and app_main.cpp unchanged).
#pragma once

// ---- LCD (ST7789, SPI2) ----
#define PIN_LCD_MOSI   GPIO_NUM_45
#define PIN_LCD_SCLK   GPIO_NUM_40
#define PIN_LCD_CS     GPIO_NUM_42
#define PIN_LCD_DC     GPIO_NUM_41
#define PIN_LCD_RST    GPIO_NUM_39
#define PIN_LCD_BL     GPIO_NUM_5   // Backlight (Active High)

// Orientation: esp_lcd_panel_mirror() after init, then the LVGL port rotation
#define KB_LCD_PANEL_MIRROR_X  false
#define KB_LCD_PANEL_MIRROR_Y  true
#define KB_LVGL_SWAP_XY        true
#define KB_LVGL_MIRROR_X       true
#define KB_LVGL_MIRROR_Y       false

// ---- Touch (CST328, I2C) ----
#define KB_TOUCH_IC    KB_TOUCH_IC_CST328
#define PIN_TOUCH_SDA  GPIO_NUM_1
#define PIN_TOUCH_SCL  GPIO_NUM_3
#define PIN_TOUCH_INT  GPIO_NUM_4   // Low active (set -1 if unconnected)
#define PIN_TOUCH_RST  GPIO_NUM_2   // Low reset
// Before Phase 24 board_pins.hpp declared a constexpr I2C_NUM_0 here, but touch.cpp
// tests the name with #ifndef and fell back to I2C_NUM_1. Keep the value actually used.
#define I2C_TOUCH_PORT I2C_NUM_1
// Raw -> display: rotation (touch.cpp map_basic_to_display: 0=0, 1=90 CW, 2=180, 3=270 CW) and the initial
// raw range for the range learning (moved from touch.cpp in Phase 24a, values unchanged)
#define KB_TOUCH_ROT        1
#define KB_TOUCH_CAL_XMIN   1
#define KB_TOUCH_CAL_XMAX   239
#define KB_TOUCH_CAL_YMIN   6
#define KB_TOUCH_CAL_YMAX   298

// ---- Audio (I2S std -> PCM5101 stereo DAC) ----
#define PIN_I2S_BCLK   GPIO_NUM_48
#define PIN_I2S_WS     GPIO_NUM_38
#define PIN_I2S_DOUT   GPIO_NUM_47
#define PIN_AMP_EN     GPIO_NUM_NC  // no amplifier enable pin

// ---- Power key with self-hold latch (battery operation) ----
#define KB_HAS_POWER_LATCH 1
#define PIN_PWR_KEY_IN     GPIO_NUM_6
#define PIN_PWR_LATCH      GPIO_NUM_7

// ---- MIDI (UART1, own circuit; Phase 8a / 8c) ----
// OUT: 2SC1815 transistor drive, 31250bps 8N1, TXD inverted (uart_set_line_inverse is required).
// IN: TLP2361 photocoupler (totem-pole, inverting): the idle current-loop state already reads as
//     UART idle H, so RXD is not inverted.
#define PIN_MIDI_TX    GPIO_NUM_18
#define PIN_MIDI_RX    GPIO_NUM_15

// ---- SD card (SDSPI on SPI3; the LCD uses SPI2) ----
#define PIN_SD_MOSI    GPIO_NUM_17
#define PIN_SD_MISO    GPIO_NUM_16
#define PIN_SD_SCLK    GPIO_NUM_14
#define PIN_SD_CS      GPIO_NUM_21
#define SD_SPI_HOST    SPI3_HOST

// ---- SD card (SDMMC) probe wiring. sdcard.cpp probes SDMMC only when PIN_SDMMC_CLK is defined ----
// (skipped by default with PSRAM: CONFIG_KYBOTOS_SD_SKIP_SDMMC_PROBE)
#ifdef CONFIG_EXAMPLE_PIN_CLK
#define PIN_SDMMC_CLK ((gpio_num_t)CONFIG_EXAMPLE_PIN_CLK)
#else
#define PIN_SDMMC_CLK GPIO_NUM_14
#endif
#ifdef CONFIG_EXAMPLE_PIN_CMD
#define PIN_SDMMC_CMD ((gpio_num_t)CONFIG_EXAMPLE_PIN_CMD)
#else
#define PIN_SDMMC_CMD GPIO_NUM_17
#endif
#ifdef CONFIG_EXAMPLE_PIN_D0
#define PIN_SDMMC_D0 ((gpio_num_t)CONFIG_EXAMPLE_PIN_D0)
#else
#define PIN_SDMMC_D0 GPIO_NUM_16
#endif
// Optional extra data lines for 4-bit mode; -1 if unused
#ifdef CONFIG_EXAMPLE_PIN_D1
#define PIN_SDMMC_D1 CONFIG_EXAMPLE_PIN_D1
#else
#define PIN_SDMMC_D1 -1
#endif
#ifdef CONFIG_EXAMPLE_PIN_D2
#define PIN_SDMMC_D2 CONFIG_EXAMPLE_PIN_D2
#else
#define PIN_SDMMC_D2 -1
#endif
#ifdef CONFIG_EXAMPLE_PIN_D3
#define PIN_SDMMC_D3 CONFIG_EXAMPLE_PIN_D3
#elif defined(CONFIG_SD_Card_D3)
#define PIN_SDMMC_D3 CONFIG_SD_Card_D3
#else
#define PIN_SDMMC_D3 -1
#endif
