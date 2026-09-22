/* マスター設定の共有ロジック。仕様と根拠は master_ui.h と docs/results/phase21b.md。 */
#include "master_ui.h"

#include "hostapi_defs.h"

static const masterui_hooks_t *s_hooks;
static bool s_open;
static int s_level[MASTERUI_ITEMS];
static const char *const k_labels[MASTERUI_ITEMS] = { "Master", "MP3", "Drums", "Click" };
static const int k_defaults[MASTERUI_ITEMS] = {
    MASTERUI_DEF_MASTER, MASTERUI_DEF_MP3, MASTERUI_DEF_DRUM, MASTERUI_DEF_CLICK
};

/* 上端で始まった押下の保留(結論が出るまでアプリへ渡さない) */
static bool s_holding;
static int s_hold_x, s_hold_y;

/* `-` / `+` の長押し連打 */
static int s_held_item = -1;      /* 押している行 */
static int s_held_delta;          /* 0 = 押していない */
static uint32_t s_held_since;
static uint32_t s_next_repeat_at;

static uint32_t now_ms(void) { return (s_hooks && s_hooks->now_ms) ? s_hooks->now_ms() : 0; }

static void apply_level(masterui_item_t item, int v)
{
    if (item < 0 || item >= MASTERUI_ITEMS) return;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_level[item] = v;
    if (s_hooks && s_hooks->set_level) s_hooks->set_level(item, v);
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void masterui_init(const masterui_hooks_t *hooks)
{
    int i;
    s_hooks = hooks;
    s_open = false;
    s_holding = false;
    s_held_item = -1;
    s_held_delta = 0;
    for (i = 0; i < MASTERUI_ITEMS; i++) apply_level((masterui_item_t)i, k_defaults[i]);
}

bool masterui_is_open(void) { return s_open; }

int masterui_level(masterui_item_t item)
{
    if (item < 0 || item >= MASTERUI_ITEMS) return 0;
    return s_level[item];
}

const char *masterui_label(masterui_item_t item)
{
    if (item < 0 || item >= MASTERUI_ITEMS) return "";
    return k_labels[item];
}

void masterui_held_down(int *x, int *y)
{
    if (x) *x = s_hold_x;
    if (y) *y = s_hold_y;
}

void masterui_open(void)
{
    s_open = true;
    s_held_delta = 0;
    s_held_item = -1;
}

void masterui_close(void)
{
    s_open = false;
    s_held_delta = 0;
    s_held_item = -1;
}

/* 開いている間の当たり判定。押した場所で意味が決まる */
static masterui_action_t on_touch_open(uint16_t type, int x, int y)
{
    if (type == HOSTAPI_EV_TOUCH_DOWN) {
        if (y >= MASTERUI_BAND_H) {
            masterui_close(); /* 帯の外 = 閉じる(iOS の引き下ろしと同じ感覚) */
            return MASTERUI_CONSUME;
        }
        if (in_rect(x, y, MASTERUI_CLOSE_X, MASTERUI_CLOSE_Y, MASTERUI_CLOSE_W, MASTERUI_CLOSE_H)) {
            masterui_close();
            return MASTERUI_CONSUME;
        }
        {
            int i;
            for (i = 0; i < MASTERUI_ITEMS; i++) {
                const int ry = MASTERUI_ROW_Y(i);
                int delta = 0;
                if (in_rect(x, y, MASTERUI_MINUS_X, ry, MASTERUI_BTN_W, MASTERUI_ROW_H)) delta = -1;
                else if (in_rect(x, y, MASTERUI_PLUS_X, ry, MASTERUI_BTN_W, MASTERUI_ROW_H)) delta = 1;
                if (delta != 0) {
                    apply_level((masterui_item_t)i, s_level[i] + delta); /* 押下直後に 1 ステップ */
                    s_held_item = i;
                    s_held_delta = delta;
                    s_held_since = now_ms();
                    s_next_repeat_at = s_held_since + MASTERUI_HOLD_DELAY_MS;
                    break;
                }
            }
        }
        return MASTERUI_CONSUME;
    }
    if (type == HOSTAPI_EV_TOUCH_UP) {
        s_held_delta = 0;
        s_held_item = -1;
        return MASTERUI_CONSUME;
    }
    return MASTERUI_CONSUME; /* MOVE も食う(下のアプリへは渡さない) */
}

masterui_action_t masterui_on_touch(uint16_t type, int x, int y)
{
    if (s_open) return on_touch_open(type, x, y);

    if (type == HOSTAPI_EV_TOUCH_DOWN) {
        if (y <= MASTERUI_EDGE_PX) {
            /* 画面外から入ってきたかもしれない。結論が出るまで保留する */
            s_holding = true;
            s_hold_x = x;
            s_hold_y = y;
            return MASTERUI_HOLD;
        }
        return MASTERUI_PASS;
    }

    if (!s_holding) return MASTERUI_PASS;

    if (type == HOSTAPI_EV_TOUCH_MOVE) {
        const int dy = y - s_hold_y;
        const int dx = x - s_hold_x;
        const int adx = dx < 0 ? -dx : dx;
        if (dy >= MASTERUI_PULL_PX && dy > adx) {
            /* 下へ引かれた = マスター設定を開く。この押下は最後までアプリへ渡さない */
            s_holding = false;
            masterui_open();
            return MASTERUI_CONSUME;
        }
        /* **デッドゾーンで判断を打ち切らない。** MOVE は 8px 刻みで届くので、
         * まだ引ける途中なら保留を続ける(Phase 18a で踏んだのと同じ罠)。
         * 諦めるのは「上へ動いた」「明らかに横へ動いた」ときだけ */
        if (dy < 0 || adx > dy + MASTERUI_SIDE_SLACK_PX) {
            s_holding = false;
            return MASTERUI_FLUSH_THEN_PASS;
        }
        return MASTERUI_HOLD;
    }

    /* UP: 上端のタップだった。保留していた DOWN を流してから UP を流す */
    s_holding = false;
    return MASTERUI_FLUSH_THEN_PASS;
}

bool masterui_tick(void)
{
    if (!s_open || s_held_delta == 0 || s_held_item < 0) return false;
    const uint32_t now = now_ms();
    if ((int32_t)(now - s_next_repeat_at) < 0) return false;
    const uint32_t held = now - s_held_since;
    uint32_t interval = MASTERUI_HOLD_INT1_MS;
    if (held >= MASTERUI_HOLD_ACCEL2_MS) interval = MASTERUI_HOLD_INT3_MS;
    else if (held >= MASTERUI_HOLD_ACCEL1_MS) interval = MASTERUI_HOLD_INT2_MS;
    const int before = s_level[s_held_item];
    apply_level((masterui_item_t)s_held_item, before + s_held_delta);
    s_next_repeat_at = now + interval;
    return s_level[s_held_item] != before;
}
