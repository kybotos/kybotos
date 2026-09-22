/*
 * マスター設定(Phase 21b)— 実機ホストと Linux ホストで共有するロジック。
 *
 * **入口も状態もホストが持ち、アプリは一切知らない。** アプリの `.wasm` は変わらない。
 *
 * ここに置くのは「どのホストでも同じでなければならないもの」だけ:
 *   - 上端からの下方向スワイプの判定(= 画面外から入ってきたスワイプ)
 *   - 開閉の状態、音量の値、`-` / `+` の長押し連打加速
 *   - オーバーレイの座標と当たり判定(両ホストで見た目と操作を揃えるため)
 * **描画はホストが行う**(実機は LVGL、Linux は SDL)。
 *
 * 上端の押下を「保留」する理由(docs/results/phase21b.md 0-b):
 *   DOWN を先にアプリへ配送してしまうと、エッジスワイプだと分かった時点で
 *   **アプリの押下を取り消す手段が無い**(ABI に cancel が無い)。取り消せないと
 *   appui::Gesture が離されないまま 600ms で長押しを発火してしまう。
 *   そこで **y <= MASTERUI_EDGE_PX の DOWN だけ**を結論が出るまで保留する。
 *   保留するのは上端の数 px だけなので、通常の操作は 1 つも遅れない。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ---- 上端スワイプの判定 ---- */
/* この y 以下で始まった押下だけを「画面外から来たかもしれない」として保留する。
 * 座標変換は画面外をクリップして 0 に張り付かせるので、0 付近が「上端またはその上」 */
#define MASTERUI_EDGE_PX 3
/* 下へこれだけ動いたらマスター設定を開く(ui-conventions の SWIPE_MIN_MOVE と同値) */
#define MASTERUI_PULL_PX 24
/* 横へこれ以上ぶれたら「下スワイプではない」と判断する(TAP_MAX_MOVE と同値)。
 * **これ未満なら保留を続ける** — MOVE は刻んで届くので、途中で打ち切ると
 * 閾値に届く前にジェスチャが死ぬ(Phase 18a と同じ罠) */
#define MASTERUI_SIDE_SLACK_PX 12

/* ---- 設定項目(Phase 21b。追記でミキサーを足した)----
 *
 * **実効音量 = マスター × チャンネル**(どちらも 0..100)。
 * チャンネルは**ポート単位**で、`architecture.md` §7 のポート抽象とそろえてある。 */
typedef enum {
    MASTERUI_MASTER = 0, /* 全体 */
    MASTERUI_MP3,        /* MP3 再生 */
    MASTERUI_DRUM,       /* 内蔵音源(SYNTH ポート) */
    MASTERUI_CLICK,      /* クリック(CLICK ポート) */
    MASTERUI_ITEMS
} masterui_item_t;

/* 既定値。MP3 の実効は 50 * 0.35 = 17.5(実機の試聴で「18 くらい」だったのに合わせた) */
#define MASTERUI_DEF_MASTER 50
#define MASTERUI_DEF_MP3    35
#define MASTERUI_DEF_DRUM   100
#define MASTERUI_DEF_CLICK  100

/* ---- オーバーレイの座標(論理 320x240)---- */
#define MASTERUI_ROW0_Y   34   /* 1 行目の上端 */
#define MASTERUI_ROW_PITCH 34
#define MASTERUI_ROW_H    30
#define MASTERUI_BAND_H   (MASTERUI_ROW0_Y + MASTERUI_ITEMS * MASTERUI_ROW_PITCH + 6)
#define MASTERUI_CLOSE_X  284
#define MASTERUI_CLOSE_Y  4
#define MASTERUI_CLOSE_W  32
#define MASTERUI_CLOSE_H  28
#define MASTERUI_LABEL_X  12
#define MASTERUI_MINUS_X  90
#define MASTERUI_PLUS_X   185
#define MASTERUI_BTN_W    40
#define MASTERUI_VALUE_X  142
#define MASTERUI_BAR_X    235
#define MASTERUI_BAR_W    75
#define MASTERUI_BAR_H    10

/* 行 i の上端 y */
#define MASTERUI_ROW_Y(i) (MASTERUI_ROW0_Y + (i) * MASTERUI_ROW_PITCH)

/* ---- 長押し連打加速(metronome の BPM± と同じ。Phase 7D の実績値)---- */
#define MASTERUI_HOLD_DELAY_MS   500
#define MASTERUI_HOLD_ACCEL1_MS  1500
#define MASTERUI_HOLD_ACCEL2_MS  3000
#define MASTERUI_HOLD_INT1_MS    400
#define MASTERUI_HOLD_INT2_MS    200
#define MASTERUI_HOLD_INT3_MS    100

#ifdef __cplusplus
extern "C" {
#endif

/* ホストが生のタッチを食わせたときの指示 */
typedef enum {
    /* そのままアプリへ配送してよい */
    MASTERUI_PASS = 0,
    /* この押下は保留する(アプリへ配送しない)。結論が出るまで持っておく */
    MASTERUI_HOLD,
    /* マスター設定が食った。アプリへは配送しない */
    MASTERUI_CONSUME,
    /* 保留していた DOWN を先に配送し、続けて今回のイベントも配送する */
    MASTERUI_FLUSH_THEN_PASS,
} masterui_action_t;

typedef struct {
    /* 設定項目の値をホストへ反映する(0..100)。起動時と変更時に呼ばれる */
    void (*set_level)(masterui_item_t item, int v);
    /* 起動からの経過ミリ秒 */
    uint32_t (*now_ms)(void);
} masterui_hooks_t;

/* 起動時に 1 回。hooks は静的寿命であること。既定値がそのままホストへ反映される */
void masterui_init(const masterui_hooks_t *hooks);

bool masterui_is_open(void);
/* 項目の値(範囲外は 0) */
int  masterui_level(masterui_item_t item);
/* 画面に出すラベル(ASCII) */
const char *masterui_label(masterui_item_t item);
/* 保留中の DOWN の座標(MASTERUI_FLUSH_THEN_PASS のときにホストが使う) */
void masterui_held_down(int *x, int *y);

/* 生のタッチを先に食わせる。戻り値に従ってアプリへ配送するかを決める。
 * type は HOSTAPI_EV_TOUCH_*(hostapi_defs.h) */
masterui_action_t masterui_on_touch(uint16_t type, int x, int y);

/* 毎フレーム(またはタイマで)呼ぶ。`-` / `+` の長押し連打を進める。
 * 音量が変わったら true を返す(ホストは再描画すればよい) */
bool masterui_tick(void);

/* メニューの行などから明示的に開く / 閉じる */
void masterui_open(void);
void masterui_close(void);

#ifdef __cplusplus
}
#endif
