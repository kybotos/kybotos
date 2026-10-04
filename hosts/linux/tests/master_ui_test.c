/* shared/master_ui.c の単体テスト(Phase 22e)。偽の時計で、開いている間の値の変え方を決定的に確かめる:
 * 数字のはじき(±1)と押したまま(±5 → ±10)、バーのタップとスクラブ、ラベルの MUTE、取っ手 / ✕ / 帯の外で閉じる。
 * SDL / WAMR には依存しない。実行: ctest --test-dir build --output-on-failure */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hostapi_defs.h"
#include "master_ui.h"

static uint32_t g_now = 1000;
static int g_pushed[MASTERUI_ITEMS];
static int g_fail;

static uint32_t fake_now(void) { return g_now; }
static void fake_set_level(masterui_item_t item, int v) { g_pushed[item] = v; }
static const masterui_hooks_t k_hooks = { fake_set_level, fake_now };

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            g_fail++;                                     \
        }                                                 \
    } while (0)

static void down(int x, int y) { masterui_on_touch(HOSTAPI_EV_TOUCH_DOWN, x, y); }
static void move(int x, int y) { masterui_on_touch(HOSTAPI_EV_TOUCH_MOVE, x, y); }
static void up(int x, int y) { masterui_on_touch(HOSTAPI_EV_TOUCH_UP, x, y); }
static int row_mid(int i) { return MASTERUI_ROW_Y(i) + MASTERUI_ROW_H / 2; }
static void advance(uint32_t ms)
{
    uint32_t t;
    for (t = 0; t < ms; t += 50) {
        g_now += 50;
        masterui_tick();
    }
}
static void reset(void)
{
    masterui_init(&k_hooks);
    masterui_open();
}

static void test_flick(void)
{
    const int x = 300, y = row_mid(MASTERUI_MASTER);
    reset();
    down(x, y); move(x, y - 10); up(x, y - 20); /* 上へ 20px: +1 */
    CHECK(masterui_level(MASTERUI_MASTER) == MASTERUI_DEF_MASTER + 1, "flick up: %d", masterui_level(MASTERUI_MASTER));
    down(x, y); up(x, y + 30); /* 下へ: −1(MOVE が無くても UP の座標で測る) */
    CHECK(masterui_level(MASTERUI_MASTER) == MASTERUI_DEF_MASTER, "flick down: %d", masterui_level(MASTERUI_MASTER));
    down(x, y); up(x, y - 10); /* 16px 未満は何もしない */
    CHECK(masterui_level(MASTERUI_MASTER) == MASTERUI_DEF_MASTER, "small move: %d", masterui_level(MASTERUI_MASTER));
    CHECK(g_pushed[MASTERUI_MASTER] == MASTERUI_DEF_MASTER, "pushed: %d", g_pushed[MASTERUI_MASTER]);
}

static void test_hold(void)
{
    const int x = 300, y = row_mid(MASTERUI_MP3);
    int v0;
    reset();
    v0 = masterui_level(MASTERUI_MP3);
    down(x, y); move(x, y - 30);
    advance(350);
    CHECK(masterui_level(MASTERUI_MP3) == v0, "before 400ms: %d", masterui_level(MASTERUI_MP3));
    advance(100); /* 400ms: +5 */
    CHECK(masterui_level(MASTERUI_MP3) == v0 + 5, "first repeat: %d", masterui_level(MASTERUI_MP3));
    advance(1200); /* +5 × 3 → 4 回目までが ±5 */
    CHECK(masterui_level(MASTERUI_MP3) == v0 + 20, "four repeats: %d", masterui_level(MASTERUI_MP3));
    advance(400); /* 5 回目は ±10 */
    CHECK(masterui_level(MASTERUI_MP3) == v0 + 30, "fifth repeat: %d", masterui_level(MASTERUI_MP3));
    move(x, y - 10); advance(1000); /* 24px 以内に戻すと止まる */
    CHECK(masterui_level(MASTERUI_MP3) == v0 + 30, "stopped: %d", masterui_level(MASTERUI_MP3));
    up(x, y - 20); /* 連続変更をした押下では離したときの ±1 は無い */
    CHECK(masterui_level(MASTERUI_MP3) == v0 + 30, "no flick after hold: %d", masterui_level(MASTERUI_MP3));
    down(x, y); move(x, y - 40); advance(10000); up(x, y - 40); /* 上限 100 で止まる */
    CHECK(masterui_level(MASTERUI_MP3) == 100, "clamped: %d", masterui_level(MASTERUI_MP3));
}

static void test_bar(void)
{
    const int y = row_mid(MASTERUI_SYNTH);
    reset();
    down(MASTERUI_BAR_X + MASTERUI_BAR_W * 7 / 10, y); /* タップした位置の値 */
    CHECK(masterui_level(MASTERUI_SYNTH) == 70, "tap: %d", masterui_level(MASTERUI_SYNTH));
    CHECK(masterui_editing() == MASTERUI_SYNTH, "editing: %d", masterui_editing());
    up(MASTERUI_BAR_X + MASTERUI_BAR_W * 7 / 10, y);
    CHECK(masterui_editing() == -1, "editing after up: %d", masterui_editing());
    down(MASTERUI_BAR_X + MASTERUI_BAR_W / 2, y); /* スクラブ: 指に追従し、離した位置で確定 */
    move(MASTERUI_BAR_X + MASTERUI_BAR_W / 4, y + 20); /* 行の外へ縦にずれても同じ行のまま */
    CHECK(masterui_level(MASTERUI_SYNTH) == 25, "scrub: %d", masterui_level(MASTERUI_SYNTH));
    up(MASTERUI_BAR_X - 5, y); /* 左端より外は 0 */
    CHECK(masterui_level(MASTERUI_SYNTH) == 0, "scrub to the left end: %d", masterui_level(MASTERUI_SYNTH));
    down(MASTERUI_BAR_X + MASTERUI_BAR_W + 4, y); up(MASTERUI_BAR_X + MASTERUI_BAR_W + 4, y); /* 右端より外は 100 */
    CHECK(masterui_level(MASTERUI_SYNTH) == 100, "right end: %d", masterui_level(MASTERUI_SYNTH));
}

static void test_mute_and_close(void)
{
    reset();
    CHECK(masterui_is_muted(MASTERUI_CLICK), "click muted by default");
    down(30, row_mid(MASTERUI_CLICK)); up(30, row_mid(MASTERUI_CLICK));
    CHECK(!masterui_is_muted(MASTERUI_CLICK), "label toggles mute");
    CHECK(g_pushed[MASTERUI_CLICK] == MASTERUI_DEF_CLICK, "unmuted level pushed: %d", g_pushed[MASTERUI_CLICK]);

    down(MASTERUI_CLOSE_X + 10, 10);
    CHECK(!masterui_is_open(), "close by the cross");
    masterui_open();
    down(100, MASTERUI_BAND_H + 5);
    CHECK(!masterui_is_open(), "close by tapping outside");
    masterui_open();
    down(160, MASTERUI_HANDLE_Y); up(160, MASTERUI_HANDLE_Y - 10);
    CHECK(masterui_is_open(), "handle: small move keeps it open");
    down(160, MASTERUI_HANDLE_Y); up(160, MASTERUI_HANDLE_Y - 20);
    CHECK(!masterui_is_open(), "handle: flick up closes");
    masterui_open();
    down(160, MASTERUI_HANDLE_Y); move(160, MASTERUI_HANDLE_Y - 30);
    CHECK(!masterui_is_open(), "handle: dragging up closes");
}

static void test_blink(void)
{
    const int x = 300, y = row_mid(MASTERUI_MASTER);
    int toggles = 0;
    uint32_t t;
    reset();
    down(x, y);
    for (t = 0; t < 1000; t += 50) {
        g_now += 50;
        if (masterui_tick()) toggles++;
    }
    CHECK(toggles >= 4 && toggles <= 6, "blink toggles in 1s: %d", toggles);
    up(x, y);
    CHECK(masterui_tick(), "tick reports the end of blinking");
    CHECK(!masterui_tick(), "then quiet");
}

int main(void)
{
    test_flick();
    test_hold();
    test_bar();
    test_mute_and_close();
    test_blink();
    if (g_fail) {
        printf("master_ui_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("master_ui_test: all passed\n");
    return 0;
}
