# Phase 24d 実施記録: CrowPanel 対応の残りの確認(一覧のスワイプ、Waveshare の実機の回帰)

- 指示書: `docs/prompts/phase24d.md`
- 状態: **完了(2026-11-03)**。**ソフトの変更なし**。CrowPanel は「Waveshare の水準」(回帰 3 本・メモリ・MIDI・戻る / ホーム。電源断とバッテリーは除く)に届いた

---

## ステップ 1: mp3player の一覧の縦スワイプ(CrowPanel、2026-11-03)

- 検査用の MP3 を ffmpeg で 6 本作り(`swipe-test-01.mp3` 〜 `06`。正弦波 4 秒、44.1kHz mono、96kbps。`captures/phase24d/music/`、git の対象外)、
  ユーザーが共用の SD の `/music` に足した。曲は 4 → **10 曲**(一覧は 7 行、右上に `1/10`)。
- 普段使いのファームのまま、シリアルから `run mp3player` → 注入のスワイプ(ログ `captures/phase24d-swipe/monitor.log`):

| 操作 | 一覧の先頭の行(`texts`) | 判定 |
|---|---|---|
| (起動直後) | `test` … `swipe-test-03`(7 行) | ✅ |
| `drag 160 200 0 -150 400`(上へ) | `3 Views Of A Secret` … `swipe-test-06`(最後まで届く) | ✅ |
| `drag 160 80 0 150 400`(下へ) | 先頭に戻る | ✅ |
| `key back` | `app: key back -> stop`、メニューへ。停止時の free_int 150,240 = 開始時 | ✅ |

- スワイプで曲が選ばれて再生が始まることはなく(ログに再生開始なし)、WARN / ERROR は 0 件。
- **指(ユーザー): OK。** 8 曲目以降まで送れた。
- `1/10` の表示はスワイプでは変わらない(選択位置の表示。仕様どおり)。

## ステップ 2: Waveshare の実機の回帰(2026-11-03)

`KYBOTOS_DEV_APPS=1` で焼き、`device-regress.sh --board waveshare_lcd28 --task phase24d-waveshare`(`captures/phase24d-waveshare/report.md`)。**PASS**(WARN / ERROR 0 件)。

| アプリ | 開始 free_int | 終了 free_int | 差分 | largest_int | シナリオ | 判定 |
|---|---|---|---|---|---|---|
| metronome ×3 | 150,232 | 150,232 | +0 | 98,304 | PASS(19 手順) | PASS |
| mp3player | 150,232 | 149,760 | −472 | 98,304 | PASS(17 手順) | PASS |
| hostapi_check | 149,760 | 149,584 | −176 | 98,304 | PASS(6 手順) | PASS |

- **Phase 24 / 24a と同じ数値**(free_int 150,232 / largest_int 98,304、metronome の highmark 25,744、free_psram 8,136,668)。SD の曲が 10 曲になった影響もない。
- 起動ログ: `APP: board: waveshare_lcd28`、`TOUCH_CST328: Touch online port=1 addr=0x1A`。
- **ホームキーの分岐(24b の追記)は Waveshare で何もしない**: `PWR_KEY: External power: key control disabled`(Waveshare は `KB_HAS_POWER_LATCH` のボードで、
  `KB_HAS_POWER_KEY` はその値になる。従来どおりの経路)。回帰のシナリオの `key back`(mp3player)も PASS。
- Host API / ABI・`.wasm` は不変。

## ステップ 3: 後片付けと文書

- Waveshare を普段使い(`KYBOTOS_DEV_APPS=0`)のファームに焼き直し、共用の SD から検査用の 3 本(hostapi_check / midi_loopback / synth_probe)を `rm` で消した
  (SD のアプリは sequencer / metronome / mp3player)。CrowPanel は 24c の終わりから普段使いのファームのまま(このフェーズでは回帰用を焼いていない)。
- SD の検査用の MP3(`swipe-test-01〜06`)は残してある(消すか残すかはユーザー次第)。
- **`scripts/hpane.sh` を直した**: herdr 0.9.5 で `herdr wait output` が `herdr pane wait-output` に変わっていて、`hpane.sh run` / `waitfor` が
  完了していても必ずタイムアウト(exit 124)になっていた。2 箇所を新しいコマンドに替え(`run` はマッチ結果の JSON を捨てる)、
  成功(exit 0)と失敗(exit 1)の戻り値を確かめた(ユーザー承認の上。`docs/workflow.md` の手順は変わらない)。
- 文書: README のボードの表、roadmap(24d を done)、status。

## 完了条件の確認

| 完了条件 | 結果 |
|---|---|
| 1. CrowPanel で一覧を縦スワイプして 8 曲目以降まで送れる、誤選択なし、WARN / ERROR なし | ✅(注入 + 指) |
| 2. Waveshare で回帰 3 本 PASS・数値が 24a と同じ、ホームキーの分岐が何もしない、Host API / ABI・`.wasm` 不変 | ✅ |
| 3. 両ボードを普段使いのファームに、SD の検査用アプリを消す、記録、roadmap | ✅ |

## Phase 24 のまとめ

CrowPanel Advance 2.8"(V1.2)は、画面(24)・タッチ(24a)・音と SD(24b)・ホームキー(24b の追記)・MIDI(24c)・一覧のスワイプと Waveshare の回帰(24d)まで、
Waveshare と同じ水準で動く。ボードはビルドで選ぶ(`scripts/fw.sh <board>`)。**対象外のまま**: 電源断(自己保持のラッチ)とバッテリー。
