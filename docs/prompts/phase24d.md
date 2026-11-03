# Phase 24d: CrowPanel 対応の残りの確認(一覧のスワイプ、Waveshare の実機の回帰)

- 契約日: 2026-11-03
- 参照: `docs/results/phase24b.md`(CrowPanel の基準値、ホームキーの追記、持ち越しの経緯)、`docs/results/phase24c.md`(次の段への引き継ぎ)、
  `docs/results/phase24a.md`(Waveshare の基準値)、`docs/boards.md`、`docs/workflow.md` §3(実機の回帰、`--board`、`fw.sh`、ポート)、
  `src/components/board/power_key.cpp`、`src/components/audio/audio.cpp`
- 結果報告先: `docs/results/phase24d.md`

## 目的

24b・24c で持ち越した **2 項目を確認して**、CrowPanel Advance 2.8" を「Waveshare の水準」
(回帰 3 本・メモリ・MIDI・戻る / ホーム。電源断とバッテリーは除く)に届かせる。Phase 24 の最後の段(24 → 24a → 24b → 24c → **24d**)。
原則として**確認だけ**で、ソフトの変更は想定しない。

## 前提(着手時に実機で確かめ直す)

- **P1: 一覧のスワイプ**: mp3player の曲の一覧は 7 行で、それより曲が多いと縦にスワイプして送る。24b のときは SD の曲が 4 曲で一覧が送られず、確かめられなかった。
  SD は Waveshare と共用(1 枚)。ユーザーが曲を足す。
- **P2: Waveshare の実機の回帰**: 24a で最後に実機の回帰を回して以降、Waveshare には焼き直していない。その間に Waveshare の経路へ入った変更は 2 つ:
  (a) 24b の `audio.cpp`(`amp_set`。アンプのピンが `GPIO_NUM_NC` のボードではコンパイル時に消える。`audio.cpp.obj` の逆アセンブルは前後で完全に一致)、
  (b) 24b の追記のホームキー(`power_key.cpp` に実行時の分岐が 1 つ増えた)。ビルドは通っている(`captures/homekey/build-waveshare.log`)。
- **P3: 基準値**(Waveshare、Phase 24 / 24a): 開始時の free_int **150,232**、largest_int **98,304**、metronome ×3 は +0、mp3player −472、hostapi_check −176、
  metronome の highmark **25,744**、free_psram 8,136,668、WARN / ERROR 0 件、`TOUCH_CST328: Touch online port=1 addr=0x1A`。
  CrowPanel(24b): free_int 155,456 / largest_int 106,496(他は同じ)。
- **P4: ポート**: 2 枚とも `/dev/ttyACM0`。必ず `KYBOTOS_PORT_<BOARD>` に by-id(Waveshare MAC 94:A9:90:17:6F:C0、CrowPanel MAC 44:1B:F6:8A:6F:04)を指定し、
  `fw.sh` と `device-regress.sh --board <board>` で操作する。焼いた後は起動ログの `APP: board: <board>` を見る。

## ゲート

1. **ソフトの変更が要ると分かったら、実装の前にユーザーの承認を得る**(原因と、もう一方のボードの経路を変えない形を添えて)。
2. Host API / ABI と `.wasm` は変えない。
3. 公開の repo なので、非公開の repo の中身は書かない。

## スコープ

### 含む

- **ステップ 1: 曲を足す(ユーザー)と一覧のスワイプ(CrowPanel)**:
  共用の SD の `/music` に **8 曲以上**(一覧が 7 行なので、送りが要る数)を置く。曲の置き方(PC で SD を直接、など)はユーザーに確認してよい。
  CrowPanel の普段使いのファームで mp3player を開き、一覧を**指で縦にスワイプ**して、(a) 8 曲目以降まで送れる、(b) 送りの向きと量が Waveshare と同じ感触、
  (c) スワイプの途中で曲が誤って選ばれて再生が始まらない、(d) 戻る / ホームのキーが効く、を確かめる。ログに WARN / ERROR が出ないこと。
  座標の注入(`tap` / `swipe` のシリアルのコマンドがあれば)で再現できるなら、数値も記録する。
  **スワイプが効かない・誤爆するなら、原因を調べて報告し、直す前にゲート 1**(タッチのドライバ側か、LVGL の設定か、アプリ側かを切り分ける)。
- **ステップ 2: Waveshare の実機の回帰**:
  `KYBOTOS_DEV_APPS=1` で Waveshare に焼き、`KYBOTOS_PORT_WAVESHARE_LCD28=<by-id> ./scripts/device-regress.sh --board waveshare_lcd28 --task phase24d-waveshare`。
  **回帰 3 本 PASS、P3 の数値と同じ**であること。加えてホームキーの分岐(P2-b)が Waveshare で何も起こさないことを、起動ログと操作(戻る / ホームは従来どおり画面側のもの)で確かめる。
  数値が P3 と違えば、差の理由(ステップ 1 で SD に曲が増えたこと等)を切り分けてから判定する。
- **ステップ 3: 後片付けと文書**:
  両ボードを普段使いのファーム(`KYBOTOS_DEV_APPS=0`)に戻し、共用の SD から検査用アプリ(hostapi_check / midi_loopback / synth_probe)を消す(`rm`、Waveshare の回帰用のファームが置く)。
  追加した曲は、ユーザーの意向を聞いて残すか消す。`docs/results/phase24d.md`、README のボードの表、roadmap(24d を done、Phase 24 のまとめ)、status、
  `docs/boards.md`(要れば)、`docs/workflow.md` / `docs/lessons.md`(要れば)を更新する。

### 含まない

- 電源のスイッチと自己保持の回路(ラッチ)、バッテリー、MIDI THRU、MIDI の回路の変更。
- 3 つ目以降のボードの追加、`build-fw.sh` へのボード指定(非公開 repo 側の話はここに書かない)。
- 回帰のしきい値(`scripts/device-regress.conf`)の変更。通らないときは、conf を触る前にゲート 1。

## 完了条件

1. CrowPanel で、mp3player の一覧を縦スワイプして 8 曲目以降まで送れる。誤選択がなく、WARN / ERROR が出ない(ステップ 1 の a〜d)。
2. Waveshare で、回帰 3 本が PASS し、数値が Phase 24a と同じ(差があれば理由つき)。ホームキーの分岐が Waveshare で何もしない。Host API / ABI・`.wasm` は不変。
3. 両ボードを普段使いのファームに戻し、共用の SD の検査用アプリを消している。記録 `docs/results/phase24d.md` に、
   「Waveshare の水準に届いた」ことと、残り(電源断・バッテリーは対象外)を書き、roadmap の 24d を done にしている。

## 追記の置き場所

スコープを変えるときは本文を書き換えず、この下に「追記 (日付)」節を足す。
