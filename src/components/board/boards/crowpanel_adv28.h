// Board description: Elecrow CrowPanel Advance 2.8" HMI ESP32 AI Display (DIS01728A), PCB V1.2
// Selected by CONFIG_KYBOTOS_BOARD_CROWPANEL_ADV28 (board_pins.hpp). Phase 24.
// Pins checked against the net names of the V1.2 schematic (docs/results/phase24.md 0-b).
// Do not drive anything not listed here: GPIO14 (TFT_PWR, parts NC), GPIO45 (mic / wireless
// switch, strapping pin), GPIO0/1/2/3/9/10/46 (wireless adapter) are left alone.
#pragma once

// ---- LCD (ST7789, SPI2). No reset pin: the panel has its own RC reset ----
#define PIN_LCD_MOSI   GPIO_NUM_39
#define PIN_LCD_SCLK   GPIO_NUM_42
#define PIN_LCD_CS     GPIO_NUM_40
#define PIN_LCD_DC     GPIO_NUM_41
#define PIN_LCD_RST    GPIO_NUM_NC
#define PIN_LCD_BL     GPIO_NUM_38  // Backlight via NPN (Active High)

// Orientation (checked on the device in Phase 24 step 2)
#define KB_LCD_PANEL_MIRROR_X  false
#define KB_LCD_PANEL_MIRROR_Y  true
#define KB_LVGL_SWAP_XY        true
#define KB_LVGL_MIRROR_X       true
#define KB_LVGL_MIRROR_Y       false

// ---- Touch (GT911 per the vendor examples, I2C shared with the J7 connector) ----
// Phase 24 only scans the bus; the GT911 driver comes in Phase 24a.
#define KB_TOUCH_IC    KB_TOUCH_IC_NONE
#define PIN_TOUCH_SDA  GPIO_NUM_15
#define PIN_TOUCH_SCL  GPIO_NUM_16
#define PIN_TOUCH_INT  GPIO_NUM_47  // schematic IO47_TP_INT
#define PIN_TOUCH_RST  GPIO_NUM_48  // schematic IO48_TP_RST
#define I2C_TOUCH_PORT I2C_NUM_1

// ---- Audio (I2S std -> NS4168 mono class-D amplifier, right channel) ----
#define PIN_I2S_BCLK   GPIO_NUM_13
#define PIN_I2S_WS     GPIO_NUM_11
#define PIN_I2S_DOUT   GPIO_NUM_12
#define PIN_AMP_EN     GPIO_NUM_21  // IO21_NS_CTRL. LOW = amplifier on (vendor example)
#define KB_AMP_EN_ON_LEVEL 0

// ---- No power key / latch on this board ----
#define KB_HAS_POWER_LATCH 0

// ---- MIDI (UART1 on the J6 3.3V-UART1 connector; circuit to be wired in Phase 24c) ----
#define PIN_MIDI_TX    GPIO_NUM_17
#define PIN_MIDI_RX    GPIO_NUM_18

// ---- SD card (SDSPI on SPI3). 10k pull-ups on the board. No SDMMC wiring ----
#define PIN_SD_MOSI    GPIO_NUM_6
#define PIN_SD_MISO    GPIO_NUM_4
#define PIN_SD_SCLK    GPIO_NUM_5
#define PIN_SD_CS      GPIO_NUM_7
#define SD_SPI_HOST    SPI3_HOST
