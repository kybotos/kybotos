#!/usr/bin/env bash
# scripts/synth-voice-compare.sh — 内蔵音源のボイスを、決めた入力で鳴らして出力のハッシュを出す(Phase 23)。
#
#   synth-voice-compare.sh [REV]
#     REV(既定 HEAD)の git の版から、Linux(hosts/linux/hostapi_sdl.c)と実機(src/components/audio/audio.cpp)の
#     ボイスのコードを目印の行で抜き出し、x86 でビルドして同じ入力(5 つのシナリオ × 4 つのゲイン × 3 つのブロック長、
#     各 2 秒)で鳴らす。ケースごとに両者のハッシュ(FNV-1a、出力の int32 の列)を出し、食い違いの数を数える。
#     全ケースで一致すれば exit 0。
#     **REV に shared/synth_voice.c があれば、それも同じ入力で鳴らす**(Phase 23 で切り出した共通の C)。
#     目印が見つからない側(切り替えた後のホスト)は外す。並べた実装のハッシュがすべて同じなら一致とする。
#
# 用途: Phase 23 のステップ 0 で「2 本のボイスが同じ」こと(指示書の前提 P1)を数値で確かめ、
#       切り出し前のハッシュを、共通の C(shared/synth_voice.c)の単体テストの期待値にするため。
#       実機の出力そのもの(ESP32-S3 の FPU と libm)とは比べられない。比べているのは「同じコードか」まで。
# 目印: Linux は「#define CLICK_RATE」の行から「/* ジッタ統計」の前まで。実機は「constexpr int kMixRate」から
#       最初の「} // namespace」の前まで(ミキサの静的変数とボイスの関数)。コードの並びを変えたら直す。
# docs/workflow.md §2.3(検証に使ったスクリプトは scripts/ に置く)。作業ディレクトリは mktemp。
set -euo pipefail
REV="${1:-HEAD}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

git -C "$REPO" show "$REV:hosts/linux/hostapi_sdl.c" > "$W/hostapi_sdl.c"
git -C "$REPO" show "$REV:src/components/audio/audio.cpp" > "$W/audio.cpp"
git -C "$REPO" show "$REV:shared/hostapi_defs.h" > "$W/hostapi_defs.h"
HAVE_S=0
if git -C "$REPO" cat-file -e "$REV:shared/synth_voice.c" 2>/dev/null; then
  git -C "$REPO" show "$REV:shared/synth_voice.c" > "$W/synth_voice.c"
  git -C "$REPO" show "$REV:shared/synth_voice.h" > "$W/synth_voice.h"
  HAVE_S=1
fi
HAVE_L=0; grep -q '^static void voice_render' "$W/hostapi_sdl.c" && HAVE_L=1
HAVE_D=0; grep -q '^void voice_render' "$W/audio.cpp" && HAVE_D=1

if [ "$HAVE_L" = 1 ]; then
{
  cat <<'EOF'
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "hostapi_defs.h"
#define MASTERUI_DEF_MASTER 50
#define MASTERUI_DEF_MP3 35
#define MASTERUI_DEF_SYNTH 100
#define MASTERUI_DEF_CLICK 100
EOF
  sed -n '/^#define CLICK_RATE/,/^\/\* ジッタ統計/p' "$W/hostapi_sdl.c" | sed '$d'
  cat <<'EOF'
void L_set(int master, int synth, int click) { s_master_vol = master; s_gain_synth = synth; s_gain_click = click; }
void L_reset(void) { memset(s_voices, 0, sizeof(s_voices)); s_voice_seq = 0; }
void L_tone(int f, int d, int lv, int master) { (void)master; ToneDef t = {true, (uint16_t)f, (uint16_t)d, (uint8_t)lv}; voice_start_tone(&t); }
void L_drum(int n, int v, int master) { (void)master; voice_start_drum((uint8_t)n, (uint8_t)v); }
void L_render(int32_t* acc, int n) { for (int v = 0; v < HOSTAPI_SYNTH_VOICES; v++) if (s_voices[v].kind != VK_IDLE) voice_render(&s_voices[v], acc, n); }
EOF
} > "$W/linux_voice.c"
fi

if [ "$HAVE_D" = 1 ]; then
{
  cat <<'EOF'
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <atomic>
#include "hostapi_defs.h"
#define BOOT_SOUND_FRAMES 1
struct Mp3Player { struct ToneMsg { uint16_t freq_hz; uint16_t dur_ms; uint8_t level; uint8_t note; uint8_t velocity; }; };
namespace {
EOF
  sed -n '/^constexpr int kMixRate/,/^} \/\/ namespace$/p' "$W/audio.cpp" | sed '$d'
  cat <<'EOF'
} // namespace
extern "C" {
void D_set(int, int synth, int click) { s_gain_synth = synth; s_gain_click = click; }
void D_reset(void) { memset(s_voices, 0, sizeof(s_voices)); s_voice_seq = 0; }
void D_tone(int f, int d, int lv, int master) { Mp3Player::ToneMsg m{(uint16_t)f, (uint16_t)d, (uint8_t)lv, 0, 0}; voice_start_tone(m, master); }
void D_drum(int n, int v, int master) { voice_start_drum((uint8_t)n, (uint8_t)v, master); }
void D_render(int32_t* acc, int n) { for (int v = 0; v < HOSTAPI_SYNTH_VOICES; v++) if (s_voices[v].kind != VK_IDLE) voice_render(&s_voices[v], acc, n); }
}
EOF
} > "$W/device_voice.cpp"
fi

cat > "$W/shared_voice.c" <<'EOF'
#include <stdint.h>
#include "synth_voice.h"
static int g_synth, g_click;
void S_set(int master, int synth, int click) { (void)master; g_synth = synth; g_click = click; }
void S_reset(void) { synthv_reset(); }
void S_tone(int f, int d, int lv, int master) { synthv_start_tone((uint16_t)f, (uint16_t)d, (uint8_t)lv, master, g_click); }
void S_drum(int n, int v, int master) { synthv_start_drum((uint8_t)n, (uint8_t)v, master, g_synth); }
void S_render(int32_t* acc, int n) { (void)synthv_render(acc, n); }
EOF

cat > "$W/main.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void (*set_fn)(int, int, int);
typedef void (*reset_fn)(void);
typedef void (*tone_fn)(int, int, int, int);
typedef void (*drum_fn)(int, int, int);
typedef void (*render_fn)(int32_t*, int);
typedef struct { const char* name; set_fn set; reset_fn reset; tone_fn tone; drum_fn drum; render_fn render; } Impl;
#define DECL(P) void P##_set(int, int, int); void P##_reset(void); void P##_tone(int, int, int, int); \
                void P##_drum(int, int, int); void P##_render(int32_t*, int);
#if HAVE_L
DECL(L)
#endif
#if HAVE_D
DECL(D)
#endif
#if HAVE_S
DECL(S)
#endif
static const Impl IMPL[] = {
#if HAVE_L
    {"linux", L_set, L_reset, L_tone, L_drum, L_render},
#endif
#if HAVE_D
    {"device", D_set, D_reset, D_tone, D_drum, D_render},
#endif
#if HAVE_S
    {"shared", S_set, S_reset, S_tone, S_drum, S_render},
#endif
};
#define NIMPL ((int)(sizeof(IMPL) / sizeof(IMPL[0])))

/* kind 0 = drum(note, velocity)、1 = tone(freq_hz, dur_ms, level)。at はフレーム */
typedef struct { int at, kind, a, b, c; } Ev;

static uint64_t run(const Impl* m, const Ev* ev, int nev, const int* g, int block, int frames)
{
    uint64_t h = 1469598103934665603ULL;
    int32_t acc[256];
    int e = 0;
    m->reset();
    m->set(g[0], g[1], g[2]);
    for (int t = 0; t < frames; t += block) {
        while (e < nev && ev[e].at <= t) {
            if (ev[e].kind == 0) m->drum(ev[e].a, ev[e].b, g[0]);
            else                 m->tone(ev[e].a, ev[e].b, ev[e].c, g[0]);
            e++;
        }
        memset(acc, 0, sizeof(acc));
        m->render(acc, block);
        for (int i = 0; i < block; i++) { h ^= (uint32_t)acc[i]; h *= 1099511628211ULL; }
    }
    return h;
}

int main(void)
{
    static const Ev s1[] = {{0, 0, 36, 127, 0}, {0, 0, 38, 127, 0}, {0, 0, 42, 127, 0}, {0, 0, 49, 127, 0}};
    static const Ev s2[] = {{0, 0, 33, 100, 0}, {11025, 0, 34, 100, 0}, {22050, 0, 33, 1, 0}, {33075, 0, 60, 127, 0}};
    static const Ev s3[] = {{0, 1, 1000, 30, 100}, {4410, 1, 2000, 100, 50}, {8820, 0, 36, 90, 0}};
    static const int notes[12] = {36, 38, 42, 49, 33, 34, 36, 38, 42, 49, 33, 34};
    Ev s4[12], s5[64];
    for (int i = 0; i < 12; i++) s4[i] = (Ev){0, 0, notes[i], 100, 0};
    for (int i = 0; i < 64; i++) s5[i] = (Ev){i * 1378, 0, (i % 3) ? 42 : 36, 30 + i, 0};
    const struct { const char* name; const Ev* ev; int n; } sc[] = {
        {"4-notes-same-time", s1, 4}, {"metro-and-unknown", s2, 4}, {"tone-and-kick", s3, 3},
        {"12-notes-steal", s4, 12}, {"64-hits", s5, 64}};
    static const int gains[4][3] = {{50, 100, 100}, {100, 100, 100}, {37, 55, 80}, {0, 100, 100}};
    static const int blocks[3] = {240, 100, 1};
    int total = 0, differ = 0;
    for (int s = 0; s < 5; s++)
        for (int g = 0; g < 4; g++)
            for (int b = 0; b < 3; b++) {
                uint64_t h[3];
                int same = 1;
                printf("%-18s master %3d synth %3d click %3d block %3d ",
                       sc[s].name, gains[g][0], gains[g][1], gains[g][2], blocks[b]);
                for (int k = 0; k < NIMPL; k++) {
                    h[k] = run(&IMPL[k], sc[s].ev, sc[s].n, gains[g], blocks[b], 44100 * 2);
                    if (h[k] != h[0]) same = 0;
                    printf(" %s %016llx", IMPL[k].name, (unsigned long long)h[k]);
                }
                printf("%s\n", same ? "" : "  DIFF");
                total++;
                if (!same) differ++;
            }
    printf("cases %d, differ %d\n", total, differ);
    return differ != 0;
}
EOF

OBJS=()
if [ "$HAVE_L" = 1 ]; then gcc -O2 -I"$W" -c "$W/linux_voice.c" -o "$W/l.o"; OBJS+=("$W/l.o"); fi
if [ "$HAVE_D" = 1 ]; then g++ -O2 -std=gnu++17 -I"$W" -c "$W/device_voice.cpp" -o "$W/d.o"; OBJS+=("$W/d.o"); fi
if [ "$HAVE_S" = 1 ]; then
  gcc -O2 -I"$W" -c "$W/synth_voice.c" -o "$W/sv.o"
  gcc -O2 -I"$W" -c "$W/shared_voice.c" -o "$W/s.o"
  OBJS+=("$W/sv.o" "$W/s.o")
fi
gcc -O2 -DHAVE_L="$HAVE_L" -DHAVE_D="$HAVE_D" -DHAVE_S="$HAVE_S" -c "$W/main.c" -o "$W/m.o"
g++ "${OBJS[@]}" "$W/m.o" -lm -o "$W/compare"
echo "rev: $(git -C "$REPO" rev-parse --short "$REV")  linux=$HAVE_L device=$HAVE_D shared=$HAVE_S"
"$W/compare"
