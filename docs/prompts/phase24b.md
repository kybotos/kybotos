# Phase 24b: CrowPanel の音と SD(回帰 3 本 PASS)

- 契約日: 2026-10-10
- 参照: `docs/results/phase24.md`(0-b の NS4168、0-c の比較表、段階の計画)、`docs/results/phase24a.md`、`docs/architecture.md` 11-12(内蔵音源とオーディオ経路)/ 11-13(ボードの切り替え)、
  `src/components/audio/audio.cpp`(I2S、ミキサ、MP3、`amp_init_off`)、`src/components/storage/sdcard.cpp`、`src/components/board/boards/crowpanel_adv28.h`、
  `scripts/device-regress.sh` / `device-regress.conf`、`docs/workflow.md` §3.3(カメラ + 人間の操作)/ §3.4(実機の回帰)/ §3.8(音の検証)
- 結果報告先: `docs/results/phase24b.md`

## 目的

CrowPanel Advance 2.8"(V1.2)は Phase 24 / 24a で画面・タッチ・SD のマウント・シリアルのコマンドまで動いたが、**音は出ない**(アンプを止めている)。
このフェーズで、**オンボードのアンプ(NS4168)を鳴らし**、内蔵音源・クリック・起動音・MP3 がすべて鳴るようにし、SD の読み書きを確かめて、
**CrowPanel でも回帰 3 本(metronome / mp3player / hostapi_check)を PASS させる**。CrowPanel を「Waveshare の水準」へ上げる段階の 2 段目(24 → 24a → **24b** → 24c MIDI と BOOT ボタン)。

## 前提(着手時にソースと実機で確かめ直す)

- **P1: アンプは NS4168**(モノラルの D 級アンプ、I2S 入力)。BCLK=IO13、LRCLK=IO11、SDATA=IO12。**CTRL は IO21 で、LOW で動作**(ベンダーのサンプルの確認済みの状態。
  トランジスタを介した電源の制御)。回路図のアンプの欄に **「Right channel」**(I2S の R のスロットを鳴らす)。出力は差動で外付けスピーカーの J2 へ(GND に落とさない)。
  手元の CrowPanel には外付けスピーカーをつないである。
- **P2: いまの CrowPanel のファーム**は、I2S を CrowPanel のピンで動かし、**`amp_init_off()` でアンプを止めている**(`audio.cpp`、Phase 24)。起動ログに `amplifier enable pin GPIO21: off`。
- **P3: ミキサ**(内蔵音源・クリック・tone・起動音)は 44.1kHz / 16 bit / **ステレオで、L と R に同じ値を書く**(`audio.cpp` のミキサのタスク、`s_chunk[i*2] = s_chunk[i*2+1] = v`)。→ R のアンプでも鳴るはず。
- **P4: MP3 の再生中は esp-audio-player が I2S を曲の形式に合わせて組み直す**(`reconfig_rate`。MP3 とミキサは排他、U-21)。
  **同梱の 3 曲(`test.mp3` / `tune_down.mp3` / `tune_duo.mp3`)はどれも 44.1kHz の mono**。mono のとき I2S は `I2S_SLOT_MODE_MONO` になり、
  ESP-IDF 5.5 の `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` は **mono のとき `slot_mask = I2S_STD_SLOT_LEFT`**。
  **ESP32-S3 でこのとき R のスロットに何が出るか(同じデータか、0 か)は未確認**。0 なら、**CrowPanel では mono の MP3 が無音**になる。
  (Waveshare の PCM5101 はステレオの DAC なので、L だけでも本体のスピーカーで聞こえていた可能性がある。)
- **P5: SD は SDSPI**(SPI3、MOSI=IO6 / MISO=IO4 / SCLK=IO5 / CS=IO7、基板に 10kΩ のプルアップ)で、Phase 24 でマウントできた(SDHC 約 32GB、Waveshare と共用の 1 枚)。
  読み書きの検証・MP3 の連続再生・速度はまだ。Waveshare は SDSPI を 11.43MHz で使っている(`KYBOTOS_SD_SKIP_SDMMC_PROBE` の説明)。
- **P6: 回帰**: `device-regress.sh --board crowpanel_adv28` でボードのビルドディレクトリとポートを選べる(Phase 24)。回帰には `KYBOTOS_DEV_APPS=1` のファームが要り、
  **検査用アプリが共用の SD に置かれる**(終わったら `rm` で消す)。しきい値は `MIN_FREE_INT=146000` / `MIN_LARGEST_INT=98304` / `MIN_FREE_PSRAM=8000000`、
  `EXPECT_DELTA` は mp3player −472 / hostapi_check −176(Waveshare の値)。CrowPanel のメニューの状態の free_int は 172,424、largest_int 122,880(Phase 24)。
- **P7: mp3player の一覧の縦スワイプ**は、SD の曲が 7 曲以下で一覧が送られず 24a で確かめられなかった(24a から持ち越し)。

## ゲート

1. **ステップ 0 の確認と設計メモ(アンプを有効にするタイミング、mono の MP3 の出し方、ボード別の回帰の基準値の持ち方)をユーザーが承認するまで実装しない。**
2. **Waveshare は変えない**: 回帰 3 本 PASS、数値が Phase 24a と同じ(開始時の free_int 150,232 / largest_int 98,304、highmark 25,744)。
   **mono の MP3 の I2S の設定を変える場合も、Waveshare の出力は変えない**(変えるならボードの記述の定数で CrowPanel だけ)。
3. **Host API / ABI と `.wasm` は変えない。** 音量の既定値(`kDefaultVolume`)・ゲインの式も変えない(ボードの差はアンプの側で吸収する。音量の差を感じたら記録して相談)。
4. **回帰の conf のスキーマを変えるなら(ボード別のしきい値など)、事前に承認を得る**(roadmap の承認ゲート)。
5. **出力にするピンは IO21(アンプの CTRL)だけ**を足す。スピーカーの端子(差動)を GND に落とさない。
6. 公開の repo なので、非公開の repo の中身は書かない。

## スコープ

### 含む

- **ステップ 0: 確認と設計メモ**(`docs/results/phase24b.md`)
  - **0-a 実機で鳴らす**(一時のコード `PHASE24B-TEMP` で、アンプを有効にしたファーム):
    - 起動音、metronome のクリック、`synth_probe`(内蔵音源の全 note)が鳴るか。**ユーザーの耳**と、カメラのマイクの録音(§3.8 のレベル)。
    - **mono の MP3**(`test.mp3`)が鳴るか(P4)。鳴らなければ、mono のときのスロットを `I2S_STD_SLOT_RIGHT` / `I2S_STD_SLOT_BOTH` にして比べる。
    - アンプを有効にする瞬間・I2S の組み直し(MP3 の開始 / 終了)・停止のときの**ポップ音**、無音のときの**ノイズ(ヒス)**、音量の感じ(Waveshare と比べて)。
  - **0-b 設計メモ**。決めること:
    - **a. アンプの有効化のタイミング**: 起動時に I2S が動き出してから 1 回だけ有効にし、以後つけたままにするか(推奨)、無音の間は止めるか(ヒスやポップの結果で決める)。
      起動音より前に有効にしておく必要がある。
    - **b. mono の MP3**: 0-a の結果で、(1) 変えない(R にも出ている)、(2) ボードの記述に mono のときのスロット(`KB_I2S_MONO_SLOT`)を持ち、CrowPanel だけ RIGHT / BOTH、
      (3) その他。**Waveshare の設定は変えない**(ゲート 2)。
    - **c. SD**: SDSPI の周波数を Waveshare と同じにするか。読み書きの確かめ方(hostapi_check の `fs_write` / `fs_read`、MP3 の連続再生で途切れないか)。
    - **d. 回帰の基準値**: CrowPanel の `free_int` / `largest_int` / `EXPECT_DELTA` が Waveshare と違った場合の持ち方
      (今の共通のしきい値のままで通るならそのまま。ボード別の上書きが要るなら、conf のスキーマの変更としてゲート 4 の承認を求める)。
- **ステップ 1: 実装**(承認された方式で)。Waveshare の回帰を先に回してゲート 2 を確かめ(オブジェクトの比較も使う。`docs/lessons.md` 24a)、それから CrowPanel。
- **ステップ 2: CrowPanel での確認**:
  - **回帰 3 本**: `KYBOTOS_DEV_APPS=1` で焼き、`device-regress.sh --board crowpanel_adv28`。**CrowPanel の基準値**(開始時の free_int / largest_int、highmark、`EXPECT_DELTA`)を記録する。
  - **耳**(ユーザー): 起動音、metronome、mp3player(3 曲)、Settings の音量、`synth_probe` のドラム。カメラで録音してレベルを残す(§3.8)。
  - **SD**: MP3 を最後まで続けて再生して途切れないこと、hostapi_check のファイルの読み書き。
  - **mp3player の一覧の縦スワイプ**(24a から): **ユーザーが SD の `/music` に 8 曲以上の MP3 を置いて**から、指で確かめる。
- **ステップ 3: 後片付けと文書**: 両方のボードを普段使い(`KYBOTOS_DEV_APPS=0`)のファームに戻し、共用の SD の検査用アプリを消す。
  `docs/results/phase24b.md`、README のボードの表、roadmap、`docs/workflow.md`(ボード別の基準値の扱いなど、要れば)、`docs/lessons.md`(要れば)。

### 含まない

- MIDI と BOOT ボタン(24c)。
- マイク(IO9 / IO10)、ブザー(IO8)、無線モジュール。
- MP3 と内蔵音源の同時再生(U-21)、音色(U-27)、発音の前倒し(U-22)。
- ミキサの作り替え、音量の既定値の変更。

## 完了条件

1. CrowPanel で、起動音・metronome のクリック・内蔵音源・mono の MP3 が外付けスピーカーから鳴る(ユーザーの耳)。ポップ音・ノイズの所見を記録している。
2. **CrowPanel で回帰 3 本 PASS**。CrowPanel の基準値を記録している(Waveshare と違う値は理由を書く)。
3. SD: MP3 の連続再生で途切れない、ファイルの読み書きができる。mp3player の一覧の縦スワイプを指で確かめた。
4. Waveshare: 回帰 3 本 PASS、数値が Phase 24a と同じ(違えば理由)。Host API / ABI・`.wasm` 不変。
5. 両方のボードを普段使いのファームに戻し、共用の SD の検査用アプリを消している。記録 `docs/results/phase24b.md`。

## 追記の置き場所

スコープを変えるときは本文を書き換えず、この下に「追記 (日付)」節を足す。

## 追記 (2026-10-10): 持ち越す 2 項目

ユーザーの判断で、次の 2 つを Phase 24c に持ち越して 24b を閉じる。

1. **mp3player の一覧の縦スワイプ**(完了条件 3 の一部)。SD の曲が 4 曲で、一覧(7 行)が送られないため。曲を足してから 24c で指で確かめる。
   スワイプの処理はタッチとアプリの側の話で、このフェーズの音と SD の変更とは独立している。
2. **Waveshare の実機の回帰**(ゲート 2・完了条件 4)。ボードと SD のつなぎ替えを省くため、後で回す。
   代わりの裏づけとして、**Waveshare のビルドの `audio.cpp.obj` の逆アセンブルが変更の前後で完全に一致する**ことを確かめた
   (このフェーズのコードの変更は `audio.cpp` だけ)。24c の Waveshare の回帰で数値を確かめる。

