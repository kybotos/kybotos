/* 内蔵音源のボイス(Phase 23)。説明は synth_voice.h。
 * Phase 21〜22 の間は実機の audio.cpp と Linux の hostapi_sdl.c に同じコードがあり、手で揃えていた。
 * 式と定数はそのまま移した(切り出しの前後で出力が一致することを hosts/linux/tests/synth_voice_test.c で確かめる)。 */
#include "synth_voice.h"

#include <math.h>
#include <string.h>

#include "hostapi_defs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ボイスの種類。SAMPLE への差し替えは voice_render の中身だけで済む(Phase 21 0-d) */
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

static void voice_set_sine(Voice* v, float freq, int frames, float amp)
{
    const float w = 2.0f * (float)M_PI * freq / SYNTHV_RATE;
    v->s = 0.0f;
    v->c = 1.0f;
    v->cw = cosf(w);
    v->sw = sinf(w);
    v->amp = amp;
    v->decay = expf(-3.5f / (float)(frames > 0 ? frames : 1));
    v->remaining = frames;
}

/* 空きボイスを取る。無ければ「同じ note の最も古いもの」→「全体で最も古いもの」を奪う(hostapi_defs.h の契約) */
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

void synthv_reset(void)
{
    memset(s_voices, 0, sizeof(s_voices));
    s_voice_seq = 0;
}

/* **掛ける順を変えないこと**(float の丸めが変わり、出力が変わる。docs/results/phase23.md b) */
void synthv_start_tone(uint16_t freq_hz, uint16_t dur_ms, uint8_t level, int master_vol, int gain_click)
{
    Voice* v = voice_alloc(0);
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->kind = VK_TONE;
    v->seq = ++s_voice_seq;
    voice_set_sine(v, (float)freq_hz, SYNTHV_RATE * dur_ms / 1000,
                   12000.0f * level / 100.0f * master_vol / 100.0f * gain_click / 100.0f);
}

/* SYNTH ポート。基準レベルは **4 音同時 + クリックでクリップしない**ように決めた(Phase 21 0-f)。
 * **掛ける順を変えないこと**(synthv_start_tone と同じ理由) */
void synthv_start_drum(uint8_t note, uint8_t velocity, int master_vol, int gain_synth)
{
    const float g = (float)velocity / 127.0f * (float)master_vol / 100.0f
                    * (float)gain_synth / 100.0f;
    Voice* v = voice_alloc(note);
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->seq = ++s_voice_seq;
    v->note = note;
    v->rng = 0x9E3779B9u ^ (uint32_t)note ^ (s_voice_seq << 8);
    switch (note) {
    case HOSTAPI_SYNTH_NOTE_KICK:
        v->kind = VK_KICK;
        voice_set_sine(v, 110.0f, SYNTHV_RATE * 180 / 1000, 7000.0f * g);
        v->f_cur = 110.0f;
        v->f_end = 45.0f;
        v->f_k = expf(-1.0f / ((float)SYNTHV_RATE * 0.040f)); /* 掃引 40ms */
        break;
    case HOSTAPI_SYNTH_NOTE_SNARE:
        v->kind = VK_SNARE;
        voice_set_sine(v, 190.0f, SYNTHV_RATE * 140 / 1000, 2800.0f * g);
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-4.5f / (float)(SYNTHV_RATE * 140 / 1000));
        break;
    case HOSTAPI_SYNTH_NOTE_CHH:
        v->kind = VK_HAT;
        v->remaining = SYNTHV_RATE * 45 / 1000;
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-5.0f / (float)v->remaining);
        break;
    case HOSTAPI_SYNTH_NOTE_CRASH:
        v->kind = VK_CRASH;
        v->remaining = SYNTHV_RATE * 800 / 1000;
        v->n_amp = 5600.0f * g;
        v->n_decay = expf(-4.0f / (float)v->remaining);
        break;
    case HOSTAPI_SYNTH_NOTE_METRO_CLICK:
    case HOSTAPI_SYNTH_NOTE_METRO_BELL: {
        /* メトロノーム(Phase 21c)。ウッドブロック系の短い共鳴音: 減衰サイン + 4ms で消えるノイズ
         * (叩いた瞬間のアタック)。**Bell は Click より高く長く鳴る**(明るく目立つ)。
         * 中〜高域に置くのは内蔵スピーカーが低域を出さないため(所感 D-1) */
        const bool bell = (note == HOSTAPI_SYNTH_NOTE_METRO_BELL);
        v->kind = VK_WOOD;
        voice_set_sine(v, bell ? 2000.0f : 1200.0f, SYNTHV_RATE * (bell ? 150 : 60) / 1000, 8000.0f * g);
        v->n_amp = 3000.0f * g;
        v->n_decay = expf(-5.0f / ((float)SYNTHV_RATE * 0.004f));
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
        const float w = 2.0f * (float)M_PI * v->f_cur / SYNTHV_RATE;
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

bool synthv_render(int32_t* acc, int n)
{
    bool any = false;
    for (int v = 0; v < HOSTAPI_SYNTH_VOICES; v++) {
        if (s_voices[v].kind != VK_IDLE) { voice_render(&s_voices[v], acc, n); any = true; }
    }
    return any;
}
