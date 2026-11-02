# Phase 24c: CrowPanel の MIDI(J6 の UART1)

- 契約日: 2026-11-02
- 参照: `docs/boards.md`(§1 の MIDI の行、§3-3 の J6、§4 の MIDI の前提)、`docs/results/phase24b.md`(追記のホームキーまで)、
  `docs/results/phase08a.md` / `phase08c.md`(Waveshare の MIDI OUT / IN の回路と、その確かめ方)、`src/components/midi/midi.cpp`、
  `src/components/board/boards/crowpanel_adv28.h`、`wasm-apps/dev/midi_loopback/`、`docs/workflow.md` §3.4 / §3.5
- 結果報告先: `docs/results/phase24c.md`

## 目的

CrowPanel Advance 2.8"(V1.2)の **MIDI OUT / IN を動かす**。CrowPanel を「Waveshare の水準」へ上げる段階の 4 段目(24 → 24a → 24b → **24c**)。
画面・タッチ・音・SD・回帰 3 本・戻る / ホーム(24b の追記の IO1 のキー)までは済んでいる。

## 前提(着手時にソースと実機で確かめ直す)

- **P1: 回路**(ユーザー、済み): Waveshare と同じ自作の回路(OUT: 2SC1815 のトランジスタで駆動、IN: フォトカプラ TLP2361)を、
  **J6(3.3V-UART1)の GND / 3.3V / TX / RX** につないだ。DIN の IN / OUT は PC の UM-ONE の OUT / IN へ。
  LED を見る限り OUT は動いているらしい(ユーザー)。
- **P2: ファーム**: ボードの記述は Phase 24 から `PIN_MIDI_TX GPIO_NUM_17` / `PIN_MIDI_RX GPIO_NUM_18`(J6)。`midi.cpp` は
  ボードによらず UART1、31250bps 8N1、**TXD を反転、RXD は反転なし**(Waveshare の回路に合わせた設定)。CrowPanel でも起動時に初期化している。
  → **ソフトの変更は要らない見込み**。
- **P3: 確かめ方**: Phase 8c の Step 1 と同じ項目(Note On / Off、ランニングステータス、SysEx 302 バイト、アクティブセンシング、UART のエラーが無いこと)。
  IN は PC の `amidi -S` で UM-ONE から送り、検査用アプリ `midi_loopback` の画面(受信バイト数と直近 16 バイトの 16 進)をシリアルの `texts` で読む。
  OUT は `midi_loopback` の START / STOP(MIDI Clock を出す)を PC の `amidi -d` で受ける。

## ゲート

1. **ソフトの変更が要ると分かったら、実装の前にユーザーの承認を得る**(極性・ピンを変えるなら、Waveshare は変えない形で)。
2. Host API / ABI と `.wasm` は変えない。
3. 公開の repo なので、非公開の repo の中身は書かない。

## スコープ

### 含む

- **ステップ 1: 確認**: `KYBOTOS_DEV_APPS=1` のファームを CrowPanel に焼き、P3 の項目を確かめる。
- **ステップ 2: 後片付けと文書**: 普段使いのファームに戻し、共用の SD の検査用アプリを消す。`docs/results/phase24c.md`、README のボードの表、roadmap、status、
  `docs/workflow.md` / `docs/lessons.md`(要れば)。

### 含まない

- **24b から持ち越した 2 項目**(mp3player の一覧の縦スワイプ、Waveshare の実機の回帰)。ユーザーの判断で、このフェーズでは処理せず、**次の段(24d)へ持ち越す**。
- MIDI の回路の変更、MIDI THRU、電源のスイッチと自己保持の回路(ラッチ)。
- MIDI Clock の精度の測定(`midi-clock-probe.sh`、Phase 13)。

## 完了条件

1. CrowPanel の MIDI IN で、Note On / Off・ランニングステータス・SysEx 302 バイトが欠けずに受信でき、UART のエラーが出ない。
2. CrowPanel の MIDI OUT で、START / Clock / STOP が PC に届く。
3. CrowPanel を普段使いのファームに戻し、共用の SD の検査用アプリを消している。記録 `docs/results/phase24c.md`。持ち越しの 2 項目を roadmap の次の段に書いている。

## 追記の置き場所

スコープを変えるときは本文を書き換えず、この下に「追記 (日付)」節を足す。
