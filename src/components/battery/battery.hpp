#pragma once
// Battery voltage sensing (Phase 25). Reads the board's battery-voltage ADC (a resistor divider on
// PIN_BAT_ADC), averages many samples, and converts the voltage to a charge estimate (%).
//
// The sensing is described by the board (board_pins.hpp: KB_HAS_BATT_SENSE, KB_BATT_SRC, PIN_BAT_ADC, KB_BAT_DIV_MUL).
// A board without it (KB_HAS_BATT_SENSE 0) creates no task and touches no ADC:
// Init() does nothing and Get() returns false.
//
// Phase 26a: KB_BATT_SRC_FUEL_GAUGE (CrowPanel + I/O board) reads a MAX17048 on the touch I2C bus instead:
// voltage = VCELL, charge = the chip's SOC. Init() does nothing there; AttachI2cBus() (after the touch driver
// made the bus) probes the chip and starts the task. The charge/discharge rate (CRATE) also drives
// ExternalPower(): charging -> external power, discharging -> battery.
//
// Limits (docs/results/phase25.md):
//  - "No battery" cannot be told from the voltage: with USB power and no battery the charger holds the pin at
//    about 4.19-4.20 V, the same as a full battery on charge. On USB the reading is also ~50 mV above the
//    battery's own voltage. The charge estimate is trustworthy on battery power (key-boot battery mode).
//  - The curve is a generic single-cell LiPo table (provisional; Phase 25b reviews it with a discharge log).
#include <cstdint>

namespace battery {

struct Reading {
    int mv;   // battery voltage in millivolts (calibrated ADC voltage x divider)
    int pct;  // charge estimate 0..100
};

// Starts the sampling task (once). No-op on boards without KB_HAS_BATT_SENSE.
void Init();

// Latest reading. Returns false if the board has no sensing or no sample has been taken yet.
// Lock-free; safe to call from any task.
bool Get(Reading* out);

// Phase 26a: fuel-gauge boards only. Adds the gauge to the given I2C bus (i2c_master_bus_handle_t), checks that it
// answers (logs the result) and starts the reading task. Call once from app_main after Touch::init().
// No-op on other boards or with a null bus.
void AttachI2cBus(void* i2c_bus);

// Phase 26a: extra values from the fuel gauge (for the serial "batt"). Returns false on other boards / no sample yet.
struct GaugeReading {
    int soc_x10;        // chip SOC in 0.1 % (may exceed 1000 near full)
    int crate_x10;      // charge rate in 0.1 %/h, + = charging
    int table_pct;      // PercentFromMv(voltage), for comparison with the chip SOC
};
bool GetGauge(GaugeReading* out);

// Whether the board booted on external (USB) power rather than the battery (the power key was not pressed at boot).
// Set once at boot by app_main; the display shows a different look for it (Phase 25a). Default: false (battery).
void SetExternalPower(bool ext);
bool ExternalPower();

// Battery voltage (mV) -> charge estimate (%). Piecewise linear, clamped to 0..100. Pure function.
int PercentFromMv(int mv);

}  // namespace battery
