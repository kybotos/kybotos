// Includes (kept minimal since header pulls most deps)
#include "audio.hpp"
#include "clock_authority.hpp"
#include "boot_sound.h"      // Phase 22a 追記: 起動音
#include "synth_voice.h"     // Phase 23: 内蔵音源のボイス(両ホスト共有)
#include "board_pins.hpp"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include <cstring>
#include <cstdio>
#include <cmath>

namespace audio {

static const char* TAG = "AUDIO/MP3";

// ---- I2S アクセスの排他 (Phase 21) ----
//
// Phase 21 でミキサが**常時**書くようになったため、MP3 の開始・終了で
// audio_player が clk_set_fn(→ reconfig_rate)を呼ぶタイミングと、ミキサの
// i2s_channel_write が重なるようになった。reconfig は disable → 設定 → enable の
// 3 段なので、その途中に別タスクが書き込むとチャネルの状態が割れる。
// **書き込みと再構成をこのミューテックスで直列化する。**(恒久物なので静的確保)
static StaticSemaphore_t s_i2s_mux_buf;
static SemaphoreHandle_t s_i2s_mux;

static void i2s_mux_ensure()
{
    if (!s_i2s_mux) s_i2s_mux = xSemaphoreCreateMutexStatic(&s_i2s_mux_buf);
}
static void i2s_lock()   { if (s_i2s_mux) xSemaphoreTake(s_i2s_mux, portMAX_DELAY); }
static void i2s_unlock() { if (s_i2s_mux) xSemaphoreGive(s_i2s_mux); }

// Clock Authority(Phase 11)のレートマスター。I2S TX の on_sent は
// データ未供給時(無音)もフリーランで発火するので、サンプルカウントは
// 途切れず単調増加する(P10-1)。ISR コンテキストなので加算のみ。
static bool i2s_on_sent_cb(i2s_chan_handle_t, i2s_event_data_t* ev, void*)
{
    clockauth::OnSent(ev ? ev->size : 0);
    return false;
}

// Global player instance for C wrappers
static Mp3Player* g_player = nullptr;
Mp3Player* Mp3Player::s_self = nullptr;

extern "C" {
uint8_t Audio_Volume = Mp3Player::kDefaultVolume; // Phase 21b
bool    Music_Next_Flag = false;
}

Mp3Player::Mp3Player() noexcept
    : pins_(Pins{PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, GPIO_NUM_NC, GPIO_NUM_NC}) {}
Mp3Player::Mp3Player(const Pins& pins) noexcept : pins_(pins) {}

Mp3Player::~Mp3Player() {
    stop();
    if (tx_) { i2s_channel_disable(tx_); i2s_del_channel(tx_); tx_ = nullptr; }
    if (rx_) { i2s_channel_disable(rx_); i2s_del_channel(rx_); rx_ = nullptr; }
}

bool Mp3Player::init(uint32_t sample_rate_hz, uint8_t bits, bool stereo) noexcept {
    if (!ensure_i2s(sample_rate_hz, bits, stereo)) return false;
    ensure_click_task();
    s_self = this;
#if HAVE_ESP_AUDIO_PLAYER
    audio_player_config_t config{};
    config.mute_fn = &Mp3Player::mute_fn;
    config.write_fn = &Mp3Player::write_fn;
    config.clk_set_fn = &Mp3Player::clk_set_fn;
    config.priority = 3;
    config.coreID = tskNO_AFFINITY;
    if (audio_player_new(config) != ESP_OK) {
        ESP_LOGE(TAG, "audio_player_new failed");
        return false;
    }
    if (audio_player_callback_register(&Mp3Player::player_callback, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "callback_register failed");
        return false;
    }
    event_queue_ = xQueueCreate(2, sizeof(audio_player_callback_event_t));
    if (!event_queue_) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return false;
    }
#else
    (void)event_queue_;
    ESP_LOGW(TAG, "esp_audio_player not available; using stubs");
#endif
    return true;
}

bool Mp3Player::ensure_i2s(uint32_t rate_hz, uint8_t bits, bool stereo) noexcept {
    i2s_mux_ensure();
    if (tx_) {
        i2s_data_bit_width_t bw = (bits == 32) ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT;
        i2s_slot_mode_t sm = stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
        return reconfig_rate(rate_hz, (uint32_t)bw, sm);
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, &tx_, &rx_) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed");
        return false;
    }
    i2s_std_config_t std_cfg{};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate_hz);
    std_cfg.slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
        bits == 32 ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT,
        stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
    std_cfg.gpio_cfg.mclk = pins_.mclk;
    std_cfg.gpio_cfg.bclk = pins_.bclk;
    std_cfg.gpio_cfg.ws   = pins_.ws;
    std_cfg.gpio_cfg.dout = pins_.dout;
    std_cfg.gpio_cfg.din  = pins_.din;
    std_cfg.gpio_cfg.invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false };
    if (i2s_channel_init_std_mode(tx_, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed");
        return false;
    }
    // Clock Authority 用の打刻コールバックは enable より前に登録する(P10-1)
    i2s_event_callbacks_t cbs = {};
    cbs.on_sent = i2s_on_sent_cb;
    if (i2s_channel_register_event_callback(tx_, &cbs, nullptr) != ESP_OK) {
        ESP_LOGW(TAG, "i2s_channel_register_event_callback failed");
    }
    clockauth::OnFormatChanged(rate_hz, bits, stereo);
    if (i2s_channel_enable(tx_) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed");
        return false;
    }
    enabled_ = true;
    cur_rate_ = rate_hz; cur_bits_ = bits; cur_stereo_ = stereo;
    return true;
}

bool Mp3Player::init_i2s_only(uint32_t sample_rate_hz, uint8_t bits, bool stereo) noexcept {
    if (!ensure_i2s(sample_rate_hz, bits, stereo)) return false;
    ensure_click_task();
    return true;
}

// ---- ブロックミキサ (Phase 21) ----
//
// Phase 7B-fix から Phase 20 までは「1 音を i2s_write で書き切り、続けて DMA リング
// 1 周ぶんのゼロを書く」方式だった。ゼロ埋めはクリック終端の **DMA アンダーフロー**時に
// auto_clear とプリフェッチが競合して古いディスクリプタが再生される現象(二重クリック)
// への対策である。この方式は 1 音ずつ直列にしか鳴らせないので、内蔵音源(SYNTH ポート)は
// 載せられなかった。
//
// Phase 21 で **常時 1 ブロック(240 フレーム = dma_frame_num)を書き続けるミキサ**に
// 変えた。**常に書いているのでアンダーフロー自体が起きず、ゼロ埋めは構造的に不要**になる
// (7B-fix の根拠がそのまま消える)。Linux ホストは元から pull 型ミキサで二重クリックが
// 起きていなかったことも、この読みの裏づけである(docs/results/phase07.md 7B-fix)。
//
// ボイス(合成の式と定数)は shared/synth_voice.c(Phase 23。それまでは Linux の hostapi_sdl.c と
// 同じコードをここにも持っていた)。ここに残るのはミキサ(発音要求のキュー、240 フレームのブロック、
// 起動音、MP3 との排他、I2S への書き込み)。**synthv_* はミキサタスクからだけ呼ぶ**(別のタスクからの
// リセットは s_synth_reset_req で頼む)。
//
// MP3 とは **排他**: esp_audio_player が同じ I2S へ write_fn から書くので、
// MP3 再生中はミキサを止める(発音要求は捨てる)。docs/results/phase21.md 0-b。

namespace {

constexpr int kMixRate = SYNTHV_RATE;
constexpr int kMixBlock = 240; // I2S_CHANNEL_DEFAULT_CONFIG の dma_frame_num と同じ
constexpr int kRingBlocks = 6; // 同 dma_desc_num。リング 1 周ぶん

// ミキサのバッファはタスクスタックではなく静的に置く(恒久物は静的確保: 6B / 7B-fix)
int32_t s_acc[kMixBlock];
int16_t s_chunk[kMixBlock * 2];

// ミキサ側が使うポート別ゲイン(Mp3Player::set_gain が写す。Phase 21b)
int s_gain_synth = 100;
int s_gain_click = 100;

bool s_mixer_suspended; // ログを状態変化のときだけ出すための記録

// 起動音(Phase 22a 追記)。PCM を 1 本だけ鳴らす(ボイスとは別枠。ボイスを奪わない)。
// 要求は atomic のフラグだけ(呼び出し側をブロックしない)。再生位置はミキサタスクだけが触る
std::atomic<bool> s_boot_req{false};
int s_boot_pos = BOOT_SOUND_FRAMES; // BOOT_SOUND_FRAMES = 鳴っていない
int s_boot_gain = 0;                // マスター音量(0..100)。開始時に固定

// ボイスのリセットの要求(Phase 23)。Synth_Reset は WASM のタスク(アプリの起動 / 破棄)から呼ばれるので、
// ボイスには触らずフラグだけ立て、ミキサタスクが次のブロックの頭で synthv_reset する(最大 1 ブロック = 5.4ms 後)。
// 発音要求のキューはこれまでどおりその場で空にする
std::atomic<bool> s_synth_reset_req{false};

} // namespace

// 発音依頼(ノンブロッキング)。実際の合成はミキサタスクが行う。
bool Mp3Player::play_click() noexcept {
    return play_tone(1000, 30, 100); // v0 既定クリック
}

bool Mp3Player::play_tone(uint16_t freq_hz, uint16_t dur_ms, uint8_t level) noexcept {
    if (!tone_queue_) return false;
    ToneMsg msg{freq_hz, dur_ms, level, 0, 0};
    // 満杯(発音が密に重なった)ときは捨てる
    return xQueueSend(tone_queue_, &msg, 0) == pdTRUE;
}

// 内蔵音源(SYNTH ポート)の発音依頼。契約は shared/hostapi_defs.h
bool Mp3Player::play_drum(uint8_t note, uint8_t velocity) noexcept {
    if (!tone_queue_) return false;
    ToneMsg msg{0, 0, 0, note, velocity};
    return xQueueSend(tone_queue_, &msg, 0) == pdTRUE;
}

void Mp3Player::set_gain(uint8_t mp3, uint8_t synth, uint8_t click) noexcept {
    gain_mp3_.store(mp3);
    gain_synth_.store(synth);
    gain_click_.store(click);
    s_gain_synth = synth;
    s_gain_click = click;
}

void Mp3Player::synth_reset() noexcept {
    // 鳴っているボイスを消す(アプリの起動 / 破棄。hostapi_defs.h の契約)。
    // ボイスはミキサタスクが次のブロックの頭で消す(s_synth_reset_req)
    if (tone_queue_) xQueueReset(tone_queue_);
    s_synth_reset_req.store(true);
}

// タスクスタック等は静的確保(BSS)。ヒープから取ると最大連続ブロックを
// 分断して WASM の linear memory 確保を壊すため(6B の教訓)。
static uint8_t s_click_stack[4096];
static StaticTask_t s_click_tcb;
static uint8_t s_tone_queue_buf[16 * sizeof(Mp3Player::ToneMsg)];
static StaticQueue_t s_tone_queue_cb;

void Mp3Player::ensure_click_task() noexcept {
    if (click_task_) return;
    tone_queue_ = xQueueCreateStatic(16, sizeof(ToneMsg), s_tone_queue_buf,
                                     &s_tone_queue_cb);
    if (!tone_queue_) return;
    auto fn = [](void* arg) { static_cast<Mp3Player*>(arg)->click_task_loop(); };
    click_task_ = xTaskCreateStatic(fn, "mixer", sizeof(s_click_stack), this, 18,
                                    s_click_stack, &s_click_tcb);
    if (!click_task_) {
        ESP_LOGE(TAG, "mixer task create failed");
    }
}

// 鳴っている音を止め、**DMA リング 1 周ぶんの無音を書いてから**戻る。
// `i2s_channel_write` はディスクリプタが空くまで待つので、これが戻った時点で
// 「前の音は鳴り終わり、リングは無音で満たされている」ことが保証される。
//
// MP3 との受け渡しはここを通す:
//   - ミキサ → MP3: 鳴っているドラムが**ぶつ切りにならない**(クラッシュの最中に
//     MP3 が始まると、切断のクリックが「ピッ」として聞こえる。Phase 21 で踏んだ)
//   - MP3 → ミキサ: **再構成の前に**残りを押し出すので、古いサンプルが
//     新しいレートで鳴ることがない
void Mp3Player::flush_silence() noexcept
{
    synthv_reset();                    // ミキサタスクから呼ばれる
    s_synth_reset_req.store(false);    // 済んだので、溜まっていた要求も消す
    s_boot_pos = BOOT_SOUND_FRAMES;
    if (tone_queue_) xQueueReset(tone_queue_);
    if (!tx_ || !enabled_) return;
    memset(s_chunk, 0, sizeof(s_chunk));
    for (int i = 0; i < kRingBlocks + 1; i++) {
        if (!i2s_write(s_chunk, sizeof(s_chunk), 100)) break;
    }
}

// ミキサ本体。**常時 1 ブロックを書き続ける**ので i2s_channel_write が
// このループのペースを作る(タイマは要らない)。
void Mp3Player::click_task_loop() noexcept {
    for (;;) {
        // MP3 が鳴っている間は書かない(同じ I2S を audio_player が使う)。
        // 溜まった発音要求は捨てる(契約どおり)。
        const bool busy = mp3_active_.load();
        if (busy) {
            if (!s_mixer_suspended) {
                s_mixer_suspended = true;
                ESP_LOGI(TAG, "mixer suspended (mp3)");
                flush_silence();      // 鳴っている音を無音まで送り切ってから譲る
                mixer_idle_.store(true); // play_file はこれを待つ
            }
            xQueueReset(tone_queue_);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (s_mixer_suspended) {
            s_mixer_suspended = false;
            mixer_idle_.store(false);
            ESP_LOGI(TAG, "mixer resumed");
            flush_silence();          // **再構成の前に** MP3 の残りを押し出す
        }
        if (!tx_) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        // MP3 が 22.05kHz 等へ変えたままなら 44.1kHz へ戻す。
        // **`!enabled_` も条件に入れる**ので、チャネルが無効になっていれば作り直す
        // (これが無いと一度の失敗で無音のまま固定される。Phase 21 で踏んだ)
        if (!enabled_ || cur_rate_ != kMixRate || cur_bits_ != 16 || !cur_stereo_) {
            if (!ensure_i2s(kMixRate, 16, true)) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        }

        // ブロックの先頭で、頼まれていればボイスを消し(Synth_Reset)、溜まっている発音要求をすべて開始する。
        // **リセットが先**: Synth_Reset はキューをその場で空にするので、キューに残っているのはその後の要求
        if (s_synth_reset_req.exchange(false)) synthv_reset();
        const int vol = volume_.load();
        ToneMsg msg;
        while (xQueueReceive(tone_queue_, &msg, 0) == pdTRUE) {
            if (msg.velocity) synthv_start_drum(msg.note, msg.velocity, vol, s_gain_synth);
            else              synthv_start_tone(msg.freq_hz, msg.dur_ms, msg.level, vol, s_gain_click);
        }
        if (s_boot_req.exchange(false)) {
            s_boot_pos = 0;
            s_boot_gain = vol;
        }

        memset(s_acc, 0, sizeof(s_acc));
        bool any = synthv_render(s_acc, kMixBlock);
        if (s_boot_pos < BOOT_SOUND_FRAMES) {
            const int n = (BOOT_SOUND_FRAMES - s_boot_pos < kMixBlock) ? BOOT_SOUND_FRAMES - s_boot_pos : kMixBlock;
            for (int i = 0; i < n; i++) s_acc[i] += boot_sound_pcm[s_boot_pos + i] * s_boot_gain / 100;
            s_boot_pos += n;
            any = true;
        }
        if (any) {
            for (int i = 0; i < kMixBlock; i++) {
                int32_t v = s_acc[i];
                if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
                s_chunk[i * 2] = (int16_t)v;
                s_chunk[i * 2 + 1] = (int16_t)v;
            }
        } else {
            memset(s_chunk, 0, sizeof(s_chunk));
        }
        i2s_write(s_chunk, sizeof(s_chunk), 50);
    }
}

bool Mp3Player::reconfig_rate(uint32_t rate_hz, uint32_t bits_cfg, i2s_slot_mode_t ch) noexcept {
    if (!tx_) return false;
    bool stereo = (ch == I2S_SLOT_MODE_STEREO);
    uint8_t bits = 16;
    if (bits_cfg == I2S_DATA_BIT_WIDTH_32BIT) bits = 32;
    else if (bits_cfg == I2S_DATA_BIT_WIDTH_24BIT) bits = 32; // use 32-slot for 24bit
    else bits = 16;
    // **enabled_ も条件に入れる**: 設定が同じでもチャネルが無効なら作り直す
    if (cur_rate_ == rate_hz && cur_bits_ == bits && cur_stereo_ == stereo && enabled_) return true;

    i2s_std_config_t std_cfg{};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate_hz);
    std_cfg.slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
        bits == 32 ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT,
        stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
    std_cfg.gpio_cfg.mclk = pins_.mclk;
    std_cfg.gpio_cfg.bclk = pins_.bclk;
    std_cfg.gpio_cfg.ws   = pins_.ws;
    std_cfg.gpio_cfg.dout = pins_.dout;
    std_cfg.gpio_cfg.din  = pins_.din;
    std_cfg.gpio_cfg.invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false };

    i2s_lock();
    // **既に無効なら INVALID_STATE が返るが、それは失敗ではない**
    // (Phase 21 以前はここで return しており、一度失敗するとチャネルが無効のまま
    //  固定されて二度と音が出なくなっていた)
    esp_err_t err = i2s_channel_disable(tx_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        i2s_unlock();
        ESP_LOGW(TAG, "i2s disable failed: %s", esp_err_to_name(err));
        return false;
    }
    enabled_ = false;

    bool ok = true;
    err = i2s_channel_reconfig_std_clock(tx_, &std_cfg.clk_cfg);
    if (err != ESP_OK) { ESP_LOGW(TAG, "reconfig clock: %s", esp_err_to_name(err)); ok = false; }
    if (ok) {
        err = i2s_channel_reconfig_std_slot(tx_, &std_cfg.slot_cfg);
        if (err != ESP_OK) { ESP_LOGW(TAG, "reconfig slot: %s", esp_err_to_name(err)); ok = false; }
    }
    if (ok) {
        // レート切替 = Clock Authority のアンカー張り替え + 換算係数の切替
        // (再構成中は on_sent が止まるが、音楽時間軸は esp_timer 外挿で連続する。
        //  実機は同一水晶なので外挿誤差は実質ゼロ。§3)
        clockauth::OnFormatChanged(rate_hz, bits, stereo);
        cur_rate_ = rate_hz; cur_bits_ = bits; cur_stereo_ = stereo;
    }

    // **DMA に残っている前のフォーマットのデータを無音で押し出す。**
    // `i2s_channel_disable()` はポインタとメッセージキューを戻すだけで
    // **バッファの中身は消さない**(IDF の実装を確認済み)。そのまま有効化すると
    // **前のデータが新しいレートで再生される**ので、22.05kHz モノラルの MP3 の残りが
    // 44.1kHz ステレオで鳴ると 4 倍速の短い高音(「ピッ」)になる。Phase 21 で実際に出た。
    // preload は無効(READY)の間だけ呼べるので、ここが唯一の place になる。
    {
        static const int16_t kSilence[240 * 2] = {};
        size_t loaded = 0;
        size_t guard = 0;
        do {
            loaded = 0;
            if (i2s_channel_preload_data(tx_, kSilence, sizeof(kSilence), &loaded) != ESP_OK) break;
            guard += loaded;
        } while (loaded == sizeof(kSilence) && guard < 64 * 1024);
    }

    // **成否にかかわらず必ず有効化を試みる**(無効のまま抜けない)
    err = i2s_channel_enable(tx_);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        enabled_ = true;
    } else {
        ESP_LOGW(TAG, "i2s enable failed: %s", esp_err_to_name(err));
    }
    i2s_unlock();
    return ok && enabled_;
}

bool Mp3Player::i2s_write(void* data, size_t len, uint32_t timeout_ms, size_t* written) noexcept {
    if (!tx_) return false;
    size_t bw = 0;
    i2s_lock();
    esp_err_t err = i2s_channel_write(tx_, data, len, &bw, timeout_ms);
    i2s_unlock();
    if (written) *written = bw;
    if (err != ESP_OK) {
        // 書けない状態が続くと**無音のまま黙って止まる**ので、必ず見えるようにする
        // (Phase 21 で実際に踏んだ。チャネルが無効のまま放置されていた)
        enabled_ = false; // 次のループで ensure_i2s による再構成を促す
        static uint32_t s_warned;
        if ((s_warned++ % 200) == 0) {
            ESP_LOGW(TAG, "i2s_write failed (%s), will reconfigure", esp_err_to_name(err));
        }
    }
    return err == ESP_OK;
}

#if HAVE_ESP_AUDIO_PLAYER
esp_err_t Mp3Player::write_fn(void* audio_buffer, size_t len, size_t* bytes_written, uint32_t timeout_ms)
{
    if (!s_self) return ESP_FAIL;
    int16_t* samples = static_cast<int16_t*>(audio_buffer);
    size_t sample_count = len / sizeof(int16_t);
    float volume_factor = (float)s_self->volume_.load() / 100.0f
                          * (float)s_self->gain_mp3_.load() / 100.0f;
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t v = (int32_t)std::lround((float)samples[i] * volume_factor);
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        samples[i] = (int16_t)v;
    }
    return s_self->i2s_write(audio_buffer, len, timeout_ms, bytes_written) ? ESP_OK : ESP_FAIL;
}

esp_err_t Mp3Player::clk_set_fn(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    if (!s_self) return ESP_FAIL;
    return s_self->reconfig_rate(rate, bits_cfg, ch) ? ESP_OK : ESP_FAIL;
}
#endif

#if HAVE_ESP_AUDIO_PLAYER
esp_err_t Mp3Player::mute_fn(AUDIO_PLAYER_MUTE_SETTING setting)
{
    ESP_LOGI(TAG, "mute setting %d", (int)setting);
    return ESP_OK;
}

void Mp3Player::player_callback(audio_player_cb_ctx_t* ctx)
{
    if (!s_self) return;
    if (ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE) {
        ESP_LOGI(TAG, "Playback finished");
        Music_Next_Flag = true;
        s_self->finished_.store(true);
        s_self->mp3_active_.store(false); // 自然終了でもミキサへ返す (Phase 21)
        // FILE* is closed by audio_player; do not fclose() here
        s_self->file_ = nullptr;
    }
    if (ctx->audio_event == s_self->expected_event_) {
        xQueueSend(s_self->event_queue_, &(ctx->audio_event), 0);
    }
}
#endif

bool Mp3Player::play_file(const std::string& path) noexcept {
    // Pause current playback
    pause();
    // Do not fclose() here; audio_player thread owns previous FILE*
    if (file_) { file_ = nullptr; }
    file_ = fopen(path.c_str(), "rb");
    if (!file_) {
        ESP_LOGE(TAG, "Failed to open MP3 file: %s", path.c_str());
        return false;
    }
    // Do not perform ID3/scanning here; lower layer handles it
    current_path_ = path;
    finished_.store(false);
    Music_Next_Flag = false;
    // ここから I2S は MP3 のもの (Phase 21)。**ミキサが無音でリングを満たすまで待つ**
    // (最大 100ms)。待たないと、鳴っているドラムが切られて「ピッ」と鳴る。
    mixer_idle_.store(false);
    mp3_active_.store(true);
    for (int i = 0; i < 20 && !mixer_idle_.load(); i++) vTaskDelay(pdMS_TO_TICKS(5));
    
#if HAVE_ESP_AUDIO_PLAYER
    expected_event_ = AUDIO_PLAYER_CALLBACK_EVENT_PLAYING;
    esp_err_t ret = audio_player_play(file_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_player_play failed: %d", (int)ret);
        mp3_active_.store(false);
        fclose(file_); file_ = nullptr;
        return false;
    }
    (void)xQueueReceive(event_queue_, &event_, pdMS_TO_TICKS(300));
    return true;
#else
    ESP_LOGE(TAG, "esp_audio_player not available; cannot play '%s'", path.c_str());
    fclose(file_); file_ = nullptr;
    return false;
#endif
}

void Mp3Player::pause() noexcept {
#if HAVE_ESP_AUDIO_PLAYER
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        expected_event_ = AUDIO_PLAYER_CALLBACK_EVENT_PAUSE;
        if (audio_player_pause() != ESP_OK) return;
        (void)xQueueReceive(event_queue_, &event_, pdMS_TO_TICKS(200));
    }
#endif
}

void Mp3Player::resume() noexcept {
#if HAVE_ESP_AUDIO_PLAYER
    if (audio_player_get_state() != AUDIO_PLAYER_STATE_PLAYING) {
        expected_event_ = AUDIO_PLAYER_CALLBACK_EVENT_PLAYING;
        if (audio_player_resume() != ESP_OK) return;
        (void)xQueueReceive(event_queue_, &event_, pdMS_TO_TICKS(200));
    }
#endif
}

void Mp3Player::stop() noexcept {
    mp3_active_.store(false); // ミキサへ I2S を返す (Phase 21)
    // Best-effort stop: pause + close file
    pause();
#if HAVE_ESP_AUDIO_PLAYER
    (void)audio_player_stop();
#endif
    // FILE is closed by audio_player thread; just clear local pointer
    file_ = nullptr;
}

void Mp3Player::set_volume(uint8_t vol_0_100) noexcept {
    if (vol_0_100 > 100) vol_0_100 = 100;
    volume_.store(vol_0_100);
    Audio_Volume = vol_0_100;
}

bool Mp3Player::is_playing() const noexcept {
#if HAVE_ESP_AUDIO_PLAYER
    return audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING;
#else
    return false;
#endif
}
bool Mp3Player::is_paused()  const noexcept {
#if HAVE_ESP_AUDIO_PLAYER
    return audio_player_get_state() == AUDIO_PLAYER_STATE_PAUSE;
#else
    return false;
#endif
}

// ---- C API wrappers ----

// Amplifier enable pin (boards with an amplifier after I2S, e.g. CrowPanel's NS4168. Phase 24 / 24b).
// The pin is driven to the off level before I2S starts and to the on level once I2S runs, then left on
// (no pops or hiss were noticed with it always on; switching it per sound would risk pops. docs/results/phase24b.md).
static void amp_set(bool on)
{
    gpio_num_t pin = PIN_AMP_EN;  // not const: GPIO_NUM_NC would make the shift below a constant warning
    if (pin == GPIO_NUM_NC) return;
    if (!on) {
        gpio_config_t io{};
        io.mode = GPIO_MODE_OUTPUT;
        io.pin_bit_mask = 1ULL << pin;
        gpio_config(&io);
    }
    gpio_set_level(pin, on ? KB_AMP_EN_ON_LEVEL : !KB_AMP_EN_ON_LEVEL);
    ESP_LOGI(TAG, "amplifier enable pin GPIO%d: %s", (int)pin, on ? "on" : "off");
}

extern "C" void Audio_Init(void) {
    const bool first = !g_player;
    if (first) { amp_set(false); g_player = new Mp3Player(); }
    g_player->init(44100, 16, true);
    if (first) amp_set(true);
}

extern "C" void Audio_Click_Init(void) {
    const bool first = !g_player;
    if (first) { amp_set(false); g_player = new Mp3Player(); }
    if (!g_player->init_i2s_only(44100, 16, true)) {
        ESP_LOGE(TAG, "Audio_Click_Init: I2S init failed");
    }
    if (first) amp_set(true);
}

extern "C" void Play_Click(void) {
    if (g_player) g_player->play_click();
}

extern "C" bool Play_Tone(uint16_t freq_hz, uint16_t dur_ms, uint8_t level) {
    return g_player && g_player->play_tone(freq_hz, dur_ms, level);
}

extern "C" bool Play_Drum(uint8_t note, uint8_t velocity) {
    return g_player && g_player->play_drum(note, velocity);
}

extern "C" void Play_Boot_Sound(void) {
    if (g_player) s_boot_req.store(true); // ミキサタスクが次のブロックの頭で鳴らし始める
}

extern "C" void Synth_Reset(void) {
    if (g_player) g_player->synth_reset();
}

extern "C" void Set_Port_Gain(uint8_t mp3, uint8_t synth, uint8_t click) {
    if (g_player) g_player->set_gain(mp3, synth, click);
}

extern "C" void Play_Music(const char* directory, const char* fileName) {
    if (!g_player) Audio_Init();
    std::string path;
    if (directory && directory[0]) {
        path = directory;
        if (path.size() > 1 && path.back() != '/') path.push_back('/');
        if (fileName) path += fileName;
    } else if (fileName) {
        path = fileName;
    }
    g_player->play_file(path);
}

extern "C" void Music_pause(void)  { if (g_player) g_player->pause(); }
extern "C" void Music_resume(void) { if (g_player) g_player->resume(); }
extern "C" void Music_stop(void)   { if (g_player) g_player->stop(); }
extern "C" bool Music_is_playing(void) {
#if HAVE_ESP_AUDIO_PLAYER
    return audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING;
#else
    return false;
#endif
}
extern "C" void Volume_adjustment(uint8_t Vol) { if (g_player) g_player->set_volume(Vol); Audio_Volume = Vol; }
extern "C" bool Music_is_paused(void) { return g_player && g_player->is_paused(); }
extern "C" bool Music_finished(void)  { return g_player && g_player->finished_flag(); }
extern "C" bool Music_play_path(const char* path) {
    if (!g_player || !path) return false;
    return g_player->play_file(path);
}

} // namespace audio
