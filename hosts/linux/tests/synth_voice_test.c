/* shared/synth_voice.c の単体テスト(Phase 23)。SDL / WAMR には依存しない。
 *
 * 1. 切り出す前のコードとの一致: scripts/synth-voice-compare.sh と同じ 60 ケース(5 つのシナリオ × 4 つのゲイン ×
 *    3 つのブロック長、各 2 秒)を鳴らし、出力の FNV-1a ハッシュが**切り出す前の Linux / 実機のコード**
 *    (2bf5156。docs/results/phase23.md 0-0)と一致すること。x86 の同じコンパイラでの一致で、実機の FPU での一致ではない。
 * 2. 奪取の規則(hostapi_defs.h の契約): 空き → 同じ note の最も古いもの → 全体で最も古いもの。
 * 3. 未知の note は鳴らない。synthv_reset の後は鳴らない。
 * 実行: ctest --test-dir build --output-on-failure */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hostapi_defs.h"
#include "synth_voice.h"

static int g_fail;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            g_fail++;                                     \
        }                                                 \
    } while (0)

/* ---- 1. 切り出す前のコードとの一致 ---- */

/* kind 0 = drum(note, velocity)、1 = tone(freq_hz, dur_ms, level)。at はフレーム */
typedef struct { int at, kind, a, b, c; } Ev;

/* scripts/synth-voice-compare.sh の run() と同じ手順。**入力・順序を変えたら期待値が無効になる** */
static uint64_t run(const Ev* ev, int nev, const int* g, int block, int frames)
{
    uint64_t h = 1469598103934665603ULL;
    int32_t acc[256];
    int e = 0;
    synthv_reset();
    for (int t = 0; t < frames; t += block) {
        while (e < nev && ev[e].at <= t) {
            if (ev[e].kind == 0) synthv_start_drum((uint8_t)ev[e].a, (uint8_t)ev[e].b, g[0], g[1]);
            else                 synthv_start_tone((uint16_t)ev[e].a, (uint16_t)ev[e].b, (uint8_t)ev[e].c, g[0], g[2]);
            e++;
        }
        memset(acc, 0, sizeof(acc));
        synthv_render(acc, block);
        for (int i = 0; i < block; i++) { h ^= (uint32_t)acc[i]; h *= 1099511628211ULL; }
    }
    return h;
}

/* 切り出す前のハッシュ(captures/phase23/voice-compare-head.txt。Linux と実機のコードで同じ値)。
 * 並びはシナリオ → ゲイン → ブロック長の順 */
static const uint64_t k_expected[60] = {
    0xa4c579d5356414b7ULL, /* 4-notes-same-time master 50 synth 100 click 100 block 240 */
    0xf8e066af5bd18392ULL, /* 4-notes-same-time master 50 synth 100 click 100 block 100 */
    0x97492c740e0a79aaULL, /* 4-notes-same-time master 50 synth 100 click 100 block 1 */
    0x0ad9b1c62a94da43ULL, /* 4-notes-same-time master 100 synth 100 click 100 block 240 */
    0x9613f450410c34a5ULL, /* 4-notes-same-time master 100 synth 100 click 100 block 100 */
    0xdec3b697c40f736dULL, /* 4-notes-same-time master 100 synth 100 click 100 block 1 */
    0xd06ebd9b521a2fadULL, /* 4-notes-same-time master 37 synth 55 click 80 block 240 */
    0x139ec6cb45974d2bULL, /* 4-notes-same-time master 37 synth 55 click 80 block 100 */
    0x65a33158d655a64fULL, /* 4-notes-same-time master 37 synth 55 click 80 block 1 */
    0x7d3cef8b873e2f83ULL, /* 4-notes-same-time master 0 synth 100 click 100 block 240 */
    0x498b9b56505db7e3ULL, /* 4-notes-same-time master 0 synth 100 click 100 block 100 */
    0x498b9b56505db7e3ULL, /* 4-notes-same-time master 0 synth 100 click 100 block 1 */
    0x368bbffca540cffcULL, /* metro-and-unknown master 50 synth 100 click 100 block 240 */
    0x671f8ebf342c9b5cULL, /* metro-and-unknown master 50 synth 100 click 100 block 100 */
    0xf62d39472a53e3e8ULL, /* metro-and-unknown master 50 synth 100 click 100 block 1 */
    0x471219d1254cb00fULL, /* metro-and-unknown master 100 synth 100 click 100 block 240 */
    0x565d54f3b343922fULL, /* metro-and-unknown master 100 synth 100 click 100 block 100 */
    0xc2a20d21bc787193ULL, /* metro-and-unknown master 100 synth 100 click 100 block 1 */
    0xc08357ba73ca8a43ULL, /* metro-and-unknown master 37 synth 55 click 80 block 240 */
    0xbabf7ce393f135e3ULL, /* metro-and-unknown master 37 synth 55 click 80 block 100 */
    0x316d807ec2832a1bULL, /* metro-and-unknown master 37 synth 55 click 80 block 1 */
    0x7d3cef8b873e2f83ULL, /* metro-and-unknown master 0 synth 100 click 100 block 240 */
    0x498b9b56505db7e3ULL, /* metro-and-unknown master 0 synth 100 click 100 block 100 */
    0x498b9b56505db7e3ULL, /* metro-and-unknown master 0 synth 100 click 100 block 1 */
    0xa679820a23acba6dULL, /* tone-and-kick master 50 synth 100 click 100 block 240 */
    0x73bd0b70e26b241bULL, /* tone-and-kick master 50 synth 100 click 100 block 100 */
    0x54d5c618c9c350f4ULL, /* tone-and-kick master 50 synth 100 click 100 block 1 */
    0x4559e26360d47feeULL, /* tone-and-kick master 100 synth 100 click 100 block 240 */
    0x9ea0d27f42795167ULL, /* tone-and-kick master 100 synth 100 click 100 block 100 */
    0xeafb5a277fc5b897ULL, /* tone-and-kick master 100 synth 100 click 100 block 1 */
    0x105aaab87480dbf4ULL, /* tone-and-kick master 37 synth 55 click 80 block 240 */
    0x8e44e339fd9c04c9ULL, /* tone-and-kick master 37 synth 55 click 80 block 100 */
    0xf2808a56d4514048ULL, /* tone-and-kick master 37 synth 55 click 80 block 1 */
    0x7d3cef8b873e2f83ULL, /* tone-and-kick master 0 synth 100 click 100 block 240 */
    0x498b9b56505db7e3ULL, /* tone-and-kick master 0 synth 100 click 100 block 100 */
    0x498b9b56505db7e3ULL, /* tone-and-kick master 0 synth 100 click 100 block 1 */
    0xf6d82401f541fd53ULL, /* 12-notes-steal master 50 synth 100 click 100 block 240 */
    0x44db54f5b42e3c57ULL, /* 12-notes-steal master 50 synth 100 click 100 block 100 */
    0xd68fe890e38e07c5ULL, /* 12-notes-steal master 50 synth 100 click 100 block 1 */
    0xae8de4c6eed4e699ULL, /* 12-notes-steal master 100 synth 100 click 100 block 240 */
    0x6225349d5b8bbe27ULL, /* 12-notes-steal master 100 synth 100 click 100 block 100 */
    0xae2ae7b970440647ULL, /* 12-notes-steal master 100 synth 100 click 100 block 1 */
    0x1518d947ff91b145ULL, /* 12-notes-steal master 37 synth 55 click 80 block 240 */
    0x26d63cb62064fe73ULL, /* 12-notes-steal master 37 synth 55 click 80 block 100 */
    0x4b765025f01aac43ULL, /* 12-notes-steal master 37 synth 55 click 80 block 1 */
    0x7d3cef8b873e2f83ULL, /* 12-notes-steal master 0 synth 100 click 100 block 240 */
    0x498b9b56505db7e3ULL, /* 12-notes-steal master 0 synth 100 click 100 block 100 */
    0x498b9b56505db7e3ULL, /* 12-notes-steal master 0 synth 100 click 100 block 1 */
    0x5e9412d362670113ULL, /* 64-hits master 50 synth 100 click 100 block 240 */
    0x5f8a96b250595f11ULL, /* 64-hits master 50 synth 100 click 100 block 100 */
    0xbf087d64d60e00f8ULL, /* 64-hits master 50 synth 100 click 100 block 1 */
    0xd26aebf35e4be210ULL, /* 64-hits master 100 synth 100 click 100 block 240 */
    0x4f5a712ea1608920ULL, /* 64-hits master 100 synth 100 click 100 block 100 */
    0xe7d1e8f269f8848bULL, /* 64-hits master 100 synth 100 click 100 block 1 */
    0xaf9ea8061c88c979ULL, /* 64-hits master 37 synth 55 click 80 block 240 */
    0x3aee2fa5505068cdULL, /* 64-hits master 37 synth 55 click 80 block 100 */
    0x03ba8240f918d29cULL, /* 64-hits master 37 synth 55 click 80 block 1 */
    0x7d3cef8b873e2f83ULL, /* 64-hits master 0 synth 100 click 100 block 240 */
    0x498b9b56505db7e3ULL, /* 64-hits master 0 synth 100 click 100 block 100 */
    0x498b9b56505db7e3ULL, /* 64-hits master 0 synth 100 click 100 block 1 */
};

static void test_matches_pre_extraction(void)
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
    int k = 0;
    for (int s = 0; s < 5; s++)
        for (int g = 0; g < 4; g++)
            for (int b = 0; b < 3; b++, k++) {
                const uint64_t h = run(sc[s].ev, sc[s].n, gains[g], blocks[b], 44100 * 2);
                CHECK(h == k_expected[k], "%s master %d synth %d click %d block %d: %016llx != %016llx",
                      sc[s].name, gains[g][0], gains[g][1], gains[g][2], blocks[b],
                      (unsigned long long)h, (unsigned long long)k_expected[k]);
            }
}

/* ---- 2〜3. 奪取の規則、未知の note、リセット ---- */

/* 1 フレームだけ描き、鳴っているかを返す */
static bool sounding(void)
{
    int32_t acc[1] = {0};
    return synthv_render(acc, 1);
}

/* n フレーム描いたときの絶対値の最大 */
static int32_t peak(int n)
{
    int32_t acc[256];
    int32_t p = 0;
    while (n > 0) {
        const int m = n > 256 ? 256 : n;
        memset(acc, 0, sizeof(acc));
        synthv_render(acc, m);
        for (int i = 0; i < m; i++) { const int32_t a = acc[i] < 0 ? -acc[i] : acc[i]; if (a > p) p = a; }
        n -= m;
    }
    return p;
}

static void test_unknown_and_reset(void)
{
    synthv_reset();
    CHECK(!sounding(), "nothing sounds after reset");
    synthv_start_drum(60, 127, 100, 100); /* 未知の note */
    CHECK(!sounding(), "unknown note 60 must not sound");
    synthv_start_drum(HOSTAPI_SYNTH_NOTE_CRASH, 127, 100, 100);
    CHECK(sounding(), "crash sounds");
    synthv_reset();
    CHECK(!sounding(), "reset silences a ringing crash");
    synthv_start_tone(1000, 30, 100, 100, 100);
    CHECK(sounding(), "tone sounds");
    CHECK(peak(SYNTHV_RATE * 30 / 1000) > 0, "tone has output");
    CHECK(!sounding(), "tone ends after dur_ms");
}

/* n フレームの出力のハッシュ */
static uint64_t render_hash(int n)
{
    uint64_t h = 1469598103934665603ULL;
    int32_t acc[256];
    while (n > 0) {
        const int m = n > 256 ? 256 : n;
        memset(acc, 0, sizeof(acc));
        synthv_render(acc, m);
        for (int i = 0; i < m; i++) { h ^= (uint32_t)acc[i]; h *= 1099511628211ULL; }
        n -= m;
    }
    return h;
}

/* 奪取の規則。ノイズの種は発音の通し番号から作るので、「奪われずに残ったボイス」と同じ通し番号の
 * ボイスを、奪取を起こさずに組み立てて出力を比べる。**未知の note は通し番号だけ進めて鳴らない**ので、
 * それで番号を合わせる */
static void test_steal(void)
{
    const int frames = SYNTHV_RATE * 300 / 1000;

    /* (a) 同じ note が無い → 全体で最も古いものを奪う。
     *     Crash 8 本(番号 1〜8)が鳴っているところへ Hat(9)→ 最も古い Crash(1)が消える。
     *     比較: 未知の note(1)+ Crash 7 本(2〜8)+ Hat(9) */
    synthv_reset();
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES; i++) synthv_start_drum(HOSTAPI_SYNTH_NOTE_CRASH, 127, 100, 100);
    synthv_start_drum(HOSTAPI_SYNTH_NOTE_CHH, 127, 100, 100);
    const uint64_t steal_oldest = render_hash(frames);
    synthv_reset();
    synthv_start_drum(60, 127, 100, 100);
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES - 1; i++) synthv_start_drum(HOSTAPI_SYNTH_NOTE_CRASH, 127, 100, 100);
    synthv_start_drum(HOSTAPI_SYNTH_NOTE_CHH, 127, 100, 100);
    const uint64_t ref_oldest = render_hash(frames);
    CHECK(steal_oldest == ref_oldest, "no same note: the oldest voice is stolen");

    /* (b) 同じ note があれば、その最も古いものを奪う(全体で最も古いものより優先)。
     *     Crash 7 本(1〜7)+ Kick(8)が鳴っているところへ Kick(9)→ Kick(8)が消え、Crash(1)は残る。
     *     比較: Crash 7 本(1〜7)+ 未知の note(8)+ Kick(9) */
    synthv_reset();
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES - 1; i++) synthv_start_drum(HOSTAPI_SYNTH_NOTE_CRASH, 127, 100, 100);
    synthv_start_drum(HOSTAPI_SYNTH_NOTE_KICK, 127, 100, 100);
    synthv_start_drum(HOSTAPI_SYNTH_NOTE_KICK, 127, 100, 100);
    const uint64_t steal_same = render_hash(frames);
    synthv_reset();
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES - 1; i++) synthv_start_drum(HOSTAPI_SYNTH_NOTE_CRASH, 127, 100, 100);
    synthv_start_drum(60, 127, 100, 100);
    synthv_start_drum(HOSTAPI_SYNTH_NOTE_KICK, 127, 100, 100);
    const uint64_t ref_same = render_hash(frames);
    CHECK(steal_same == ref_same, "same note: its oldest voice is stolen, not the overall oldest");

    /* 比較が意味を持つこと(番号がずれれば出力も変わる) */
    synthv_reset();
    for (int i = 0; i < HOSTAPI_SYNTH_VOICES - 1; i++) synthv_start_drum(HOSTAPI_SYNTH_NOTE_CRASH, 127, 100, 100);
    synthv_start_drum(HOSTAPI_SYNTH_NOTE_CHH, 127, 100, 100);
    CHECK(render_hash(frames) != ref_oldest, "sequence numbers change the noise (the comparison is meaningful)");
}

int main(void)
{
    test_matches_pre_extraction();
    test_unknown_and_reset();
    test_steal();
    if (g_fail) {
        printf("synth_voice_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("synth_voice_test: all passed\n");
    return 0;
}
