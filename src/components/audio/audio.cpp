// Includes (kept minimal since header pulls most deps)
#include "audio.hpp"
#include "clock_authority.hpp"
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
uint8_t Audio_Volume = 98;
bool    Music_Next_Flag = false;
}

Mp3Player::Mp3Player() noexcept : pins_(Pins{}) {}
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
// 合成はキャッシュレス: サインは再帰振動子(回転行列)、ノイズは xorshift32。
// サンプルごとに libm を呼ばない。音色の式は Linux ホスト(hosts/linux/hostapi_sdl.c)と同一。
//
// MP3 とは **排他**: esp_audio_player が同じ I2S へ write_fn から書くので、
// MP3 再生中はミキサを止める(発音要求は捨てる)。docs/results/phase21.md 0-b。

namespace {

constexpr int kMixRate = 44100;
constexpr int kMixBlock = 240; // I2S_CHANNEL_DEFAULT_CONFIG の dma_frame_num と同じ

enum VoiceKind : uint8_t { VK_IDLE = 0, VK_TONE, VK_KICK, VK_SNARE, VK_HAT, VK_CRASH };

struct Voice {
    VoiceKind kind;
    uint8_t note;
    uint32_t seq;
    int remaining;
    float s, c, cw, sw;   // サイン(再帰振動子)
    float decay, amp;
    float f_cur, f_end, f_k; // ピッチ掃引(Kick)
    uint32_t rng;            // ノイズ
    float n_prev, n_amp, n_decay;
};

Voice s_voices[HOSTAPI_SYNTH_VOICES];
uint32_t s_voice_seq;

// ミキサのバッファはタスクスタックではなく静的に置く(恒久物は静的確保: 6B / 7B-fix)
int32_t s_acc[kMixBlock];
int16_t s_chunk[kMixBlock * 2];

bool s_mixer_suspended; // ログを状態変化のときだけ出すための記録

void voice_set_sine(Voice* v, float freq, int frames, float amp)
{
    const float w = 2.0f * (float)M_PI * freq / kMixRate;
    v->s = 0.0f;
    v->c = 1.0f;
    v->cw = cosf(w);
    v->sw = sinf(w);
    v->amp = amp;
    v->decay = expf(-3.5f / (float)(frames > 0 ? frames : 1));
    v->remaining = frames;
}

// 空きボイス → 同じ note の最も古いもの → 全体で最も古いもの(hostapi_defs.h の契約)
Voice* voice_alloc(uint8_t note)
{
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES; i++) {
        if (s_voices[i].kind == VK_IDLE || s_voices[i].remaining <= 0) return &s_voices[i];
    }
    Voice* best = nullptr;
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES; i++) {
        if (s_voices[i].note == note && (!best || s_voices[i].seq < best->seq)) best = &s_voices[i];
    }
    if (!best) {
        for (int i = 0; i < HOSTAPI_SYNTH_VOICES; i++) {
            if (!best || s_voices[i].seq < best->seq) best = &s_voices[i];
        }
    }
    return best;
}

void voice_start_tone(const Mp3Player::ToneMsg& t, int master_vol)
{
    Voice* v = voice_alloc(0);
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->kind = VK_TONE;
    v->seq = ++s_voice_seq;
    voice_set_sine(v, (float)t.freq_hz, kMixRate * t.dur_ms / 1000,
                   12000.0f * t.level / 100.0f * master_vol / 100.0f);
}

// 基準レベルは **4 音同時 + クリックでクリップしない**ように決めた
// (Linux の WAV で実測。4 音同時のピーク 19,839 / -4.3dBFS)
void voice_start_drum(uint8_t note, uint8_t velocity, int master_vol)
{
    const float g = (float)velocity / 127.0f * (float)master_vol / 100.0f;
    Voice* v = voice_alloc(note);
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->seq = ++s_voice_seq;
    v->note = note;
    v->rng = 0x9E3779B9u ^ (uint32_t)note ^ (s_voice_seq << 8);
    switch (note) {
    case HOSTAPI_SYNTH_NOTE_KICK:
        v->kind = VK_KICK;
        voice_set_sine(v, 110.0f, kMixRate * 180 / 1000, 7000.0f * g);
        v->f_cur = 110.0f;
        v->f_end = 45.0f;
        v->f_k = expf(-1.0f / ((float)kMixRate * 0.040f));
        break;
    case HOSTAPI_SYNTH_NOTE_SNARE:
        v->kind = VK_SNARE;
        voice_set_sine(v, 190.0f, kMixRate * 140 / 1000, 2800.0f * g);
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-4.5f / (float)(kMixRate * 140 / 1000));
        break;
    case HOSTAPI_SYNTH_NOTE_CHH:
        v->kind = VK_HAT;
        v->remaining = kMixRate * 45 / 1000;
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-5.0f / (float)v->remaining);
        break;
    case HOSTAPI_SYNTH_NOTE_CRASH:
        v->kind = VK_CRASH;
        v->remaining = kMixRate * 800 / 1000;
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-4.0f / (float)v->remaining);
        break;
    default:
        v->kind = VK_IDLE; // 未知の note は何もしない(ログも出さない)
        v->remaining = 0;
        break;
    }
}

inline float voice_noise(Voice* v)
{
    v->rng ^= v->rng << 13;
    v->rng ^= v->rng >> 17;
    v->rng ^= v->rng << 5;
    return (float)((int32_t)v->rng) * (1.0f / 2147483648.0f);
}

// n フレームを acc にミックスする。**サンプル再生に差し替えるときはこの関数だけ**
void voice_render(Voice* v, int32_t* acc, int n)
{
    if (v->remaining <= 0) { v->kind = VK_IDLE; return; }
    if (n > v->remaining) n = v->remaining;

    if (v->kind == VK_KICK) {
        // ピッチ掃引はブロック単位で係数を作り直す(サンプルごとに cosf を呼ばない)
        v->f_cur = v->f_end + (v->f_cur - v->f_end) * powf(v->f_k, (float)n);
        const float w = 2.0f * (float)M_PI * v->f_cur / kMixRate;
        v->cw = cosf(w);
        v->sw = sinf(w);
    }

    for (int i = 0; i < n; i++) {
        float out = 0.0f;
        switch (v->kind) {
        case VK_TONE:
        case VK_KICK:
        case VK_SNARE: {
            const float s2 = v->s * v->cw + v->c * v->sw;
            v->c = v->c * v->cw - v->s * v->sw;
            v->s = s2;
            v->amp *= v->decay;
            out = v->amp * v->s;
            if (v->kind == VK_SNARE) {
                v->n_amp *= v->n_decay;
                out += v->n_amp * voice_noise(v);
            }
            break;
        }
        case VK_HAT:
        case VK_CRASH: {
            const float x = voice_noise(v);
            const float hp = x - v->n_prev; // 1 次ハイパス(差分)
            v->n_prev = x;
            v->n_amp *= v->n_decay;
            out = v->n_amp * hp;
            break;
        }
        default:
            break;
        }
        acc[i] += (int32_t)out;
    }
    v->remaining -= n;
    if (v->remaining <= 0) v->kind = VK_IDLE;
}

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

void Mp3Player::synth_reset() noexcept {
    // 鳴っているボイスを消す(transport_stop / アプリ破棄。hostapi_defs.h の契約)
    if (tone_queue_) xQueueReset(tone_queue_);
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES; i++) s_voices[i].kind = VK_IDLE;
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

// ミキサ本体。**常時 1 ブロックを書き続ける**ので i2s_channel_write が
// このループのペースを作る(タイマは要らない)。
void Mp3Player::click_task_loop() noexcept {
    for (;;) {
        // MP3 が鳴っている間は書かない(同じ I2S を audio_player が使う)。
        // 溜まった発音要求は捨てる(契約どおり)。
        const bool busy = mp3_active_.load();
        if (busy != s_mixer_suspended) {
            s_mixer_suspended = busy;
            ESP_LOGI(TAG, "mixer %s", busy ? "suspended (mp3)" : "resumed");
        }
        if (busy) {
            xQueueReset(tone_queue_);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!tx_) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        // MP3 が 22.05kHz 等へ変えたままなら 44.1kHz へ戻す。
        // **`!enabled_` も条件に入れる**ので、チャネルが無効になっていれば作り直す
        // (これが無いと一度の失敗で無音のまま固定される。Phase 21 で踏んだ)
        if (!enabled_ || cur_rate_ != kMixRate || cur_bits_ != 16 || !cur_stereo_) {
            if (!ensure_i2s(kMixRate, 16, true)) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        }

        // ブロックの先頭で、溜まっている発音要求をすべて開始する
        const int vol = volume_.load();
        ToneMsg msg;
        while (xQueueReceive(tone_queue_, &msg, 0) == pdTRUE) {
            if (msg.velocity) voice_start_drum(msg.note, msg.velocity, vol);
            else              voice_start_tone(msg, vol);
        }

        memset(s_acc, 0, sizeof(s_acc));
        bool any = false;
        for (int v = 0; v < HOSTAPI_SYNTH_VOICES; v++) {
            if (s_voices[v].kind != VK_IDLE) { voice_render(&s_voices[v], s_acc, kMixBlock); any = true; }
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
    float volume_factor = (float)s_self->volume_.load() / 100.0f;
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
    mp3_active_.store(true); // ここから I2S は MP3 のもの (Phase 21)
    
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

extern "C" void Audio_Init(void) {
    if (!g_player) g_player = new Mp3Player();
    g_player->init(44100, 16, true);
}

extern "C" void Audio_Click_Init(void) {
    if (!g_player) g_player = new Mp3Player();
    if (!g_player->init_i2s_only(44100, 16, true)) {
        ESP_LOGE(TAG, "Audio_Click_Init: I2S init failed");
    }
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

extern "C" void Synth_Reset(void) {
    if (g_player) g_player->synth_reset();
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
