/* マスター設定の共有ロジック。仕様と根拠は master_ui.h と docs/results/phase21b.md。 */
#include "master_ui.h"

#include "hostapi_defs.h"

static const masterui_hooks_t *s_hooks;
static bool s_open;
static int s_volume = 50;

/* 上端で始まった押下の保留(結論が出るまでアプリへ渡さない) */
static bool s_holding;
static int s_hold_x, s_hold_y;

/* `-` / `+` の長押し連打 */
static int s_held_delta;          /* 0 = 押していない */
static uint32_t s_held_since;
static uint32_t s_next_repeat_at;

static uint32_t now_ms(void) { return (s_hooks && s_hooks->now_ms) ? s_hooks->now_ms() : 0; }

static void apply_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_volume = v;
    if (s_hooks && s_hooks->set_volume) s_hooks->set_volume(v);
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void masterui_init(const masterui_hooks_t *hooks, int volume)
{
    s_hooks = hooks;
    s_volume = volume;
    s_open = false;
    s_holding = false;
    s_held_delta = 0;
}

bool masterui_is_open(void) { return s_open; }
int masterui_volume(void) { return s_volume; }

void masterui_held_down(int *x, int *y)
{
    if (x) *x = s_hold_x;
    if (y) *y = s_hold_y;
}

void masterui_open(void)
{
    s_open = true;
    s_held_delta = 0;
}

void masterui_close(void)
{
    s_open = false;
    s_held_delta = 0;
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
        int delta = 0;
        if (in_rect(x, y, MASTERUI_MINUS_X, MASTERUI_BTN_Y, MASTERUI_BTN_W, MASTERUI_BTN_H)) delta = -1;
        else if (in_rect(x, y, MASTERUI_PLUS_X, MASTERUI_BTN_Y, MASTERUI_BTN_W, MASTERUI_BTN_H)) delta = 1;
        if (delta != 0) {
            apply_volume(s_volume + delta); /* 押下直後に 1 ステップ */
            s_held_delta = delta;
            s_held_since = now_ms();
            s_next_repeat_at = s_held_since + MASTERUI_HOLD_DELAY_MS;
        }
        return MASTERUI_CONSUME;
    }
    if (type == HOSTAPI_EV_TOUCH_UP) {
        s_held_delta = 0;
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
    if (!s_open || s_held_delta == 0) return false;
    const uint32_t now = now_ms();
    if ((int32_t)(now - s_next_repeat_at) < 0) return false;
    const uint32_t held = now - s_held_since;
    uint32_t interval = MASTERUI_HOLD_INT1_MS;
    if (held >= MASTERUI_HOLD_ACCEL2_MS) interval = MASTERUI_HOLD_INT3_MS;
    else if (held >= MASTERUI_HOLD_ACCEL1_MS) interval = MASTERUI_HOLD_INT2_MS;
    const int before = s_volume;
    apply_volume(s_volume + s_held_delta);
    s_next_repeat_at = now + interval;
    return s_volume != before;
}
