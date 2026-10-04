/* 内蔵音源のボイス(Phase 23)。実機・Linux の両ホストで共有する。
 *
 * CLICK ポート / tone_play の減衰サインと、SYNTH ポート(内蔵音源。Phase 21)の
 * ドラムとメトロノームの音を、HOSTAPI_SYNTH_VOICES 本のボイスで鳴らす。
 * 合成はキャッシュレス(サインは再帰振動子、ノイズは xorshift32)で、サンプルごとに
 * libm を呼ばない(expf / powf / cosf / sinf は発音の開始時と、Kick のブロックごとだけ)。
 *
 * ここに置くのはボイスの生成と描き出しだけ。ミキサ(ブロックの長さ、発音要求の受け渡し、
 * MP3 との排他、起動音、クリップと出力)はホストに残す。
 *
 * - 状態(ボイスと発音の通し番号)はこのファイルの静的変数。**呼び出しは 1 つのスレッドから**
 *   (両ホストともミキサのスレッド / タスク。発音要求はホストが受け渡す)。
 * - 音量は**発音の開始時に焼き込む**。鳴っている音にはマスター音量の変更は効かない。
 * - 契約(note の割り当て、同時発音数、奪取の規則)は shared/hostapi_defs.h の SYNTH ポート。
 * - 式と定数は Phase 21 / 21c で決めたもの(docs/results/phase21.md 0-d / 0-f、phase21c.md)。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTHV_RATE 44100 /* 両ホストのミキサのレート(Hz) */

/* 全ボイスを止め、発音の通し番号を 0 に戻す(アプリの破棄、transport_stop、MP3 との受け渡し) */
void synthv_reset(void);

/* CLICK ポート / tone_play。freq_hz / dur_ms / level(0..100)はトーンの定義そのまま。
 * master_vol / gain_click は 0..100(マスター音量と CLICK のゲイン。MUTE は 0 で渡す) */
void synthv_start_tone(uint16_t freq_hz, uint16_t dur_ms, uint8_t level, int master_vol, int gain_click);

/* SYNTH ポート。note は HOSTAPI_SYNTH_NOTE_*。未知の note は何もしない。
 * master_vol / gain_synth は 0..100 */
void synthv_start_drum(uint8_t note, uint8_t velocity, int master_vol, int gain_synth);

/* 鳴っている全ボイスの n フレームを acc に**足す**(acc の初期化は呼び出し側)。
 * 呼んだ時点で鳴っているボイスがあれば true。n は任意(ブロックを分けて呼んでよい) */
bool synthv_render(int32_t* acc, int n);

#ifdef __cplusplus
} /* extern "C" */
#endif
