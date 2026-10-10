// Board description: Elecrow CrowPanel Advance 2.8" HMI ESP32 AI Display (DIS01728A), PCB V1.2
// Selected by CONFIG_KYBOTOS_BOARD_CROWPANEL_ADV28 (board_pins.hpp). Phase 24.
// Pins checked against the net names of the V1.2 schematic (docs/results/phase24.md 0-b).
// Do not drive anything not listed here: GPIO14 (TFT_PWR, parts NC), GPIO45 (mic / wireless
// switch, strapping pin), GPIO0/2/3/9/10/46 (wireless adapter) are left alone. GPIO1 is the home key.
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

// ---- Touch (FT6336U at 0x38, chip ID 0x64; I2C shared with the J7 connector). Phase 24a ----
// The vendor examples use a GT911 driver, but only 0x38 answers on this board (docs/results/phase24a.md).
// INT and RST are not used: the controller answers right after boot, and it is polled like the CST328.
#define KB_TOUCH_IC    KB_TOUCH_IC_FT6336
#define PIN_TOUCH_SDA  GPIO_NUM_15
#define PIN_TOUCH_SCL  GPIO_NUM_16
#define PIN_TOUCH_INT  GPIO_NUM_47  // schematic IO47_TP_INT (not used)
#define PIN_TOUCH_RST  GPIO_NUM_48  // schematic IO48_TP_RST (not used)
#define I2C_TOUCH_PORT I2C_NUM_1
// Raw points are portrait 240 x 320; same rotation as the Waveshare CST328 (display x = raw y, display y = 239 - raw x)
#define KB_TOUCH_RAW_W      240
#define KB_TOUCH_RAW_H      320
#define KB_TOUCH_ROT        1
#define KB_TOUCH_CAL_XMIN   0
#define KB_TOUCH_CAL_XMAX   239
#define KB_TOUCH_CAL_YMIN   0
#define KB_TOUCH_CAL_YMAX   319

// ---- Audio (I2S std -> NS4168 mono class-D amplifier, right channel) ----
#define PIN_I2S_BCLK   GPIO_NUM_13
#define PIN_I2S_WS     GPIO_NUM_11
#define PIN_I2S_DOUT   GPIO_NUM_12
#define PIN_AMP_EN     GPIO_NUM_21  // IO21_NS_CTRL. LOW = amplifier on (vendor example)
#define KB_AMP_EN_ON_LEVEL 0

// ---- Home key: a push switch between IO1 (J9 TX2, wireless adapter) and GND, internal pull-up ----
// Same key handling as the Waveshare power key (short press = back, 1 s or more = forced home),
// but there is no self-hold latch, so the long press does not power off.
#define KB_HAS_POWER_KEY   1
#define KB_HAS_POWER_LATCH 0
#define PIN_PWR_KEY_IN     GPIO_NUM_1

// ---- MIDI (UART1 on the J6 3.3V-UART1 connector; circuit to be wired in Phase 24c) ----
#define PIN_MIDI_TX    GPIO_NUM_17
#define PIN_MIDI_RX    GPIO_NUM_18

// ---- SD card (SDSPI on SPI3). 10k pull-ups on the board. No SDMMC wiring ----
#define PIN_SD_MOSI    GPIO_NUM_6
#define PIN_SD_MISO    GPIO_NUM_4
#define PIN_SD_SCLK    GPIO_NUM_5
#define PIN_SD_CS      GPIO_NUM_7
#define SD_SPI_HOST    SPI3_HOST
