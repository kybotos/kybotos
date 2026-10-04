/* マスター設定の共有ロジック。仕様と根拠は master_ui.h と docs/results/phase21b.md。 */
#include "master_ui.h"

#include "hostapi_defs.h"

static const masterui_hooks_t *s_hooks;
static bool s_open;
static int s_level[MASTERUI_ITEMS];
static bool s_muted[MASTERUI_ITEMS];
static const char *const k_labels[MASTERUI_ITEMS] = { "Master", "MP3", "Synth", "Click" };
static const int k_defaults[MASTERUI_ITEMS] = {
    MASTERUI_DEF_MASTER, MASTERUI_DEF_MP3, MASTERUI_DEF_SYNTH, MASTERUI_DEF_CLICK
};
static const bool k_default_mute[MASTERUI_ITEMS] = { false, false, false, MASTERUI_DEF_MUTE_CLICK };

/* 上端で始まった押下の保留(結論が出るまでアプリへ渡さない) */
static bool s_holding;
static int s_hold_x, s_hold_y;

/* 開いている間の押下(Phase 22e)。押した場所で的が決まり、離すまで変わらない */
typedef enum { TGT_NONE = 0, TGT_NUM, TGT_BAR, TGT_HANDLE } target_t;
static target_t s_target;
static int s_item = -1;           /* 的の行(数字・バー) */
static int s_press_y;             /* 押した y(はじき・押したまま・取っ手) */
static int s_cur_y;               /* 最新の y */
static bool s_rep_on;             /* 押したままの連続変更の最中 */
static int s_repeats;             /* この押下で連続変更した回数(> 0 なら離したときの ±1 は無し) */
static uint32_t s_next_repeat_at;
static int s_blink_phase = -1;    /* 最後に知らせた点滅の相 */

static uint32_t now_ms(void) { return (s_hooks && s_hooks->now_ms) ? s_hooks->now_ms() : 0; }

/* ホストへは実効値(MUTE 中は 0)を渡す。ホストのゲイン計算は MUTE を知らなくてよい */
static void push_level(masterui_item_t item)
{
    if (s_hooks && s_hooks->set_level) s_hooks->set_level(item, masterui_effective(item));
}

static void apply_level(masterui_item_t item, int v)
{
    if (item < 0 || item >= MASTERUI_ITEMS) return;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_level[item] = v;
    push_level(item);
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
    s_target = TGT_NONE;
    s_item = -1;
    for (i = 0; i < MASTERUI_ITEMS; i++) {
        s_muted[i] = k_default_mute[i];
        apply_level((masterui_item_t)i, k_defaults[i]);
    }
}

bool masterui_is_open(void) { return s_open; }

int masterui_level(masterui_item_t item)
{
    if (item < 0 || item >= MASTERUI_ITEMS) return 0;
    return s_level[item];
}

bool masterui_is_muted(masterui_item_t item)
{
    if (item < 0 || item >= MASTERUI_ITEMS) return false;
    return s_muted[item];
}

int masterui_effective(masterui_item_t item)
{
    if (item < 0 || item >= MASTERUI_ITEMS) return 0;
    return s_muted[item] ? 0 : s_level[item];
}

void masterui_set_level(masterui_item_t item, int v) { apply_level(item, v); }

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

static void end_press(void)
{
    s_target = TGT_NONE;
    s_item = -1;
    s_rep_on = false;
    s_repeats = 0;
}

void masterui_open(void)
{
    s_open = true;
    end_press();
}

void masterui_close(void)
{
    s_open = false;
    end_press();
}

/* バーの x → 値(左端 = 0、右端 = 100。四捨五入) */
static int bar_value(int x)
{
    int v = ((x - MASTERUI_BAR_X) * 100 + MASTERUI_BAR_W / 2) / MASTERUI_BAR_W;
    return v < 0 ? 0 : (v > 100 ? 100 : v);
}

/* 押した位置の行(行の間の隙間は下の行に含める)。無ければ -1 */
static int row_at(int y)
{
    int i;
    for (i = 0; i < MASTERUI_ITEMS; i++) {
        const int ry = MASTERUI_ROW_Y(i);
        if (y >= ry - (MASTERUI_ROW_PITCH - MASTERUI_ROW_H) && y < ry + MASTERUI_ROW_H) return i;
    }
    return -1;
}

/* 開いている間の当たり判定。押した場所で意味が決まる */
static masterui_action_t on_touch_open(uint16_t type, int x, int y)
{
    if (type == HOSTAPI_EV_TOUCH_DOWN) {
        end_press();
        if (y >= MASTERUI_BAND_H) {
            masterui_close(); /* 帯の外 = 閉じる(iOS の引き下ろしと同じ感覚) */
            return MASTERUI_CONSUME;
        }
        if (in_rect(x, y, MASTERUI_CLOSE_X, MASTERUI_CLOSE_Y, MASTERUI_CLOSE_W, MASTERUI_CLOSE_H)) {
            masterui_close();
            return MASTERUI_CONSUME;
        }
        if (in_rect(x, y, MASTERUI_HANDLE_HIT_X, MASTERUI_HANDLE_HIT_Y, MASTERUI_HANDLE_HIT_W,
                    MASTERUI_BAND_H - MASTERUI_HANDLE_HIT_Y)) {
            s_target = TGT_HANDLE; /* 取っ手: 上へ払うと閉じる */
            s_press_y = s_cur_y = y;
            return MASTERUI_CONSUME;
        }
        {
            const int i = row_at(y);
            if (i < 0) return MASTERUI_CONSUME;
            if (x >= MASTERUI_MUTE_X && x < MASTERUI_MUTE_X + MASTERUI_MUTE_W) {
                /* ラベル = MUTE のトグル(Phase 21c)。戻せる操作なので 1 タップ、DOWN で即 */
                s_muted[i] = !s_muted[i];
                push_level((masterui_item_t)i);
            } else if (x >= MASTERUI_BAR_HIT_X && x < MASTERUI_BAR_HIT_X + MASTERUI_BAR_HIT_W) {
                /* バー: 押した位置の値にする(タップ)。動かすと追従する(スクラブ) */
                s_target = TGT_BAR;
                s_item = i;
                apply_level((masterui_item_t)i, bar_value(x));
            } else if (x >= MASTERUI_NUM_HIT_X) {
                /* 数字: はじき(UP で ±1)/ 押したまま(tick で ±5 → ±10) */
                s_target = TGT_NUM;
                s_item = i;
                s_press_y = s_cur_y = y;
            }
        }
        return MASTERUI_CONSUME;
    }
    if (type == HOSTAPI_EV_TOUCH_MOVE) {
        s_cur_y = y;
        if (s_target == TGT_BAR) {
            apply_level((masterui_item_t)s_item, bar_value(x));
        } else if (s_target == TGT_HANDLE && s_press_y - y >= MASTERUI_HOLD_PX) {
            masterui_close();
        }
        return MASTERUI_CONSUME; /* MOVE も食う(下のアプリへは渡さない) */
    }
    if (type == HOSTAPI_EV_TOUCH_UP) {
        /* 離した位置は UP に入る(MOVE は間引かれるので、最後の数 px は UP にしか無いことがある。ui-conventions §6) */
        if (s_target == TGT_BAR) {
            apply_level((masterui_item_t)s_item, bar_value(x));
        } else if (s_target == TGT_NUM && s_repeats == 0) {
            const int dy = y - s_press_y;
            if (dy <= -MASTERUI_FLICK_MIN_PX) apply_level((masterui_item_t)s_item, s_level[s_item] + 1);
            else if (dy >= MASTERUI_FLICK_MIN_PX) apply_level((masterui_item_t)s_item, s_level[s_item] - 1);
        } else if (s_target == TGT_HANDLE && s_press_y - y >= MASTERUI_HANDLE_CLOSE_PX) {
            masterui_close();
            return MASTERUI_CONSUME;
        }
        end_press();
        return MASTERUI_CONSUME;
    }
    return MASTERUI_CONSUME;
}

int masterui_editing(void)
{
    return (s_open && (s_target == TGT_NUM || s_target == TGT_BAR)) ? s_item : -1;
}

bool masterui_blink_on(void)
{
    return ((now_ms() / MASTERUI_BLINK_MS) & 1u) == 0;
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
    bool changed = false;
    if (!s_open) return false;
    if (s_target == TGT_NUM) {
        /* 押したままの連続変更(metronome の hold_repeat と同じ規則) */
        const int dy = s_cur_y - s_press_y;
        const int ady = dy < 0 ? -dy : dy;
        const uint32_t now = now_ms();
        if (ady < MASTERUI_HOLD_PX) {
            s_rep_on = false; /* 戻したら止まる(もう一度動かせば最初の ±5 から) */
        } else {
            if (!s_rep_on) {
                s_rep_on = true;
                s_next_repeat_at = now + MASTERUI_HOLD_START_MS;
            }
            if ((int32_t)(now - s_next_repeat_at) >= 0) {
                const int k = s_repeats < MASTERUI_REPEAT_TO_10 ? 5 : 10;
                const int before = s_level[s_item];
                s_repeats++;
                s_next_repeat_at = now + MASTERUI_REPEAT_MS;
                apply_level((masterui_item_t)s_item, before + (dy < 0 ? k : -k));
                changed = s_level[s_item] != before;
            }
        }
    }
    if (masterui_editing() >= 0) {
        const int phase = masterui_blink_on() ? 1 : 0;
        if (phase != s_blink_phase) {
            s_blink_phase = phase;
            changed = true;
        }
    } else if (s_blink_phase != -1) {
        s_blink_phase = -1; /* 点滅をやめた(通常色に戻す) */
        changed = true;
    }
    return changed;
}
