#pragma once
// Battery voltage sensing (Phase 25). Reads the board's battery-voltage ADC (a resistor divider on
// PIN_BAT_ADC), averages many samples, and converts the voltage to a charge estimate (%).
//
// The sensing is described by the board (board_pins.hpp: KB_HAS_BATT_SENSE, PIN_BAT_ADC, KB_BAT_DIV_MUL).
// A board without it (KB_HAS_BATT_SENSE 0, e.g. CrowPanel for now) creates no task and touches no ADC:
// Init() does nothing and Get() returns false.
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

// Battery voltage (mV) -> charge estimate (%). Piecewise linear, clamped to 0..100. Pure function.
int PercentFromMv(int mv);

}  // namespace battery
