/*
 * ホスト API v0 実装(Linux / SDL2 バックエンド)。
 *
 * 実機側 (src/components/wasm_runtime/hostapi.cpp) と同じ retained モデル:
 * (x,y) をキーに text / rect のスロットを保持し、同一座標への再描画は置き換え。
 * 毎 tick、host_sdl_render() が全スロットを描き直す(rect 群→text 群の順)。
 *
 * テキストは font8x8 (public domain) の 8x8 ビットマップで描画。
 * クリック音は実機と同じ 1kHz 減衰サイン 30ms を SDL のキューへ書く。
 */
#include "hostapi_sdl.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef HAVE_SDL_TTF
#include <SDL_ttf.h>
#endif

#ifdef HAVE_SDL_MIXER
#include <SDL_mixer.h>
#endif

#include "font8x8_basic.h"
#include "hostapi_defs.h"
#include "master_ui.h"   /* Phase 21b: マスター設定(両ホスト共有)*/
#include "hostapi_midi.h"
#include "hostapi_seq.h"

/* 実機と同じランドスケープ 320x240 */
#define SCREEN_W 320
#define SCREEN_H 240
#define WINDOW_SCALE 2

#define MAX_TEXT_SLOTS 80 /* Phase 19a: 16 → 32、Phase 21d: 32 → 80(実機と同値) */
#define MAX_RECT_SLOTS 80 /* Phase 19a: 16 → 24、Phase 21d: 24 → 48、Phase 21e: 48 → 80(実機と同値) */
#define MAX_TEXT_LEN 63
#define EVENT_QUEUE_DEPTH 16

typedef struct {
    bool used;
    int32_t x, y;
    uint32_t rgb888; /* Phase 18b: 文字色 */
    char text[MAX_TEXT_LEN + 1];
} TextSlot;

typedef struct {
    bool used;
    int32_t x, y, w, h;
    uint32_t rgb888;
} RectSlot;

static SDL_Window* s_window;
static SDL_Renderer* s_renderer;
static SDL_AudioDeviceID s_audio;
static uint32_t s_start_ms;

/* ---- クリック音のコールバックミキサ (Phase 7A) ----
 * v0 の SDL_QueueAudio(push 型)では発音タイミングがポーリング周期に縛られる
 * ため、クリック用デバイスをコールバック(pull)型に変更。再生済みフレーム数を
 * 音声クロックとして扱い、予約時刻(now_ms 時基)を目標サンプル位置に換算して
 * バッファ内オフセットでサンプル精度の発音を行う。
 * 共有状態は SDL_LockAudioDevice(コールバックは暗黙にロック保持)で保護。 */
#define CLICK_RATE 44100

/* トーンパレット (Phase 7C)。アプリセッション状態(audio_reset で初期化) */
typedef struct {
    bool defined;
    uint16_t freq_hz;
    uint16_t dur_ms;
    uint8_t level;
} ToneDef;
static ToneDef s_tones[HOSTAPI_TONE_SLOTS];
static const ToneDef kDefaultClick = {true, 1000, 30, 100};

/* ---- ポリフォニックミキサ (Phase 21) ----
 * 単声だった Voice を HOSTAPI_SYNTH_VOICES 本にし、CLICK のトーンと内蔵音源
 * (SYNTH ポート)の両方をここに載せる。合成はキャッシュレス(サインは再帰振動子、
 * ノイズは xorshift32)で、サンプルごとに libm を呼ばない。
 * 実機側(src/components/audio)も同じ構造にしてあり、音色の式は両ホストで同一。 */

/* ボイスの種類。SAMPLE への差し替えは voice_render の中身だけで済む(0-d) */
typedef enum {
    VK_IDLE = 0,
    VK_TONE,   /* CLICK ポート / tone_play: 減衰サイン */
    VK_KICK,   /* ピッチ掃引する減衰サイン */
    VK_SNARE,  /* ノイズ + 減衰サイン */
    VK_HAT,    /* ハイパスしたノイズ(短い) */
    VK_CRASH,  /* ハイパスしたノイズ(長い) */
    VK_WOOD,   /* 減衰サイン + すぐ消えるノイズ(メトロノーム。Phase 21c) */
} VoiceKind;

typedef struct {
    VoiceKind kind;
    uint8_t note;        /* SYNTH のとき。ボイス奪取の判定に使う */
    uint32_t seq;        /* 発音順。最も古いものを奪うため */
    int remaining;       /* 残りフレーム(0 = idle) */
    /* サイン(再帰振動子) */
    float s, c, cw, sw;
    float decay, amp;
    /* ピッチ掃引(Kick): ブロックごとに係数を作り直す */
    float f_cur, f_end, f_k;
    /* ノイズ */
    uint32_t rng;
    float n_prev;        /* 1 次ハイパス(差分)用 */
    float n_amp, n_decay;
} Voice;

static Voice s_voices[HOSTAPI_SYNTH_VOICES];
static uint32_t s_voice_seq;

static uint64_t s_audio_samples;   /* 再生済みフレーム数(音声クロック) */
/* マスター音量の既定値(Phase 21b)。**実機の Mp3Player::kDefaultVolume と同じ値にすること**。
 * 98 は HW のつまみを最大にすると大きすぎたため 50 にした(暫定。実機の試聴で決める) */
#define DEFAULT_MASTER_VOL MASTERUI_DEF_MASTER
static int s_master_vol = DEFAULT_MASTER_VOL;
/* ポート単位のゲイン(Phase 21b 追記)。**実効音量 = マスター × チャンネル**。
 * master_ui が MUTE を畳み込んだ実効値(MUTE 中は 0)を入れる(Phase 21c) */
static int s_gain_mp3 = MASTERUI_DEF_MP3;
static int s_gain_synth = MASTERUI_DEF_SYNTH;
static int s_gain_click = MASTERUI_DEF_CLICK;

/* 発音要求のキュー(ロック下で積み、コールバックが取り出す)。
 * 単一の s_click_asap では、1 コールバックの間に来た複数の発音を落としてしまう */
typedef struct { uint8_t is_synth; uint8_t note, velocity; ToneDef tone; } Req;
#define REQ_MAX 32
static Req s_reqs[REQ_MAX];
static int s_req_n;

static void req_push(const Req* r)
{
    if (s_req_n < REQ_MAX) s_reqs[s_req_n++] = *r;
}

static void voice_set_sine(Voice* v, float freq, int frames, float amp)
{
    const float w = 2.0f * (float)M_PI * freq / CLICK_RATE;
    v->s = 0.0f;
    v->c = 1.0f;
    v->cw = cosf(w);
    v->sw = sinf(w);
    v->amp = amp;
    v->decay = expf(-3.5f / (float)(frames > 0 ? frames : 1));
    v->remaining = frames;
}

/* 空きボイスを取る。無ければ「同じ note の最も古いもの」→「全体で最も古いもの」を奪う */
static Voice* voice_alloc(uint8_t note)
{
    Voice* best = NULL;
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES; i++) {
        if (s_voices[i].kind == VK_IDLE || s_voices[i].remaining <= 0) return &s_voices[i];
    }
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

/* CLICK ポート / tone_play。マスター音量は発音時に焼き込む(従来と同じ) */
static void voice_start_tone(const ToneDef* t)
{
    Voice* v = voice_alloc(0);
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->kind = VK_TONE;
    v->seq = ++s_voice_seq;
    voice_set_sine(v, (float)t->freq_hz, CLICK_RATE * t->dur_ms / 1000,
                   12000.0f * t->level / 100.0f * s_master_vol / 100.0f
                       * s_gain_click / 100.0f);
}

/* SYNTH ポート。4 音とも「0-d の式」で作る。未知の note は何もしない。
 * 基準レベルは **4 音同時 + クリックでクリップしない**ように決めた(0-f)。
 * 実測(Linux の WAV): 4 音同時のピークは 8000 系で 24,781(-2.4dBFS)、
 * クリック(12,000)が重なると振り切れるので **0.7 倍**にしてある。 */
static void voice_start_drum(uint8_t note, uint8_t velocity)
{
    const float g = (float)velocity / 127.0f * (float)s_master_vol / 100.0f
                    * (float)s_gain_synth / 100.0f;
    Voice* v = voice_alloc(note);
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->seq = ++s_voice_seq;
    v->note = note;
    v->rng = 0x9E3779B9u ^ (uint32_t)note ^ (s_voice_seq << 8);
    switch (note) {
    case HOSTAPI_SYNTH_NOTE_KICK:
        v->kind = VK_KICK;
        voice_set_sine(v, 110.0f, CLICK_RATE * 180 / 1000, 7000.0f * g);
        v->f_cur = 110.0f;
        v->f_end = 45.0f;
        v->f_k = expf(-1.0f / ((float)CLICK_RATE * 0.040f)); /* 掃引 40ms */
        break;
    case HOSTAPI_SYNTH_NOTE_SNARE:
        v->kind = VK_SNARE;
        voice_set_sine(v, 190.0f, CLICK_RATE * 140 / 1000, 2800.0f * g);
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-4.5f / (float)(CLICK_RATE * 140 / 1000));
        break;
    case HOSTAPI_SYNTH_NOTE_CHH:
        v->kind = VK_HAT;
        v->remaining = CLICK_RATE * 45 / 1000;
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-5.0f / (float)v->remaining);
        break;
    case HOSTAPI_SYNTH_NOTE_CRASH:
        v->kind = VK_CRASH;
        v->remaining = CLICK_RATE * 800 / 1000;
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-4.0f / (float)v->remaining);
        break;
    case HOSTAPI_SYNTH_NOTE_METRO_CLICK:
    case HOSTAPI_SYNTH_NOTE_METRO_BELL: {
        /* メトロノーム(Phase 21c)。ウッドブロック系の短い共鳴音: 減衰サイン + 4ms で消えるノイズ
         * (叩いた瞬間のアタック)。**Bell は Click より高く長く鳴る**(明るく目立つ)。
         * 中〜高域に置くのは内蔵スピーカーが低域を出さないため(所感 D-1)。
         * **式と定数は実機の audio.cpp と同じにすること** */
        const bool bell = (note == HOSTAPI_SYNTH_NOTE_METRO_BELL);
        v->kind = VK_WOOD;
        voice_set_sine(v, bell ? 2000.0f : 1200.0f, CLICK_RATE * (bell ? 150 : 60) / 1000, 8000.0f * g);
        v->n_amp = 3000.0f * g;
        v->n_decay = expf(-5.0f / ((float)CLICK_RATE * 0.004f));
        break;
    }
    default:
        v->kind = VK_IDLE; /* 未知の note は何もしない(ログも出さない) */
        v->remaining = 0;
        break;
    }
}

static inline float voice_noise(Voice* v)
{
    /* xorshift32。サンプルごとに libm を呼ばない */
    v->rng ^= v->rng << 13;
    v->rng ^= v->rng >> 17;
    v->rng ^= v->rng << 5;
    return (float)((int32_t)v->rng) * (1.0f / 2147483648.0f);
}

/* n フレームを acc にミックスする。**サンプル再生に差し替えるときはこの関数だけ** */
static void voice_render(Voice* v, int32_t* acc, int n)
{
    if (v->remaining <= 0) { v->kind = VK_IDLE; return; }
    if (n > v->remaining) n = v->remaining;

    if (v->kind == VK_KICK) {
        /* ピッチ掃引はブロック単位で係数を作り直す(サンプルごとに cosf を呼ばない) */
        v->f_cur = v->f_end + (v->f_cur - v->f_end) * powf(v->f_k, (float)n);
        const float w = 2.0f * (float)M_PI * v->f_cur / CLICK_RATE;
        v->cw = cosf(w);
        v->sw = sinf(w);
    }

    for (int i = 0; i < n; i++) {
        float out = 0.0f;
        switch (v->kind) {
        case VK_TONE:
        case VK_KICK:
        case VK_SNARE:
        case VK_WOOD: {
            const float s2 = v->s * v->cw + v->c * v->sw;
            v->c = v->c * v->cw - v->s * v->sw;
            v->s = s2;
            v->amp *= v->decay;
            out = v->amp * v->s;
            if (v->kind == VK_SNARE || v->kind == VK_WOOD) {
                v->n_amp *= v->n_decay;
                out += v->n_amp * voice_noise(v);
            }
            break;
        }
        case VK_HAT:
        case VK_CRASH: {
            const float x = voice_noise(v);
            const float hp = x - v->n_prev; /* 1 次ハイパス(差分) */
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

/* ジッタ統計: 発音開始位置(音声クロック)と壁時計を N 発ごとに集計 */
#define CLICK_STAT_N 100
static uint64_t s_fire_sample[CLICK_STAT_N];
static uint32_t s_fire_wall[CLICK_STAT_N];
static int s_fire_count;

static void click_record_fire(uint64_t sample)
{
    if (s_fire_count < CLICK_STAT_N) {
        s_fire_sample[s_fire_count] = sample;
        s_fire_wall[s_fire_count] = SDL_GetTicks() - s_start_ms;
        s_fire_count++;
    }
    if (s_fire_count == CLICK_STAT_N) {
        double smin = 1e18, smax = 0, ssum = 0;
        uint32_t wmin = UINT32_MAX, wmax = 0;
        uint64_t wsum = 0;
        for (int i = 1; i < CLICK_STAT_N; i++) {
            double ds = (double)(s_fire_sample[i] - s_fire_sample[i - 1]) * 1000.0 / CLICK_RATE;
            uint32_t dw = s_fire_wall[i] - s_fire_wall[i - 1];
            if (ds < smin) smin = ds;
            if (ds > smax) smax = ds;
            ssum += ds;
            if (dw < wmin) wmin = dw;
            if (dw > wmax) wmax = dw;
            wsum += dw;
        }
        fprintf(stderr,
                "click jitter (sample clock): min=%.3f avg=%.3f max=%.3f ms (n=%d)\n",
                smin, ssum / (CLICK_STAT_N - 1), smax, CLICK_STAT_N - 1);
        fprintf(stderr,
                "click jitter (wall clock)  : min=%u avg=%.1f max=%u ms (n=%d)\n",
                wmin, (double)wsum / (CLICK_STAT_N - 1), wmax, CLICK_STAT_N - 1);
        s_fire_count = 0;
    }
}

/* ---- 検証用の WAV 書き出し (Phase 21) ----
 * 環境変数 MIDIBOX_WAV_OUT が設定されているときだけ、ミキサの出力を追記する。
 * 既定 off なので既存の挙動は変わらない。4 音同時・ピーク・オンセット間隔を
 * 耳ではなく数値で確かめるための口(docs/results/phase21.md 0-g)。 */
static FILE* s_wav;
static uint32_t s_wav_frames;

static void wav_put32(FILE* f, uint32_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f); }
static void wav_put16(FILE* f, uint16_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }

static void wav_open(void)
{
    const char* path = getenv("MIDIBOX_WAV_OUT");
    if (!path || !*path) return;
    s_wav = fopen(path, "wb");
    if (!s_wav) { fprintf(stderr, "MIDIBOX_WAV_OUT: cannot open %s\n", path); return; }
    /* ヘッダは閉じるときに書き直す */
    fwrite("RIFF????WAVEfmt ", 1, 16, s_wav);
    wav_put32(s_wav, 16);
    wav_put16(s_wav, 1);                 /* PCM */
    wav_put16(s_wav, 2);                 /* stereo */
    wav_put32(s_wav, CLICK_RATE);
    wav_put32(s_wav, CLICK_RATE * 4);
    wav_put16(s_wav, 4);
    wav_put16(s_wav, 16);
    fwrite("data????", 1, 8, s_wav);
    fprintf(stderr, "wav: writing to %s\n", path);
}

static void wav_close(void)
{
    if (!s_wav) return;
    const uint32_t bytes = s_wav_frames * 4;
    fseek(s_wav, 4, SEEK_SET);  wav_put32(s_wav, 36 + bytes);
    fseek(s_wav, 40, SEEK_SET); wav_put32(s_wav, bytes);
    fclose(s_wav);
    fprintf(stderr, "wav: %u frames\n", s_wav_frames);
    s_wav = NULL;
    s_wav_frames = 0;
}

/* SDL オーディオスレッドから呼ばれる。stream は 16bit ステレオ。
 * **240 フレームのブロックに切って処理する**(実機のミキサと同じ粒度。
 * 発音は「次に書くブロックの先頭」に丸まる = docs/results/phase21.md 0-c の案 A)。 */
#define MIX_BLOCK 240

static void audio_callback(void* userdata, Uint8* stream, int len)
{
    (void)userdata;
    int16_t* out = (int16_t*)stream;
    const int frames = len / 4;
    int32_t acc[MIX_BLOCK];

    for (int off = 0; off < frames; off += MIX_BLOCK) {
        int n = frames - off;
        if (n > MIX_BLOCK) n = MIX_BLOCK;

        /* ブロックの先頭で、溜まっている発音要求をすべて開始する */
        if (s_req_n > 0) {
            for (int i = 0; i < s_req_n; i++) {
                if (s_reqs[i].is_synth) voice_start_drum(s_reqs[i].note, s_reqs[i].velocity);
                else                    voice_start_tone(&s_reqs[i].tone);
            }
            s_req_n = 0;
            click_record_fire(s_audio_samples + (uint64_t)off);
        }

        memset(acc, 0, sizeof(int32_t) * (size_t)n);
        for (int v = 0; v < HOSTAPI_SYNTH_VOICES; v++) {
            if (s_voices[v].kind != VK_IDLE) voice_render(&s_voices[v], acc, n);
        }
        for (int i = 0; i < n; i++) {
            int32_t v = acc[i];
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
            out[(off + i) * 2] = (int16_t)v;
            out[(off + i) * 2 + 1] = (int16_t)v;
        }
    }

    if (s_wav) {
        fwrite(out, 4, (size_t)frames, s_wav);
        s_wav_frames += (uint32_t)frames;
    }

    s_audio_samples += (uint64_t)frames;
    /* Clock Authority(Phase 11)のレートマスター。実機の I2S on_sent と同型に、
     * 再生済みフレーム数を渡す(hostapi_seq.c が受け取る)。 */
    host_seq_on_audio((uint32_t)frames);
}

#ifdef HAVE_SDL_TTF
/* 実機(LVGL Montserrat 14, アンチエイリアス)に見た目を近づけるため、
 * TTF フォントを WINDOW_SCALE 倍のピクセルサイズでラスタライズし、
 * 論理座標では 1/WINDOW_SCALE で貼る(ウィンドウ実ピクセルで等倍=最良の AA)。
 * フォントが無い環境では font8x8 にフォールバック。 */
static TTF_Font* s_font;
#define TTF_POINT_SIZE 13

static void try_open_font(void)
{
    const char* candidates[] = {
        getenv("MIDIBOX_FONT"), /* 環境変数で差し替え可 */
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s (falling back to font8x8)\n",
                TTF_GetError());
        return;
    }
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!candidates[i]) continue;
        s_font = TTF_OpenFont(candidates[i], TTF_POINT_SIZE * WINDOW_SCALE);
        if (s_font) {
            printf("font: %s\n", candidates[i]);
            return;
        }
    }
    fprintf(stderr, "no TTF font found (falling back to font8x8)\n");
}
#endif

static TextSlot s_texts[MAX_TEXT_SLOTS];
static RectSlot s_rects[MAX_RECT_SLOTS];

/* ---- オーディオ (Phase 6B) ----
 * MP3 再生は SDL_mixer(クリック音の SDL_QueueAudio 経路とは独立のデバイス。
 * OS 側ミキサで混ざる)。状態は実機と同じ「ホスト宣言 + 自然終了の取り込み」。 */
#define MUSIC_ROOT "./sdcard/music"

static int s_audio_state = 0; /* HOSTAPI_AUDIO_* */
#ifdef HAVE_SDL_MIXER
static Mix_Music* s_music;
static volatile int s_music_finished;
static bool s_mixer_ready;

/* SDL_mixer の音楽スレッドから呼ばれる。フラグを立てるだけ */
static void music_finished_hook(void)
{
    s_music_finished = 1;
}
#endif

static void audio_refresh_finished(void)
{
#ifdef HAVE_SDL_MIXER
    if (s_audio_state == HOSTAPI_AUDIO_PLAYING && s_music_finished) {
        s_audio_state = HOSTAPI_AUDIO_FINISHED;
    }
#endif
}

void host_sdl_audio_reset(void)
{
#ifdef HAVE_SDL_MIXER
    if (s_mixer_ready) {
        Mix_HaltMusic();
        if (s_music) {
            Mix_FreeMusic(s_music);
            s_music = NULL;
        }
    }
#endif
    s_audio_state = HOSTAPI_AUDIO_STOPPED;

    /* トーン発音状態・トーンパレットもリセット(Phase 7A/7C 契約)。
     * **マスター音量はここでは触らない(Phase 21b)。** 装置の設定としてホストが持ち、
     * アプリを切り替えても持続する(契約は shared/hostapi_defs.h の audio)。 */
    if (s_audio) {
        SDL_LockAudioDevice(s_audio);
        s_req_n = 0;
        memset(s_voices, 0, sizeof(s_voices));   /* 鳴っているボイスを消す */
        s_voice_seq = 0;
        s_fire_count = 0;
        for (int i = 0; i < HOSTAPI_TONE_SLOTS; i++) s_tones[i] = (ToneDef){0};
        s_tones[0] = kDefaultClick; /* slot 0 = v0 互換の既定クリック */
        SDL_UnlockAudioDevice(s_audio);
    }
    host_midi_reset(); /* MIDI Clock 生成も必ず停止する (Phase 8b 契約) */
    host_seq_reset();  /* L0/L1 も初期状態へ (Phase 11) */
}

/* ミュージックルート相対パスの検証(実機側 hostapi.cpp と同じ規則) */
static bool audio_path_ok(const char* path, uint32_t len)
{
    if (len == 0 || len > 64) return false;
    if (path[0] == '/') return false;
    for (uint32_t i = 0; i + 1 < len; i++) {
        if (path[i] == '.' && path[i + 1] == '.') return false;
    }
    return true;
}

int32_t native_hostapi_audio_play(wasm_exec_env_t exec_env, const char* path, uint32_t len)
{
    (void)exec_env;
    if (!audio_path_ok(path, len)) {
        fprintf(stderr, "audio_play: rejected path\n");
        s_audio_state = HOSTAPI_AUDIO_ERROR;
        return -1;
    }
#ifdef HAVE_SDL_MIXER
    if (!s_mixer_ready) {
        s_audio_state = HOSTAPI_AUDIO_ERROR;
        return -1;
    }
    char full[256];
    snprintf(full, sizeof(full), "%s/%.*s", MUSIC_ROOT, (int)len, path);

    Mix_HaltMusic();
    if (s_music) {
        Mix_FreeMusic(s_music);
        s_music = NULL;
    }
    s_music = Mix_LoadMUS(full);
    if (!s_music) {
        fprintf(stderr, "audio_play: %s: %s\n", full, Mix_GetError());
        s_audio_state = HOSTAPI_AUDIO_ERROR;
        return -1;
    }
    s_music_finished = 0;
    if (Mix_PlayMusic(s_music, 1) != 0) {
        fprintf(stderr, "audio_play: %s\n", Mix_GetError());
        Mix_FreeMusic(s_music);
        s_music = NULL;
        s_audio_state = HOSTAPI_AUDIO_ERROR;
        return -1;
    }
    printf("audio_play: %s\n", full);
    s_audio_state = HOSTAPI_AUDIO_PLAYING;
    return 0;
#else
    fprintf(stderr, "audio_play: built without SDL_mixer\n");
    s_audio_state = HOSTAPI_AUDIO_ERROR;
    return -1;
#endif
}

int32_t native_hostapi_audio_ctrl(wasm_exec_env_t exec_env, int32_t cmd)
{
    (void)exec_env;
    audio_refresh_finished();
    switch (cmd) {
    case HOSTAPI_AUDIO_CMD_PAUSE:
        if (s_audio_state != HOSTAPI_AUDIO_PLAYING) return -1;
#ifdef HAVE_SDL_MIXER
        Mix_PauseMusic();
#endif
        s_audio_state = HOSTAPI_AUDIO_PAUSED;
        return 0;
    case HOSTAPI_AUDIO_CMD_RESUME:
        if (s_audio_state != HOSTAPI_AUDIO_PAUSED) return -1;
#ifdef HAVE_SDL_MIXER
        Mix_ResumeMusic();
#endif
        s_audio_state = HOSTAPI_AUDIO_PLAYING;
        return 0;
    case HOSTAPI_AUDIO_CMD_STOP:
#ifdef HAVE_SDL_MIXER
        if (s_mixer_ready) Mix_HaltMusic();
#endif
        s_audio_state = HOSTAPI_AUDIO_STOPPED;
        return 0;
    default:
        return -1;
    }
}

/* ---- マスター設定のフック(Phase 21b)---- */
static void apply_mp3_volume(void)
{
#ifdef HAVE_SDL_MIXER
    if (s_mixer_ready) {
        const int eff = s_master_vol * s_gain_mp3 / 100;
        Mix_VolumeMusic(eff * MIX_MAX_VOLUME / 100);
    }
#endif
}

static void masterui_set_level_cb(masterui_item_t item, int v)
{
    if (s_audio) SDL_LockAudioDevice(s_audio);
    switch (item) {
    case MASTERUI_MASTER: s_master_vol = v; break;
    case MASTERUI_MP3:    s_gain_mp3 = v;   break;
    case MASTERUI_SYNTH:  s_gain_synth = v; break;
    case MASTERUI_CLICK:  s_gain_click = v; break;
    default: break;
    }
    if (s_audio) SDL_UnlockAudioDevice(s_audio);
    apply_mp3_volume();
}
static uint32_t masterui_now_cb(void) { return SDL_GetTicks() - s_start_ms; }
static const masterui_hooks_t k_masterui_hooks = { masterui_set_level_cb, masterui_now_cb };

void host_sdl_masterui_init(void)
{
    masterui_init(&k_masterui_hooks);
}

void native_hostapi_audio_set_volume(wasm_exec_env_t exec_env, int32_t v)
{
    (void)exec_env;
    /* マスター音量はホストの設定(Phase 21b)。**master_ui を通す**(Phase 21c):
     * 直接 s_master_vol を書くと Master の MUTE を素通りし、オーバーレイの表示ともずれる。
     * クランプとゲインの反映は masterui_set_level → masterui_set_level_cb が行う */
    masterui_set_level(MASTERUI_MASTER, v);
}

int32_t native_hostapi_audio_get_state(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    audio_refresh_finished();
    return s_audio_state;
}

/* ---- ファイル列挙 (Phase 6C) ----
 * 実機側 hostapi.cpp と同じ契約: MUSIC_ROOT 直下の .mp3 を idx で列挙 */
static bool has_mp3_ext(const char* name)
{
    size_t len = strlen(name);
    return len > 4 && strcasecmp(name + len - 4, ".mp3") == 0;
}

int32_t native_hostapi_fs_list(wasm_exec_env_t exec_env, int32_t idx,
                               char* buf, uint32_t buf_len)
{
    (void)exec_env;
    if (idx < 0) return -1;

    DIR* dir = opendir(MUSIC_ROOT);
    if (!dir) return -1;

    int32_t found = -1;
    int32_t count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!has_mp3_ext(ent->d_name)) continue;
        size_t name_len = strlen(ent->d_name);
        if (name_len > 63) continue; /* 契約: 63 バイト超は列挙から除外 */

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", MUSIC_ROOT, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (count == idx) {
            uint32_t n = (name_len < buf_len) ? (uint32_t)name_len : buf_len;
            memcpy(buf, ent->d_name, n);
            found = (int32_t)n;
            break;
        }
        count++;
    }
    closedir(dir);
    return found;
}

/* ---- ファイル読み書き (Phase 20) ----
 * 実機側 hostapi.cpp と同じ契約: DATA_ROOT 直下の**フラットな名前だけ**を許し、
 * 書き込みは一時ファイル + rename。ルートはホストが起動時に作る。 */
#define DATA_ROOT "./sdcard/data"
#define MAX_DATA_PATH_LEN 63

static void ensure_data_root(void)
{
    /* 親 → 子の順に作る(既に在れば EEXIST で無視) */
    if (mkdir("./sdcard", 0775) != 0 && errno != EEXIST) {
        fprintf(stderr, "cannot create ./sdcard: %s\n", strerror(errno));
    }
    if (mkdir(DATA_ROOT, 0775) != 0 && errno != EEXIST) {
        fprintf(stderr, "cannot create %s: %s\n", DATA_ROOT, strerror(errno));
    }
}

static bool data_path_ok(const char* path, uint32_t len)
{
    uint32_t i;
    if (len == 0 || len > MAX_DATA_PATH_LEN) return false;
    if (path[0] == '.') return false; /* "." / ".." / 隠しファイル */
    for (i = 0; i < len; i++) {
        const char c = path[i];
        if (c == '/' || c == '\\' || c == '\0') return false;
        if (c == '.' && i + 1 < len && path[i + 1] == '.') return false;
    }
    return true;
}

/* rel(NUL 終端なし)を DATA_ROOT 直下のパスにする。不正なら false */
static bool data_full_path(const char* path, uint32_t len, char* out, size_t out_len,
                           const char* suffix)
{
    char rel[MAX_DATA_PATH_LEN + 1];
    int n;
    if (!data_path_ok(path, len)) return false;
    memcpy(rel, path, len);
    rel[len] = '\0';
    n = snprintf(out, out_len, "%s/%s%s", DATA_ROOT, rel, suffix);
    return n > 0 && (size_t)n < out_len;
}

int32_t native_hostapi_fs_read(wasm_exec_env_t exec_env, const char* path, uint32_t path_len,
                               char* buf, uint32_t buf_len)
{
    char full[256];
    size_t n;
    bool bad;
    FILE* f;
    (void)exec_env;
    if (!data_full_path(path, path_len, full, sizeof(full), "")) {
        fprintf(stderr, "fs_read: rejected path\n");
        return -1;
    }
    f = fopen(full, "rb");
    if (!f) return -1; /* 無いのは正常系(空きスロット) */
    n = (buf_len > 0) ? fread(buf, 1, buf_len, f) : 0;
    bad = ferror(f) != 0;
    fclose(f);
    if (bad) {
        fprintf(stderr, "fs_read: %s failed\n", full);
        return -1;
    }
    return (int32_t)n; /* ファイルが buf_len より大きくても切り詰めて成功 */
}

int32_t native_hostapi_fs_write(wasm_exec_env_t exec_env, const char* path, uint32_t path_len,
                                const char* buf, uint32_t buf_len)
{
    char full[256];
    char tmp[256];
    size_t n;
    int closed;
    FILE* f;
    (void)exec_env;
    if (!data_full_path(path, path_len, full, sizeof(full), "") ||
        !data_full_path(path, path_len, tmp, sizeof(tmp), ".tmp")) {
        fprintf(stderr, "fs_write: rejected path\n");
        return -1;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        fprintf(stderr, "fs_write: cannot open %s\n", tmp);
        return -1;
    }
    n = (buf_len > 0) ? fwrite(buf, 1, buf_len, f) : 0;
    closed = fclose(f);
    if (n != buf_len || closed != 0) {
        fprintf(stderr, "fs_write: %s failed (%zu/%u)\n", tmp, n, (unsigned)buf_len);
        remove(tmp);
        return -1;
    }
    /* 実機(FATFS)に合わせて宛先を先に消してから rename する */
    remove(full);
    if (rename(tmp, full) != 0) {
        fprintf(stderr, "fs_write: rename failed: %s\n", full);
        remove(tmp);
        return -1;
    }
    return 0;
}

/* ---- 入力イベントキュー (Phase 6A) ----
 * 実機と同じ規約: 深さ 16、満杯は最古から捨てる、DOWN 未配送の UP は捨てる。
 * Linux は main ループ単一スレッドなのでロック不要。 */
static hostapi_event_t s_evq[EVENT_QUEUE_DEPTH];
static int s_evq_head = 0;
static int s_evq_count = 0;
static bool s_down_delivered = false;
static bool s_pressed = false; /* 押下中か(MOVE を出すのはこの間だけ) */
static int16_t s_last_x = 0; /* 最後に配送した座標(MOVE の間引きの基準) */
static int16_t s_last_y = 0;

void host_sdl_clear_events(void)
{
    s_evq_head = 0;
    s_evq_count = 0;
    s_down_delivered = false;
    s_pressed = false;
    s_last_x = 0;
    s_last_y = 0;
}

/* 実際にキューへ積む(マスター設定の関所を通した後)。Phase 21b で分離した */
static void enqueue_touch(bool down, int x, int y);

void host_sdl_push_touch(bool down, int x, int y)
{
    /* **マスター設定の関所**(Phase 21b)。上端からの下スワイプで開く。
     * 上端で始まった押下は結論が出るまで保留し、違ったら後から流す */
    masterui_action_t a =
        masterui_on_touch(down ? HOSTAPI_EV_TOUCH_DOWN : HOSTAPI_EV_TOUCH_UP, x, y);
    if (a == MASTERUI_CONSUME || a == MASTERUI_HOLD) return;
    if (a == MASTERUI_FLUSH_THEN_PASS) {
        int hx, hy;
        masterui_held_down(&hx, &hy);
        enqueue_touch(true, hx, hy); /* 保留していた DOWN を先に流す */
    }
    enqueue_touch(down, x, y);
}

static void enqueue_touch(bool down, int x, int y)
{
    /* アプリを起動したクリックの UP がアプリに漏れないように */
    if (!down && !s_down_delivered) return;
    if (down) {
        s_down_delivered = true;
        s_last_x = (int16_t)x;
        s_last_y = (int16_t)y;
    }
    s_pressed = down;

    if (s_evq_count == EVENT_QUEUE_DEPTH) { /* 満杯: 最古を捨てる */
        s_evq_head = (s_evq_head + 1) % EVENT_QUEUE_DEPTH;
        s_evq_count--;
        fprintf(stderr, "event queue full, dropped oldest\n");
    }
    hostapi_event_t* ev = &s_evq[(s_evq_head + s_evq_count) % EVENT_QUEUE_DEPTH];
    ev->type = down ? HOSTAPI_EV_TOUCH_DOWN : HOSTAPI_EV_TOUCH_UP;
    ev->param = 0;
    ev->x = (int16_t)x;
    ev->y = (int16_t)y;
    ev->time_ms = SDL_GetTicks() - s_start_ms;
    s_evq_count++;
}

void host_sdl_push_touch_move(int x, int y)
{
    int dx, dy;
    hostapi_event_t* tail;
    masterui_action_t a = masterui_on_touch(HOSTAPI_EV_TOUCH_MOVE, x, y);
    if (a == MASTERUI_CONSUME || a == MASTERUI_HOLD) return;
    if (a == MASTERUI_FLUSH_THEN_PASS) {
        int hx, hy;
        masterui_held_down(&hx, &hy);
        enqueue_touch(true, hx, hy);
    }

    /* 押下中だけ出す(実機の LV_EVENT_PRESSING と同じ意味にする)。
     * SDL のボタンマスクではなく、自分が配送した DOWN/UP で判断するので、
     * 合成イベント(xdotool の --window クリック)でも同じ経路を通る */
    if (!s_down_delivered || !s_pressed) return;

    dx = x - (int)s_last_x;
    dy = y - (int)s_last_y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx < HOSTAPI_TOUCH_MOVE_MIN_PX && dy < HOSTAPI_TOUCH_MOVE_MIN_PX) return;

    s_last_x = (int16_t)x;
    s_last_y = (int16_t)y;

    /* 末尾が MOVE なら上書きする(畳み込み。MOVE でキューを溢れさせない) */
    if (s_evq_count > 0) {
        tail = &s_evq[(s_evq_head + s_evq_count - 1) % EVENT_QUEUE_DEPTH];
        if (tail->type == HOSTAPI_EV_TOUCH_MOVE) {
            tail->x = (int16_t)x;
            tail->y = (int16_t)y;
            tail->time_ms = SDL_GetTicks() - s_start_ms;
            return;
        }
    }

    if (s_evq_count == EVENT_QUEUE_DEPTH) { /* 満杯: 最古を捨てる */
        s_evq_head = (s_evq_head + 1) % EVENT_QUEUE_DEPTH;
        s_evq_count--;
        fprintf(stderr, "event queue full, dropped oldest\n");
    }
    tail = &s_evq[(s_evq_head + s_evq_count) % EVENT_QUEUE_DEPTH];
    tail->type = HOSTAPI_EV_TOUCH_MOVE;
    tail->param = 0;
    tail->x = (int16_t)x;
    tail->y = (int16_t)y;
    tail->time_ms = SDL_GetTicks() - s_start_ms;
    s_evq_count++;
}

bool host_sdl_init(void)
{
    ensure_data_root(); /* Phase 20: 保存先を作っておく */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    /* ALLOW_HIGHDPI: ディスプレイスケール環境(ChromeOS 等)でウィンドウサイズと
     * マウスイベントの単位(ポイント)を一致させる。無いとイベントだけ 1/scale に
     * なりヒットテストがずれる */
    s_window = SDL_CreateWindow("MidiAppBox WASM host",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                SCREEN_W * WINDOW_SCALE, SCREEN_H * WINDOW_SCALE,
                                SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(s_renderer, SCREEN_W, SCREEN_H);

#ifdef HAVE_SDL_TTF
    try_open_font();
#endif

    s_start_ms = SDL_GetTicks();

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = CLICK_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    /* Phase 21: 1024(~23.2ms)から 256(~5.8ms)へ。実機のミキサのブロック長
     * (240 フレーム = 5.44ms)に近づけ、発音の丸めを両ホストでそろえる */
    want.samples = 256;
    want.callback = audio_callback;
    s_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!s_audio) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s (continuing without sound)\n",
                SDL_GetError());
    } else {
        wav_open();
        SDL_PauseAudioDevice(s_audio, 0);
    }

#ifdef HAVE_SDL_MIXER
    if ((Mix_Init(MIX_INIT_MP3) & MIX_INIT_MP3) == 0) {
        /* click デバイスとは独立(OS ミキサで混合) */
        fprintf(stderr, "Mix_Init(MP3) failed: %s (audio_play disabled)\n",
                Mix_GetError());
    } else if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s (audio_play disabled)\n",
                Mix_GetError());
    } else {
        s_mixer_ready = true;
        Mix_HookMusicFinished(music_finished_hook);
        Mix_VolumeMusic(98 * MIX_MAX_VOLUME / 100); /* 実機の既定音量 98 に合わせる */
    }
#endif

    return true;
}

void host_sdl_shutdown(void)
{
#ifdef HAVE_SDL_TTF
    if (s_font) TTF_CloseFont(s_font);
    if (TTF_WasInit()) TTF_Quit();
#endif
#ifdef HAVE_SDL_MIXER
    host_sdl_audio_reset();
    if (s_mixer_ready) Mix_CloseAudio();
    Mix_Quit();
#endif
    if (s_audio) SDL_CloseAudioDevice(s_audio);
    wav_close();
    if (s_renderer) SDL_DestroyRenderer(s_renderer);
    if (s_window) SDL_DestroyWindow(s_window);
    SDL_Quit();
}

static void draw_char8x8(int32_t x, int32_t y, unsigned char c)
{
    if (c >= 128) c = '?';
    const char* glyph = font8x8_basic[c];
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            if (glyph[row] & (1 << col)) {
                SDL_RenderDrawPoint(s_renderer, x + col, y + row);
            }
        }
    }
}

/* Phase 18b: 実機のフォント(LVGL Montserrat + FontAwesome サブセット)にある
 * U+F04B(▶)/ U+F04D(■)は DejaVu に無い。アプリが両ホストで同じバイト列を書けるよう、
 * これらだけホスト側が図形として描く。戻り値は進めた幅(px)、0 = 記号ではない。
 * Phase 19a で U+F00D(✕、タイルの削除)、Phase 21f で U+F079(⟲、リピートのトグル)を追加した */
#define SYM_W 12
static int draw_symbol(int x, int y, const unsigned char* p, uint32_t rgb888)
{
    int i;
    SDL_Rect r;
    if (p[0] != 0xEF) return 0;
    if (p[1] == 0x80 && p[2] == 0x81) { /* U+F001: 実機は ♪。ここではメトロノームの形で描く */
        SDL_SetRenderDrawColor(s_renderer, (rgb888 >> 16) & 0xff, (rgb888 >> 8) & 0xff,
                               rgb888 & 0xff, 255);
        for (i = 0; i < 6; ++i) { /* 台形の本体(下ほど広い) */
            r.x = x + 5 - i;
            r.y = y + 6 + i;
            r.w = 2 + i * 2;
            r.h = 1;
            SDL_RenderFillRect(s_renderer, &r);
        }
        for (i = 0; i < 6; ++i) { /* 振り子(本体の上に出る右上がりの棒) */
            r.x = x + 6 + i / 2;
            r.y = y + 6 - i;
            r.w = 1;
            r.h = 1;
            SDL_RenderFillRect(s_renderer, &r);
        }
        return SYM_W;
    }
    if (p[1] == 0x80 && p[2] == 0x8D) { /* U+F00D ✕(2 本の斜線) */
        SDL_SetRenderDrawColor(s_renderer, (rgb888 >> 16) & 0xff, (rgb888 >> 8) & 0xff,
                               rgb888 & 0xff, 255);
        for (i = 0; i < 9; ++i) {
            r.w = 2; r.h = 2;
            r.x = x + 1 + i; r.y = y + 2 + i;
            SDL_RenderFillRect(s_renderer, &r);
            r.x = x + 1 + i; r.y = y + 10 - i;
            SDL_RenderFillRect(s_renderer, &r);
        }
        return SYM_W;
    }
    if (p[1] != 0x81) return 0;
    SDL_SetRenderDrawColor(s_renderer, (rgb888 >> 16) & 0xff, (rgb888 >> 8) & 0xff,
                           rgb888 & 0xff, 255);
    if (p[2] == 0x8B) { /* U+F04B ▶(右向き三角): 左端が最も高く、右端で 1px になる */
        for (i = 0; i < 10; ++i) {
            int h = 11 - i;
            r.x = x + 1 + i;
            r.y = y + 6 - h / 2;
            r.w = 1;
            r.h = h;
            SDL_RenderFillRect(s_renderer, &r);
        }
        return SYM_W;
    }
    if (p[2] == 0xB9) { /* U+F079 ⟲(Phase 21f、`RPT` のトグル): 上の辺は右向き、下の辺は左向きの矢印で輪を作る */
        r.w = 7; r.h = 2;
        r.x = x + 2; r.y = y + 2;  SDL_RenderFillRect(s_renderer, &r); /* 上の辺 */
        r.x = x + 3; r.y = y + 9;  SDL_RenderFillRect(s_renderer, &r); /* 下の辺 */
        r.w = 2; r.h = 5;
        r.x = x + 1;  r.y = y + 2; SDL_RenderFillRect(s_renderer, &r); /* 左の辺(上から下りる) */
        r.x = x + 9;  r.y = y + 5; SDL_RenderFillRect(s_renderer, &r); /* 右の辺(下から上る) */
        for (i = 0; i < 3; ++i) {  /* 矢じり: 上の辺の右端(右向き)、下の辺の左端(左向き) */
            r.w = 1; r.h = 1 + 2 * (2 - i);
            r.x = x + 9 + i;  r.y = y + 1 + i; SDL_RenderFillRect(s_renderer, &r);
            r.x = x + 2 - i;  r.y = y + 8 + i; SDL_RenderFillRect(s_renderer, &r);
        }
        return SYM_W;
    }
    if (p[2] == 0x8D) { /* U+F04D ■ */
        r.x = x + 1; r.y = y + 1; r.w = 10; r.h = 10;
        SDL_RenderFillRect(s_renderer, &r);
        return SYM_W;
    }
    return 0;
}

/* テキスト描画の共通経路。TTF があればアンチエイリアス描画、無ければ font8x8。
 * 記号(上記)は図形で描き、残りを通常のテキストとして描く */
static void draw_string(int x, int y, const char* s, uint32_t rgb888)
{
    {
        const unsigned char* p = (const unsigned char*)s;
        int adv = draw_symbol(x, y, p, rgb888);
        if (adv > 0) {
            if (p[3] != '\0') draw_string(x + adv, y, (const char*)(p + 3), rgb888);
            return;
        }
    }
#ifdef HAVE_SDL_TTF
    if (s_font && s[0]) {
        SDL_Color color = { (Uint8)(rgb888 >> 16), (Uint8)(rgb888 >> 8),
                            (Uint8)rgb888, 255 };
        SDL_Surface* surf = TTF_RenderUTF8_Blended(s_font, s, color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(s_renderer, surf);
            if (tex) {
                SDL_Rect dst = { x, y, surf->w / WINDOW_SCALE,
                                 surf->h / WINDOW_SCALE };
                SDL_RenderCopy(s_renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
            return;
        }
    }
#endif
    SDL_SetRenderDrawColor(s_renderer, (rgb888 >> 16) & 0xff, (rgb888 >> 8) & 0xff,
                           rgb888 & 0xff, 255);
    for (size_t k = 0; s[k]; ++k) {
        draw_char8x8(x + (int)k * 8, y, (unsigned char)s[k]);
    }
}

/* ---- 直描画ヘルパ(ランチャーメニュー用。retained スロットとは別系統) ---- */

void host_sdl_clear_slots(void)
{
    memset(s_texts, 0, sizeof(s_texts));
    memset(s_rects, 0, sizeof(s_rects));
}

void host_sdl_begin_frame(uint32_t rgb888)
{
    SDL_SetRenderDrawColor(s_renderer, (rgb888 >> 16) & 0xff, (rgb888 >> 8) & 0xff,
                           rgb888 & 0xff, 255);
    SDL_RenderClear(s_renderer);
}

void host_sdl_rect(int x, int y, int w, int h, uint32_t rgb888)
{
    SDL_SetRenderDrawColor(s_renderer, (rgb888 >> 16) & 0xff, (rgb888 >> 8) & 0xff,
                           rgb888 & 0xff, 255);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(s_renderer, &rect);
}

void host_sdl_text(int x, int y, const char* s, uint32_t rgb888)
{
    draw_string(x, y, s, rgb888);
}

void host_sdl_present(void)
{
    SDL_RenderPresent(s_renderer);
}

void host_sdl_debug_dump_coords(int wx, int wy, int lx, int ly)
{
    int ww = 0, wh = 0, ow = 0, oh = 0;
    float sx = 1, sy = 1;
    SDL_Rect vp;
    SDL_GetWindowSize(s_window, &ww, &wh);
    SDL_GetRendererOutputSize(s_renderer, &ow, &oh);
    SDL_RenderGetScale(s_renderer, &sx, &sy);
    SDL_RenderGetViewport(s_renderer, &vp);
    printf("click: ev=(%d,%d) win=(%d,%d) out=(%d,%d) scale=(%.2f,%.2f) "
           "vp=(%d,%d,%d,%d) -> logical=(%d,%d)\n",
           wx, wy, ww, wh, ow, oh, sx, sy, vp.x, vp.y, vp.w, vp.h, lx, ly);
    fflush(stdout);
}

void host_sdl_window_to_logical(int wx, int wy, int* lx, int* ly)
{
    /* SDL2 は SDL_RenderSetLogicalSize を設定すると、マウスイベント座標を
     * 自動で論理座標(SCREEN_W x SCREEN_H)へ変換して届ける
     * (SDL_RendererEventWatch)。したがってここでは変換せず、クランプのみ行う。
     * 追加で割ると二重変換になり、ヒットテストが左上 1/4 に縮む。 */
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    if (wx >= SCREEN_W) wx = SCREEN_W - 1;
    if (wy >= SCREEN_H) wy = SCREEN_H - 1;
    *lx = wx;
    *ly = wy;
}

/* マスター設定のオーバーレイ(Phase 21b)。
 * **アプリのスロットを 1 つも使わず**、描画のいちばん最後に上から重ねる。
 * 座標と当たり判定は shared/master_ui.h(両ホストで同じ) */
void draw_master_overlay_if_open(void);
static void draw_master_overlay(void)
{
    char buf[16];
    int v, fill;
    if (!masterui_is_open()) return;

    /* 帯は不透明(全画面の半透明は毎フレームのブレンドが高いので使わない) */
    host_sdl_rect(0, 0, SCREEN_W, MASTERUI_BAND_H, 0x1a2234);
    host_sdl_rect(0, MASTERUI_BAND_H - 2, SCREEN_W, 2, 0x305090);
    draw_string(12, 8, "Settings", 0xffffff);
    draw_string(MASTERUI_CLOSE_X + 10, MASTERUI_CLOSE_Y + 6, "X", 0xf06060);

    for (int i = 0; i < MASTERUI_ITEMS; i++) {
        const int ry = MASTERUI_ROW_Y(i);
        const int ty = ry + 8;
        /* ラベルの箱 = MUTE のトグル(Phase 21c)。状態は文字色: 緑 = 鳴る / 灰 = MUTE */
        const bool muted = masterui_is_muted((masterui_item_t)i);
        const uint32_t on_col = muted ? 0x707880 : 0x40e070;
        host_sdl_rect(MASTERUI_MUTE_X, ry, MASTERUI_MUTE_W, MASTERUI_ROW_H, 0x2a3340);
        draw_string(MASTERUI_LABEL_X, ty, masterui_label((masterui_item_t)i), on_col);
        host_sdl_rect(MASTERUI_MINUS_X, ry, MASTERUI_BTN_W, MASTERUI_ROW_H, 0x2a3340);
        draw_string(MASTERUI_MINUS_X + 16, ty, "-", 0xffffff);
        host_sdl_rect(MASTERUI_PLUS_X, ry, MASTERUI_BTN_W, MASTERUI_ROW_H, 0x2a3340);
        draw_string(MASTERUI_PLUS_X + 16, ty, "+", 0xffffff);
        v = masterui_level((masterui_item_t)i);
        snprintf(buf, sizeof(buf), "%d", v);
        draw_string(MASTERUI_VALUE_X, ty, buf, on_col);
        /* バー(要否は使ってから判断する。ユーザー指示) */
        host_sdl_rect(MASTERUI_BAR_X, ry + 10, MASTERUI_BAR_W, MASTERUI_BAR_H, 0x2a3340);
        fill = MASTERUI_BAR_W * v / 100;
        if (fill > 0) host_sdl_rect(MASTERUI_BAR_X, ry + 10, fill, MASTERUI_BAR_H, on_col);
    }
}

void draw_master_overlay_if_open(void) { draw_master_overlay(); }

void host_sdl_render(void)
{
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);

    for (int i = 0; i < MAX_RECT_SLOTS; ++i) {
        if (!s_rects[i].used) continue;
        const RectSlot* r = &s_rects[i];
        SDL_SetRenderDrawColor(s_renderer, (r->rgb888 >> 16) & 0xff,
                               (r->rgb888 >> 8) & 0xff, r->rgb888 & 0xff, 255);
        SDL_Rect rect = { r->x, r->y, r->w, r->h };
        SDL_RenderFillRect(s_renderer, &rect);
    }

    for (int i = 0; i < MAX_TEXT_SLOTS; ++i) {
        if (!s_texts[i].used) continue;
        const TextSlot* t = &s_texts[i];
        draw_string(t->x, t->y, t->text, t->rgb888);
    }

    draw_master_overlay(); /* アプリより上。Phase 21b */

    SDL_RenderPresent(s_renderer);
}

/* ---- natives (wasm import "env") ---- */

/* Phase 18b: draw_text / draw_text_rgb の共通実装。**同じ (x,y) は同じスロット**
 * (色だけ変える再描画も同じスロットを使い、スロットを二重に消費しない) */
static void draw_text_common(int32_t x, int32_t y, const char* str, uint32_t len,
                             uint32_t rgb888)
{
    TextSlot* slot = NULL;
    for (int i = 0; i < MAX_TEXT_SLOTS; ++i) {
        if (s_texts[i].used && s_texts[i].x == x && s_texts[i].y == y) {
            slot = &s_texts[i];
            break;
        }
    }
    if (!slot) {
        for (int i = 0; i < MAX_TEXT_SLOTS; ++i) {
            if (!s_texts[i].used) { slot = &s_texts[i]; break; }
        }
    }
    if (!slot) {
        fprintf(stderr, "draw_text: no free slot (max %d)\n", MAX_TEXT_SLOTS);
        return;
    }
    if (len > MAX_TEXT_LEN) len = MAX_TEXT_LEN;
    memcpy(slot->text, str, len);
    slot->text[len] = '\0';
    slot->x = x;
    slot->y = y;
    slot->rgb888 = rgb888 & 0xffffff;
    slot->used = true;
}

void native_hostapi_draw_text(wasm_exec_env_t exec_env, int32_t x, int32_t y,
                              const char* str, uint32_t len)
{
    (void)exec_env;
    draw_text_common(x, y, str, len, 0xffffff);
}

void native_hostapi_draw_text_rgb(wasm_exec_env_t exec_env, int32_t x, int32_t y,
                                  const char* str, uint32_t len, uint32_t rgb888)
{
    (void)exec_env;
    draw_text_common(x, y, str, len, rgb888);
}

void native_hostapi_fill_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y,
                              int32_t w, int32_t h, uint32_t rgb888)
{
    (void)exec_env;
    RectSlot* slot = NULL;
    for (int i = 0; i < MAX_RECT_SLOTS; ++i) {
        if (s_rects[i].used && s_rects[i].x == x && s_rects[i].y == y) {
            slot = &s_rects[i];
            break;
        }
    }
    if (!slot) {
        for (int i = 0; i < MAX_RECT_SLOTS; ++i) {
            if (!s_rects[i].used) { slot = &s_rects[i]; break; }
        }
    }
    if (!slot) {
        fprintf(stderr, "fill_rect: no free slot (max %d)\n", MAX_RECT_SLOTS);
        return;
    }
    slot->x = x;
    slot->y = y;
    slot->w = w;
    slot->h = h;
    slot->rgb888 = rgb888;
    slot->used = true;
}

/* slot を解決してコピーを返す(未定義なら false)。ロック外から呼ぶこと */
static bool tone_lookup(int32_t slot, ToneDef* out)
{
    if (slot < 0 || slot >= HOSTAPI_TONE_SLOTS) return false;
    bool ok;
    SDL_LockAudioDevice(s_audio);
    ok = s_tones[slot].defined;
    if (ok) *out = s_tones[slot];
    SDL_UnlockAudioDevice(s_audio);
    return ok;
}

static int32_t tone_play_impl(int32_t slot)
{
    if (!s_audio) return -1;
    ToneDef tone;
    if (!tone_lookup(slot, &tone)) return -1;
    /* 即時発音 = 次に書くブロックの先頭で開始(Phase 21: 要求キューに積む) */
    Req r = { .is_synth = 0, .note = 0, .velocity = 0, .tone = tone };
    SDL_LockAudioDevice(s_audio);
    req_push(&r);
    SDL_UnlockAudioDevice(s_audio);
    return 0;
}

/* L0 の CLICK ポート(Phase 11)からの発音。既存の即時発音経路
 * (tone_play_impl → s_click_asap → audio_callback)をそのまま共有の出口に
 * するので、音声側の追加配線はない。 */
void host_click_play_slot(uint32_t slot)
{
    (void)tone_play_impl((int32_t)slot);
}

/* L0 の SYNTH ポート(Phase 21)。at_host_us は案 A では使わない
 * (docs/results/phase21.md 0-c。案 B へ移るときにここでブロック内オフセットを求める) */
void host_synth_note(uint8_t note, uint8_t velocity, int64_t at_host_us)
{
    (void)at_host_us;
    if (!s_audio) return;
    Req r = { .is_synth = 1, .note = note, .velocity = velocity, .tone = (ToneDef){0} };
    SDL_LockAudioDevice(s_audio);
    req_push(&r);
    SDL_UnlockAudioDevice(s_audio);
}

void native_hostapi_play_click(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    tone_play_impl(0);
}

int32_t native_hostapi_tone_define(wasm_exec_env_t exec_env, int32_t slot,
                                   int32_t wave, int32_t freq_hz, int32_t dur_ms,
                                   int32_t level)
{
    (void)exec_env;
    if (slot < 0 || slot >= HOSTAPI_TONE_SLOTS) return -1;
    if (wave != HOSTAPI_WAVE_SINE) return -1; /* 未知の波形(トラップしない) */
    if (!s_audio) return -1;

    if (freq_hz < 100) freq_hz = 100;
    if (freq_hz > 8000) freq_hz = 8000;
    if (dur_ms < 5) dur_ms = 5;
    if (dur_ms > 100) dur_ms = 100;
    if (level < 0) level = 0;
    if (level > 100) level = 100;

    SDL_LockAudioDevice(s_audio);
    s_tones[slot] = (ToneDef){true, (uint16_t)freq_hz, (uint16_t)dur_ms, (uint8_t)level};
    SDL_UnlockAudioDevice(s_audio);
    return 0;
}

int32_t native_hostapi_tone_play(wasm_exec_env_t exec_env, int32_t slot)
{
    (void)exec_env;
    return tone_play_impl(slot);
}

uint32_t native_hostapi_now_ms(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    /* SDL_GetTicks は内部で CLOCK_MONOTONIC 相当。起動からの経過 ms を返す */
    return SDL_GetTicks() - s_start_ms;
}

/* buf は WAMR 境界検証済み(シグネチャ "*~")。書いた件数を返す */
int32_t native_hostapi_poll_event(wasm_exec_env_t exec_env, char* buf, uint32_t len)
{
    (void)exec_env;
    const uint32_t max_events = len / sizeof(hostapi_event_t);
    int32_t n = 0;
    while (n < (int32_t)max_events && s_evq_count > 0) {
        memcpy(buf + n * sizeof(hostapi_event_t), &s_evq[s_evq_head],
               sizeof(hostapi_event_t));
        s_evq_head = (s_evq_head + 1) % EVENT_QUEUE_DEPTH;
        s_evq_count--;
        n++;
    }
    return n;
}
