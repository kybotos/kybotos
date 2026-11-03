#include "battery.hpp"
#include "board_pins.hpp"

#include <atomic>
#if KB_HAS_BATT_SENSE
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

#if KB_HAS_BATT_SENSE

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

#else  // no battery sensing on this board: nothing is created or touched

void Init() {}
bool Get(Reading*) { return false; }

#endif

}  // namespace battery
