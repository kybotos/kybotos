/*
 * マスター設定(Phase 21b)— 実機ホストと Linux ホストで共有するロジック。
 *
 * **入口も状態もホストが持ち、アプリは一切知らない。** アプリの `.wasm` は変わらない。
 *
 * ここに置くのは「どのホストでも同じでなければならないもの」だけ:
 *   - 上端からの下方向スワイプの判定(= 画面外から入ってきたスワイプ)
 *   - 開閉の状態、音量の値、値の変え方(数字のはじき / 押したまま、バーのタップ / スクラブ。Phase 22e)
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
 * **実効音量 = マスター × チャンネル × (MUTE ? 0 : 1)**(マスターとチャンネルは 0..100)。
 * チャンネルは**ポート単位**で、`architecture.md` §7 のポート抽象とそろえてある。
 * **MUTE は値とは別のフラグ**(Phase 21c)。アンミュートで元の値に戻る。 */
typedef enum {
    MASTERUI_MASTER = 0, /* 全体 */
    MASTERUI_MP3,        /* MP3 再生 */
    MASTERUI_SYNTH,      /* 内蔵音源(SYNTH ポート。21c で DRUM から改名) */
    MASTERUI_CLICK,      /* クリック(CLICK ポート) */
    MASTERUI_ITEMS
} masterui_item_t;

/* 既定値。MP3 の実効は 50 * 0.35 = 17.5(実機の試聴で「18 くらい」だったのに合わせた) */
#define MASTERUI_DEF_MASTER 50
#define MASTERUI_DEF_MP3    35
#define MASTERUI_DEF_SYNTH  100
#define MASTERUI_DEF_CLICK  100
/* MUTE の既定(Phase 21c)。**Click は「機能的なクリック」なので既定で鳴らさない** */
#define MASTERUI_DEF_MUTE_CLICK true

/* ---- オーバーレイの形と座標(論理 320x240。Phase 22e で作り直した)----
 *
 * 帯は「枠に入った板」: 黒の地を中緑の線(2px)で囲み、題名 `Settings` と ✕ を枠の上辺に食い込ませる。
 * 行は `[ラベル] [バー] [数字]`(ラベルは文字だけ。タップ = MUTE)、行の間に薄い区切り線、枠の内側の下端に取っ手。
 * **描画と当たり判定で同じ定数を使う**(両ホストで見た目と操作を揃えるため) */
#define MASTERUI_FRAME_X   4
#define MASTERUI_FRAME_Y   8
#define MASTERUI_FRAME_W   312
#define MASTERUI_FRAME_H   178
#define MASTERUI_FRAME_T   2    /* 枠の線の太さ */
#define MASTERUI_BAND_H    (MASTERUI_FRAME_Y + MASTERUI_FRAME_H + 2) /* 黒で覆う高さ。これより下のタップは閉じる */
/* 枠の上辺に食い込ませる題名と ✕(文字の後ろを黒で抜いて線を切る) */
#define MASTERUI_TITLE_X   20
#define MASTERUI_TITLE_Y   1
#define MASTERUI_CLOSE_SYM_X 288
#define MASTERUI_CLOSE_X   272  /* ✕ の当たり判定 */
#define MASTERUI_CLOSE_Y   0
#define MASTERUI_CLOSE_W   48
#define MASTERUI_CLOSE_H   30
#define MASTERUI_HINT_X    16
#define MASTERUI_HINT_Y    16
/* 行 */
#define MASTERUI_ROW0_Y    36   /* 1 行目の上端 */
#define MASTERUI_ROW_PITCH 34
#define MASTERUI_ROW_H     30
#define MASTERUI_LABEL_X   16
#define MASTERUI_MUTE_X    8    /* ラベル = MUTE のトグルの当たり判定 */
#define MASTERUI_MUTE_W    78
#define MASTERUI_BAR_X     90
#define MASTERUI_BAR_W     172
#define MASTERUI_BAR_H     10
#define MASTERUI_BAR_HIT_X 84   /* バーの当たり判定(行の高さ全体): x 84〜270 */
#define MASTERUI_BAR_HIT_W 186
#define MASTERUI_NUM_RIGHT 302  /* 数字は右寄せ */
#define MASTERUI_NUM_HIT_X 270  /* 数字の当たり判定: x 270〜320 */
#define MASTERUI_SEP_X     14   /* 行の間の区切り線 */
#define MASTERUI_SEP_W     292
/* 取っ手(枠の内側の下端の中央)。上へ払うと閉じる */
#define MASTERUI_HANDLE_X  140
#define MASTERUI_HANDLE_Y  (MASTERUI_FRAME_Y + MASTERUI_FRAME_H - 9)
#define MASTERUI_HANDLE_W  40
#define MASTERUI_HANDLE_H  3
#define MASTERUI_HANDLE_HIT_X 90
#define MASTERUI_HANDLE_HIT_W 140
#define MASTERUI_HANDLE_HIT_Y (MASTERUI_ROW0_Y + MASTERUI_ITEMS * MASTERUI_ROW_PITCH - 2)

/* 行 i の上端 y */
#define MASTERUI_ROW_Y(i) (MASTERUI_ROW0_Y + (i) * MASTERUI_ROW_PITCH)

/* ---- 配色(Phase 22e。メニュー shared/launcher_theme.h とアプリ wasm-apps/appui の theme に揃える)---- */
#define MASTERUI_RGB_BG      0x000000
#define MASTERUI_RGB_FRAME   0x3e6648 /* 枠: 中緑 */
#define MASTERUI_RGB_TEXT    0xf3f1e4 /* 題名・ラベル・数字: クリーム */
#define MASTERUI_RGB_HINT    0x5f7466 /* 手引き・取っ手: くすんだ緑 */
#define MASTERUI_RGB_CLOSE   0xf06060 /* ✕ */
#define MASTERUI_RGB_MUTED   0x707a74 /* MUTE 中の文字とバー */
#define MASTERUI_RGB_EDIT    0xffe060 /* 値の変更中(点滅。ui-conventions §4) */
#define MASTERUI_RGB_TRACK   0x1c2620 /* バーの地・行の区切り線 */
#define MASTERUI_RGB_FILL    0x8fd18b /* バー: 若葉 */
#define MASTERUI_CLOSE_SYM   "\xEF\x80\x8D" /* U+F00D ✕(実機のフォントにあり、Linux ホストは図形で描く) */

/* ---- 値の変え方(Phase 22e。metronome の BPM と同じ。docs/apps/metronome/spec.md §3)----
 * 数字を上下に**はじく**(押して動かして離す)と、離したときに ±1(上 = 増。移動は UP の座標で測る)。
 * **押したまま**上下に HOLD_PX 以上動かして置くと、HOLD_START_MS 後から REPEAT_MS ごとに ±5、
 * REPEAT_TO_10 回続くと ±10。HOLD_PX 以内に戻すと止まる。連続変更をした押下では離したときの ±1 は足さない。
 * バーは**タップでその位置の値**、押したまま左右に動かすと指に追従する(スクラブ) */
#define MASTERUI_FLICK_MIN_PX   16
#define MASTERUI_HOLD_PX        24
#define MASTERUI_HOLD_START_MS  400
#define MASTERUI_REPEAT_MS      400
#define MASTERUI_REPEAT_TO_10   4
#define MASTERUI_HANDLE_CLOSE_PX 16 /* 取っ手をこれだけ上へ払ったら閉じる */
#define MASTERUI_BLINK_MS       200 /* 変更中の点滅の周期 */

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
    /* 設定項目の**実効値**をホストへ反映する(0..100。MUTE 中は 0)。起動時と変更時に呼ばれる */
    void (*set_level)(masterui_item_t item, int v);
    /* 起動からの経過ミリ秒 */
    uint32_t (*now_ms)(void);
} masterui_hooks_t;

/* 起動時に 1 回。hooks は静的寿命であること。既定値がそのままホストへ反映される */
void masterui_init(const masterui_hooks_t *hooks);

bool masterui_is_open(void);
/* 項目の値(範囲外は 0)。MUTE 中でも値そのものを返す */
int  masterui_level(masterui_item_t item);
/* MUTE 中か(Phase 21c) */
bool masterui_is_muted(masterui_item_t item);
/* ホストへ渡す実効値 = MUTE ? 0 : 値 */
int  masterui_effective(masterui_item_t item);
/* 値を外から設定する(hostapi_audio_set_volume が Master に使う)。MUTE は変えない */
void masterui_set_level(masterui_item_t item, int v);
/* 画面に出すラベル(ASCII) */
const char *masterui_label(masterui_item_t item);
/* 保留中の DOWN の座標(MASTERUI_FLUSH_THEN_PASS のときにホストが使う) */
void masterui_held_down(int *x, int *y);

/* 生のタッチを先に食わせる。戻り値に従ってアプリへ配送するかを決める。
 * type は HOSTAPI_EV_TOUCH_*(hostapi_defs.h) */
masterui_action_t masterui_on_touch(uint16_t type, int x, int y);

/* 毎フレーム(またはタイマで)呼ぶ。数字の押したままの連続変更を進める。
 * 音量が変わったか、変更中の点滅が切り替わったら true を返す(ホストは再描画すればよい) */
bool masterui_tick(void);
/* 値を変えている項目(数字を押している / バーを触っている)。無ければ -1(Phase 22e) */
int  masterui_editing(void);
/* 変更中の点滅の今の相(true = 編集色)。masterui_editing() >= 0 のときだけ意味がある */
bool masterui_blink_on(void);

/* メニューの行などから明示的に開く / 閉じる */
void masterui_open(void);
void masterui_close(void);

#ifdef __cplusplus
}
#endif
