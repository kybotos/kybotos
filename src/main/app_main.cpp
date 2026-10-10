// Kybotos: WASM アプリランチャー (Phase 6D で一本化)。
// 旧 MP3 デモモード(Kconfig 分岐)は Phase 6D で削除した。同等機能は
// WASM アプリ側の mp3player.wasm が提供する。
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "power_key.hpp"
#include "display.hpp"
#include "touch.hpp"
#include "audio.hpp"
#include "midi.hpp"
#include "clock_authority.hpp"
#include "seq.hpp"
#include "wasm_runtime.hpp"
#include "hostapi.hpp"
#include "launcher.hpp"
#include "screensaver.hpp"
#include "serial_cmd.hpp"
#include "board_pins.hpp"

static const char* TAG = "APP";

extern "C" void app_main()
{
    // NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 焼き間違いに気づけるよう、最初にボードの名前を出す(Phase 24)
    ESP_LOGI(TAG, "board: %s", CONFIG_KYBOTOS_BOARD_NAME);

#if KB_HAS_POWER_LATCH
    PowerKey::Config cfg;
    cfg.key_pin = PIN_PWR_KEY_IN;
    cfg.latch_pin = PIN_PWR_LATCH;
    cfg.hold_ms = 2000;          // Power off on 2-second long press
    cfg.poll_period_ms = 10;     // Poll every 10 ms
    cfg.use_deepsleep_hold = true;

    static PowerKey pwr{cfg};
    pwr.init();
    pwr.start_task();
#endif

    ESP_LOGI(TAG, "Boot: WASM launcher");
    static Display disp;
    disp.init();
    disp.start_lvgl();
    static Touch touch;
    touch.init(disp.lvgl_get_disp());
    // 起動時のスプラッシュ(Phase 22a)。以降の初期化と SD の準備の間、ロゴを出しておく
    wasmrt::launcher_show_splash();

    // Clock Authority(Phase 11)。I2S 初期化より前にアンカーを初期化しておく
    clockauth::Init();

    // hostapi_audio_* 用のフル初期化(esp-audio-player タスク起動、実測 ~47KB)
    const size_t heap_before_audio =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    audio::Audio_Init();
    ESP_LOGI(TAG, "Audio_Init: free heap %u -> %u (delta %d)",
             (unsigned)heap_before_audio,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (int)(heap_before_audio
                   - heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    // 起動音(Phase 22a 追記)。スプラッシュを出した直後。ミキサタスクが鳴らすのでここは待たない
    audio::Play_Boot_Sound();

    // MIDI OUT の常設初期化(Phase 8a で確認済みの UART1 設定。起動時1回のみ)
    midi::Midi_Init();

    // L0/L1(音楽時間軸)。Phase 11 ステップ 1 では誰からも呼ばれない
    seq::Init();
#ifdef SEQCORE_SELFTEST
    seq::SelfTest();
#endif
    ESP_LOGI(TAG, "heap after seq init: free %u, largest block %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    if (!wasmrt::runtime_init()) {
        ESP_LOGE(TAG, "WASM runtime init failed");
    }

    // power_key 短押し = 戻るキー(Phase 18a)。実行中アプリの任意 export
    // app_key() へ app_tick の切れ目で渡し、アプリが処理しなければ従来どおり
    // 停止する(app_key を持たない既存アプリは即終了のまま)。
    // コールバックは power_key タスク(小スタック)上なので atomic 操作のみ。
    // 消灯中の短押しは「復帰」も兼ねる(要求フラグを立てるだけ。LVGL には触らない)
    // 電源キーの無いボード(KB_HAS_POWER_LATCH 0)では、戻る / ホームはシリアルの key だけ(Phase 24。ボタンの割り当ては 24c)
#if KB_HAS_POWER_LATCH
    pwr.set_on_short_press([](void*) {
        wasmrt::screensaver_request_wake();
        wasmrt::app_request_key_back();
    }, nullptr);

    // power_key 1〜2 秒の長押し = 強制ホーム(Phase 18a)。アプリに聞かずに停止する。
    // 戻るキーを無視する / 画面遷移が壊れて戻れないアプリからの脱出路。
    // (電池運転では 2 秒で電源断が先に発火する)
    pwr.set_on_force_home([](void*) {
        wasmrt::screensaver_request_wake();
        wasmrt::app_request_force_home();
    }, nullptr);
#endif

    // SD 準備+メニュー表示は FATFS 用に十分なスタックを持つタスクで行う
    auto boot_task = [](void*) {
        vTaskDelay(pdMS_TO_TICKS(500)); // SD 安定待ち
        char status[64];
        if (!wasmrt::launcher_prepare_sd(status, sizeof(status))) {
            ESP_LOGE(TAG, "SD prepare failed: %s", status);
        }
#if CONFIG_KYBOTOS_WASM_CYCLE_TEST
        else {
            wasmrt::launcher_run_cycle_test();
        }
#endif
        // 失敗時もメニューは出す(エラー表示付き・空リスト)。スプラッシュの残り時間を待ってから切り替える
        wasmrt::launcher_show(status);
        // シリアルコマンド(回帰自動化用)。ls / run が SD を見るのでこの位置。
        // メニューを出した後に開く(スプラッシュの待ちの間に run が来て、アプリの画面を
        // メニューが上書きしないように。Phase 22a)
        serialcmd::Init();
        vTaskDelete(nullptr);
    };
    xTaskCreate(boot_task, "wasm_boot", 8192, nullptr, 4, nullptr);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
