#include "battery.hpp"
#include "board_pins.hpp"

#include <atomic>
#if KB_HAS_BATT_SENSE && KB_BATT_SRC == KB_BATT_SRC_FUEL_GAUGE
#include "esp_log.h"
#include "driver/i2c_master.h"
#elif KB_HAS_BATT_SENSE
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#endif

namespace battery {

namespace {
std::atomic<bool> s_external_power{false};
// Provisional single-cell LiPo discharge table (mV, %), highest first.
struct Pt { int mv; int pct; };
constexpr Pt kCurve[] = {
    {4200, 100}, {4110, 90}, {4020, 80}, {3950, 70}, {3870, 60}, {3840, 50},
    {3800, 40},  {3770, 30}, {3730, 20}, {3690, 10}, {3610, 5},  {3300, 0},
};
constexpr int kCurveN = (int)(sizeof(kCurve) / sizeof(kCurve[0]));
}  // namespace

void SetExternalPower(bool ext) { s_external_power.store(ext, std::memory_order_relaxed); }
bool ExternalPower() { return s_external_power.load(std::memory_order_relaxed); }

int PercentFromMv(int mv)
{
    if (mv >= kCurve[0].mv) return kCurve[0].pct;
    if (mv <= kCurve[kCurveN - 1].mv) return kCurve[kCurveN - 1].pct;
    for (int i = 0; i + 1 < kCurveN; i++) {
        const Pt& hi = kCurve[i];
        const Pt& lo = kCurve[i + 1];
        if (mv <= hi.mv && mv >= lo.mv) {
            // linear interpolation, rounded to nearest
            const int span = hi.mv - lo.mv;
            return lo.pct + ((mv - lo.mv) * (hi.pct - lo.pct) + span / 2) / span;
        }
    }
    return 0;
}

#if KB_HAS_BATT_SENSE && KB_BATT_SRC == KB_BATT_SRC_FUEL_GAUGE

// ---- MAX17048 fuel gauge (Phase 26a) ----
namespace {
constexpr const char* TAG = "BATT";
constexpr uint8_t kRegVcell   = 0x02;   // 78.125 uV/LSB
constexpr uint8_t kRegSoc     = 0x04;   // 1/256 %/LSB
constexpr uint8_t kRegVersion = 0x08;   // 0x001x
constexpr uint8_t kRegCrate   = 0x16;   // 0.208 %/h/LSB, signed
// Polled every 2 s (the badge timer); the chip itself updates VCELL every 250 ms
// (every 45 s in hibernate: the chip default, not changed here)
// External power is guessed from the charge rate, with a dead band (CRATE lags by about a minute)
constexpr int kChargeX10    = 20;       // > +2.0 %/h: charging -> external power
constexpr int kDischargeX10 = -20;      // < -2.0 %/h: discharging -> battery

i2c_master_dev_handle_t s_dev = nullptr;
std::atomic<int> s_mv{-1};              // -1 = no sample yet
// Set by app_main only after the probe succeeded; the badge timer may already be polling (it then sees nullptr)
std::atomic<i2c_master_dev_handle_t> s_gauge{nullptr};
std::atomic<int> s_soc_x10{0};
std::atomic<int> s_crate_x10{0};

// Read from the LVGL task (PollGauge(), by the badge timer), like the touch controller on the same bus:
// no task of our own, so no extra stack in internal RAM. Not logged; values are read with the serial "batt".
bool read16(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t* out)
{
    uint8_t rx[2] = {0, 0};
    if (i2c_master_transmit_receive(dev, &reg, 1, rx, 2, 50) != ESP_OK) return false;
    *out = (uint16_t)((rx[0] << 8) | rx[1]);   // big-endian
    return true;
}

}  // namespace

void Init() {}   // the gauge needs the I2C bus: AttachI2cBus()

void AttachI2cBus(void* i2c_bus)
{
    if (!i2c_bus || s_dev) return;   // s_dev: added to the bus (app_main only)
    i2c_device_config_t cfg{};
    cfg.device_address  = KB_FG_I2C_ADDR;
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.scl_speed_hz    = 100000;
    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device((i2c_master_bus_handle_t)i2c_bus, &cfg, &dev) != ESP_OK) {
        ESP_LOGE(TAG, "fuel gauge: cannot add 0x%02X to the bus", KB_FG_I2C_ADDR);
        return;
    }
    uint16_t ver = 0;
    if (!read16(dev, kRegVersion, &ver)) {
        ESP_LOGW(TAG, "fuel gauge (MAX17048) not answering at 0x%02X: no battery level", KB_FG_I2C_ADDR);
        (void)i2c_master_bus_rm_device(dev);
        return;
    }
    s_dev = dev;
    ESP_LOGI(TAG, "fuel gauge MAX17048 at 0x%02X, version 0x%04X", KB_FG_I2C_ADDR, ver);
    s_gauge.store(dev, std::memory_order_release);   // the badge timer polls from here on
}

void PollGauge()
{
    i2c_master_dev_handle_t dev = s_gauge.load(std::memory_order_acquire);
    if (!dev) return;
    uint16_t vcell = 0, soc = 0, crate = 0;
    if (!(read16(dev, kRegVcell, &vcell) && read16(dev, kRegSoc, &soc) && read16(dev, kRegCrate, &crate))) return;
    const int mv = (int)(((uint32_t)vcell * 78125u + 500000u) / 1000000u);
    const int soc_x10 = (int)(((uint32_t)soc * 10u + 128u) / 256u);
    const int crate_x10 = ((int)(int16_t)crate * 208 + (crate & 0x8000 ? -50 : 50)) / 100;  // 0.208 %/h -> 0.1 %/h
    s_soc_x10.store(soc_x10, std::memory_order_relaxed);
    s_crate_x10.store(crate_x10, std::memory_order_relaxed);
    s_mv.store(mv, std::memory_order_relaxed);
    if (crate_x10 > kChargeX10) SetExternalPower(true);
    else if (crate_x10 < kDischargeX10) SetExternalPower(false);
}

bool Get(Reading* out)
{
    const int mv = s_mv.load(std::memory_order_relaxed);
    if (mv < 0) return false;
    if (out) {
        int pct = (s_soc_x10.load(std::memory_order_relaxed) + 5) / 10;
        out->mv = mv;
        out->pct = pct > 100 ? 100 : (pct < 0 ? 0 : pct);
    }
    return true;
}

bool GetGauge(GaugeReading* out)
{
    const int mv = s_mv.load(std::memory_order_relaxed);
    if (mv < 0) return false;
    if (out) {
        out->soc_x10 = s_soc_x10.load(std::memory_order_relaxed);
        out->crate_x10 = s_crate_x10.load(std::memory_order_relaxed);
        out->table_pct = PercentFromMv(mv);
    }
    return true;
}

#elif KB_HAS_BATT_SENSE

namespace {
constexpr const char* TAG = "BATT";
constexpr int kSamples = 64;          // averaged per cycle (single reads wander by +-40 counts)
// The task stack is small on purpose (2048 B; the loop itself peaks near 1000 B): a log call from the loop would
// need more than 1 KB more, so the loop does not log. The value is read with the serial "batt" command.
constexpr uint32_t kPeriodMs = 1000;  // pause between cycles (one cycle itself takes ~0.64 s: 1 tick per sample)

std::atomic<int> s_mv{-1};            // -1 = no sample yet
std::atomic<bool> s_started{false};

void sample_task(void*)
{
    adc_unit_t unit;
    adc_channel_t chan;
    if (adc_oneshot_io_to_channel((int)PIN_BAT_ADC, &unit, &chan) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC pin", (int)PIN_BAT_ADC);
        vTaskDelete(nullptr);
        return;
    }
    adc_oneshot_unit_handle_t adc = nullptr;
    adc_oneshot_unit_init_cfg_t ucfg = {};
    ucfg.unit_id = unit;
    if (adc_oneshot_new_unit(&ucfg, &adc) != ESP_OK) {
        ESP_LOGE(TAG, "adc unit init failed");
        vTaskDelete(nullptr);
        return;
    }
    adc_oneshot_chan_cfg_t ccfg = {};
    ccfg.atten = ADC_ATTEN_DB_12;       // divided voltage is ~1.4 V at most
    ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(adc, chan, &ccfg);

    adc_cali_handle_t cali = nullptr;
    adc_cali_curve_fitting_config_t kcfg = {};
    kcfg.unit_id = unit;
    kcfg.chan = chan;
    kcfg.atten = ADC_ATTEN_DB_12;
    kcfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&kcfg, &cali) != ESP_OK) {
        ESP_LOGE(TAG, "ADC calibration unavailable (no eFuse data?): battery sensing disabled");
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        long sum = 0;
        int n = 0;
        for (int i = 0; i < kSamples; i++) {
            int raw = 0;
            if (adc_oneshot_read(adc, chan, &raw) == ESP_OK) { sum += raw; n++; }
            vTaskDelay(1);
        }
        int adc_mv = 0;
        if (n > 0 && adc_cali_raw_to_voltage(cali, (int)(sum / n), &adc_mv) == ESP_OK) {
            const int mv = adc_mv * KB_BAT_DIV_MUL;
            s_mv.store(mv, std::memory_order_relaxed);
        }
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
    }
}
}  // namespace

void Init()
{
    if (s_started.exchange(true)) return;
    xTaskCreate(sample_task, "battery", 2048, nullptr, 2, nullptr);
}

bool Get(Reading* out)
{
    const int mv = s_mv.load(std::memory_order_relaxed);
    if (mv < 0) return false;
    if (out) { out->mv = mv; out->pct = PercentFromMv(mv); }
    return true;
}

void AttachI2cBus(void*) {}
void PollGauge() {}
bool GetGauge(GaugeReading*) { return false; }

#else  // no battery sensing on this board: nothing is created or touched

void Init() {}
bool Get(Reading*) { return false; }
void AttachI2cBus(void*) {}
void PollGauge() {}
bool GetGauge(GaugeReading*) { return false; }

#endif

}  // namespace battery
