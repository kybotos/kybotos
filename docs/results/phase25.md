# Phase 25 実施記録: Waveshare のバッテリー電圧の検知

- 指示書: `docs/prompts/phase25.md`(追記 2026-11-03 を含む)
- 状態: **完了(2026-11-03)**。Host API / ABI・`.wasm` は不変。「電池なし」は電圧では判定できない(指示書の追記 その 2)

---

## ステップ 0: 確認(2026-11-03)

### a. 回路図と公式デモ(資料)

指示書の追記のとおり: GPIO8 = `BAT_ADC`、分圧は 200kΩ / 100kΩ(ADC の電圧 = 電池電圧 / 3)、充電 IC は ETA6098(STAT は LED へ。ESP32 には出ていない見込み)、
公式デモの補正係数は V1 の Arduino 版 0.990476・V2 の ESP-IDF 版 0.9945。資料は `captures/phase25/waveshare-docs/`(git の対象外)。

### b. 実測

一時のコード(`PHASE25-TEMP`。撤去済み)で、ADC1_CH7(GPIO8)、減衰 12dB、curve fitting の校正、64 回平均を 1 秒ごとに読み、ログと画面(左下)に出した。
テスターで電池コネクタの電圧を測った(ユーザー)。ログは `captures/phase25/usb-nobatt.log`(USB のときのログ。ファイル名は測定の途中の名前のまま)。

| 状態 | テスター | ADC の校正値 → ×3 | 生の値の 64 回平均(1 回ごとの幅) |
|---|---|---|---|
| USB + 電池(充電側) | 4.10V | 4,107〜4,116 mV(画面 410X) | 1,592〜1,595(1,542〜1,633) |
| 電池のみ(電源キーで起動、USB なし) | 4.03〜4.05V | 画面 403X 前後 | — |
| 電池のみ + mp3player で再生 | 3.99〜4.00V | 画面で一致、下がりは見えない | — |
| **USB のみ・電池を外した** | **4.18V** | **4,194〜4,200 mV** | 1,626〜1,629(1,577〜1,669) |

- **換算は校正値 × 3 で合う**(差は 0.4% 以内。テスターの分解能は 10mV)。補正係数は 1.000 でよい。
- **音の負荷**(mp3player の再生中)の電圧の落ち込みは、画面で見える範囲(約 10mV)では無かった。
- **USB 給電中は電池のみより約 50mV 高い**(充電の電圧が乗る)。
- **電池を外して USB だけにすると 4.19〜4.20V に張り付く**(充電 IC の出力)。**満充電の電池を USB につないだときと区別がつかない**
  → **電圧だけでは「電池なし」を判定できない**(ゲート 2 の提案に反映)。
- 1 回ごとの読みは ±40 ほど揺れる(64 回平均なら ±2 程度)ので、平均は必須。

### c. ゲート 2(承認済み)

換算 = 校正値 × 3(補正 1.000)、64 回平均、暫定の LiPo の表(4200→100 … 3300→0)、電池なしは電圧で判定しない。指示書の追記(その 2)のとおり。

## ステップ 1: 実装

- **ボードの記述**: `board_pins.hpp` に `KB_HAS_BATT_SENSE`(既定 0)。1 のボードは `PIN_BAT_ADC` と `KB_BAT_DIV_MUL` が無いとビルドが止まる。
  Waveshare は `KB_HAS_BATT_SENSE 1` / `PIN_BAT_ADC GPIO_NUM_8` / `KB_BAT_DIV_MUL 3`、CrowPanel は 0。
- **`src/components/battery/`**(`battery.hpp` / `battery.cpp`): 取り出す口は `battery::Init()`(タスクを 1 つ起動。電池の検知が無いボードでは何もしない)、
  `battery::Get(Reading*)`(mV と %。ロックなし、どのタスクからでも。最初のサンプルまでは false)、`battery::PercentFromMv(mv)`(純関数)。
  ADC は oneshot(`adc_oneshot_io_to_channel` で GPIO から ADC1 の CH7 を決める)、減衰 12dB、curve fitting の校正(eFuse が無ければ検知を止める)。
  64 回(1 サンプルごとに 1 tick 待つ)の平均 → 約 1.6 秒ごとに更新。タスクの優先度 2、**スタック 2048B**。
- `app_main.cpp` から `battery::Init()`(電源キーの初期化の後、ディスプレイの前)。`serial_cmd.cpp` に `batt`(下)。

**メモリ**(ステップ 3 の回帰): 開始時の free_int は **150,232 → 147,108(−3,124B)**。最初は stack 2560 で 146,604(−3,628B)になり、
hostapi_check の終了時が 145,956 で `MIN_FREE_INT`(146,000)を 44B 割った(リークではなく基準の下限)。ユーザーの判断(ゲート 5)でスタックを計って小さくした:
`uxTaskGetStackHighWaterMark` はループの使用量が約 1,000B、**ログの呼び出しが 1KB 以上を食う**(スタック 2048 のとき `ESP_LOGI` でオーバーフローした)ので、
ループでログを出さない設計にして 2048 にした。**conf(しきい値)は変えていない。**

## ステップ 2: `batt` コマンド

`KBCMD: batt mv=<mV> pct=<%>`。検知が無いボードは `batt err not supported`、最初のサンプル前は `batt err no sample yet`。`docs/workflow.md` §3.4 の表に 1 行足した。

| 場面 | `batt` | テスター |
|---|---|---|
| USB + 電池(実装後のファーム) | 4,122〜4,131 mV、91〜92% | 4.11V |

(実装前の測定は 4.10V / 画面 410X、電池のみ 4.03〜4.05V / 403X、再生中 3.99〜4.00V で一致。ステップ 0-b)

## ステップ 3: 回帰と後片付け

**Waveshare の回帰 3 本**(`KYBOTOS_DEV_APPS=1`、`device-regress.sh --board waveshare_lcd28`。`captures/phase25-waveshare/report.md`)

| アプリ | 開始 free_int | 終了 free_int | 差分 | largest_int | シナリオ | 判定 |
|---|---|---|---|---|---|---|
| metronome ×3 | 147,108 | 147,108 | +0 | 98,304 | PASS(19 手順) | PASS |
| mp3player | 147,108 | 146,636 | −472 | 98,304 | PASS(17 手順) | PASS |
| hostapi_check | 146,636 | 146,460 | −176 | 98,304 | PASS(6 手順) | PASS |

PASS。WARN / ERROR は 0 件(`E ... hcheck_missing.mp3` は hostapi_check が意図して出す)。`largest_int` と `EXPECT_DELTA` は変わらず、**`MIN_FREE_INT` との余裕は 460B まで減った**
(以後の free_int を減らす変更は、基準の更新を先に相談する)。電池のみ・USB の両方で既存の動作は変わらない。

**CrowPanel**(普段使いのファームを焼き、起動ログ。`captures/phase25/crowpanel-boot.log`): `batt` は `err not supported`、起動ログに `BATT:` は無い。
`battery.cpp.obj` には ADC もタスクを作る処理も無い(`nm`)。**起動の途中の値は 24c の普段使いのファームと完全に一致**
(`Audio_Init: free heap 220716 -> 174484`、`heap after seq init: free 170892`)。24b の基準値(アプリ起動時 155,456)は、この時 SD が Waveshare 側にありアプリを起動できなかったので、
同じ値の比較はしていない(起動の途中の値の一致で代えた)。

**後片付け**: Waveshare を普段使い(`KYBOTOS_DEV_APPS=0`)のファームに焼き直し、共用の SD から検査用の 3 本(hostapi_check / midi_loopback / synth_probe)を `rm` で消した
(SD のアプリは sequencer / metronome / mp3player)。CrowPanel は 24c の終わりから普段使いのファームのまま(今回は電池のコードを入れて焼き直した)。
文書: README のボードの表、`docs/boards.md`(GPIO8、J10、§4 の電池電圧)、`docs/workflow.md`(`batt`)、`docs/lessons.md`、roadmap、status。

## 完了条件の確認

| 完了条件 | 結果 |
|---|---|
| 1. ステップ 0 の結果と、ユーザーの承認 | ✅ |
| 2. `batt` で電圧と残量が読め、テスターと合う。**電池なしは判定できない理由を記録**(追記 その 2) | ✅ |
| 3. Waveshare の回帰 3 本 PASS、Host API / ABI・`.wasm` 不変 | ✅(余裕 460B) |
| 4. CrowPanel が電池のコードを動かさずにビルド・起動する | ✅(起動の途中の値が 24c と一致) |
| 5. 普段使いのファームに戻し SD の検査用アプリを消す、記録・roadmap・status・workflow、25a への引き継ぎ | ✅ |

## 25a への引き継ぎ

- **取り出す関数**: `#include "battery.hpp"` → `battery::Get(&r)`(`r.mv`、`r.pct`)。電池の検知が無いボード・サンプル前は false(表示しない)。ロックは不要。
- **更新間隔**: 約 1.6 秒(0.64 秒のサンプリング + 1 秒の待ち)。表示の更新はそれより速くしても意味がない。
- **揺れ**: 64 回平均で ±2 カウント程度(±10mV)、残量にして 1% 前後。音の再生中も落ち込みは見えなかった(約 10mV 以下)。表示は整数 % で十分。
- **USB のとき**: 約 50mV 高く読め、電池なしでは 4.19〜4.20V(= 99〜100%)。USB 給電(電源キーで起動していない = `External power`)のときは % を出さずに「USB」の表示にする案がある(ユーザーと決める)。
- **メモリ**: `MIN_FREE_INT` との余裕が 460B。メニューの表示(ラベルの追加)が回帰の開始時の free_int を減らすなら、しきい値の更新を先に相談する。
- 残量(%)の表示は、Phase 22d / 22e の配色に合わせる。Linux ホストでは検知が無いので、25a のステップで模擬値を決める。
