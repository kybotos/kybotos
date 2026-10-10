# Phase 24: ボードをビルドで切り替える(CrowPanel Advance 2.8" への対応の第 1 段)

- 契約日: 2026-10-10
- 参照: `src/components/board/board_pins.hpp`(Waveshare のピン)、`src/components/{display,touch,audio,storage,power_key,midi}/`、
  `src/main/serial_cmd.cpp`(USB Serial/JTAG のコンソール)、`src/sdkconfig.defaults`、`src/main/Kconfig.projbuild`、
  `scripts/device-regress.sh`(`KYBOTOS_PORT`)、`docs/workflow.md`(ビルド・フラッシュ・回帰のコマンド)、
  `docs/results/phase12.md` / `phase15.md`(PSRAM と SDMMC の経緯)
- 結果報告先: `docs/results/phase24.md`

## 目的

ファームは **Waveshare ESP32-S3-Touch-LCD-2.8** に固定されている(ピン、LCD / タッチ / 音声のチップ、USB Serial/JTAG のコンソール、
電源キーの自己保持)。2 枚目のボードとして **Elecrow CrowPanel Advance 2.8" HMI ESP32 AI Display**(DIS01728A)を手に入れた。

- 当面は **Waveshare と CrowPanel を併用**する。どちらも同じソースから、**ビルドの指定だけで**ファームを作れるようにしたい。
- いずれ **ESP32-S3 系のボードやコンフィグが 3 つ以上**になりうる。2 枚のための場当たりの `#if` ではなく、ボードを足す手順が決まっている構成にする。
- CrowPanel は一度に Waveshare と同じ水準にはならない。**段階を決めて、Waveshare の水準(回帰 3 本 PASS)まで持っていく**。

このフェーズでは、(1) 実機と Web の情報から 2 枚の構成を把握・比較し、(2) ビルドの切り替え方式と段階の計画を決め、
(3) 切り替えの枠組みを入れて Waveshare を変えずに保ち、(4) CrowPanel で起動してメニューが映るところまでを行う。
タッチ・音・SD・回帰などの残りは、ステップ 0 で決めた段階ごとに後続のフェーズ(24a〜)の指示書で行う。

## 前提(2026-10-10 に確かめたこと。ステップ 0 で実機と回路図で確かめ直す)

- **P1: CrowPanel の手元の構成**: 電源スイッチなし、バッテリーなし、SD カードあり、外付けスピーカーを SPK 端子に接続。
  PC には **CH340(`1a86:7522`)経由の `/dev/ttyUSB0`** として見えている(Waveshare は内蔵の USB Serial/JTAG で `/dev/ttyACM0`)。
  ハードウェアの版(V1.0 / V1.1 / V1.2)は未確認。
- **P2: Elecrow の公開情報**(wiki と GitHub の `Elecrow-RD/CrowPanel-Advance-2.8-HMI-ESP32-AI-Display-320x240`。回路図 V1.0 / V1.1 / V1.2、サンプルコード、データシート):
  - ESP32-S3(WROOM-1)、**16MB フラッシュ、8MB PSRAM**(オクタルかどうかは未確認)。
  - LCD: **ST7789**、SPI(サンプルでは SCLK=42, MOSI=39, DC=41, CS=40, RST なし)、240×320 を回転して 320×240、色の反転あり。バックライトは **IO38**。
  - タッチ: I2C のアドレス **0x38**(データシートは FT6336U。サンプルは FT5x06 互換のドライバ。GT911 のコードも同梱されていて、どちらが載っているかは版で違う可能性がある)、
    SDA=15, SCL=16(外部の I2C 端子と共用)、INT=47。
  - 音声: **I2S でアンプに直接**(LRCLK=11, BCLK=13, SDIN=12)、V1.1 から **IO21 でミュート**。
  - SD: SPI(MOSI=6, MISO=4, CLK=5。CS は wiki の表では「3.3V」とあり、回路図で確かめる)。
  - その他: ブザー IO8、I2S マイク(IO9 / IO10 / IO3)、IO45 でマイクと無線モジュールを切り替え、UART1 の端子(RX=18, TX=17)、
    UART0 の端子(RX=44, TX=43)、BOOT / RST のボタン、バッテリーの端子(充電回路あり)。
- **P3: そのまま焼くと動かない理由**: LCD・タッチ・I2S・SD・電源キーのピンがすべて違う。Waveshare の MIDI の RX(IO15)は CrowPanel のタッチの SDA、
  Waveshare のタッチの SCL(IO3)は CrowPanel のマイクの WS に当たる。シリアルのコマンド窓口(`KYBOTOS_SERIAL_CMD`)は USB Serial/JTAG 前提で、
  CH340(UART0)では使えない。
- **P4: いまのビルドの形**: ESP-IDF 5.5(docker の `esp-idf-v5.5:5.5.5`)、`src/` が 1 つのプロジェクト。`src/sdkconfig.defaults` は 1 本(16MB、PSRAM OCT 80MHz、CAPS_ALLOC)。
  生成物の `src/sdkconfig` は git に入れていない。ビルドディレクトリは `src/build`(回帰用の `KYBOTOS_DEV_APPS=ON` は `src/build-dev`)。
  この repo の外でファームを組む側は、`-B` / `-DSDKCONFIG=` / `-DKYBOTOS_EXTRA_APPS=` を渡して同じ `src/` をビルドしている。
- **P5: 両方のボードは同時に PC につなげる**(`/dev/ttyACM0` と `/dev/ttyUSB0`)。ポートは `KYBOTOS_PORT` で変えられる(`scripts/device-regress.sh`)。

## ゲート

1. **ステップ 0 の調査・比較・設計メモ・段階の計画をユーザーが承認するまで、ステップ 1 以降を実装しない。**
2. **CrowPanel に初めて焼く前に、工場出荷時のフラッシュの全体を吸い出して保存する**(`esptool.py read_flash 0 ALL`)。
   保存先は `captures/phase24/`(git の対象外)。Elecrow のバイナリなので、**repo にはコミットしない**。
3. **ピンは回路図で確かめてから出力にする**。サンプルコードの値だけで出力に設定しない(版で違いうる。IO45 / IO3 などのストラッピングピンに注意)。
4. **Waveshare は変えない**: ボードを指定しないビルドは Waveshare になり、回帰 3 本 PASS、開始時の `free_int` / `largest_int` と各アプリの highmark が Phase 23 と同じ
   (変わった場合は理由を書く)。この repo の外でファームを組む側が、何も変えずに今までどおりビルドできること。
5. **Host API / ABI と `.wasm` は変えない**(ボードの違いはホストの中で吸収する)。
6. 公開の repo なので、非公開の repo の中身は書かない。

## スコープ

### 含む

- **ステップ 0: 調査と設計**(記録は `docs/results/phase24.md`)
  - **0-a 実機**: 工場出荷時のファームの吸い出し(ゲート 2)、`esptool.py chip_id` / `flash_id`、工場出荷時のファームの起動ログ(PSRAM の種類と容量)、
    USB の構成(CH340 だけか、ネイティブの USB も出ているか)、基板の版の確認(シルクの印刷。ユーザーに見てもらう)、タッチの IC の確認(I2C のスキャン)。
  - **0-b Web**: wiki、版ごとの回路図、サンプルコード、データシート。参照した版と URL を記録に残す。
  - **0-c 比較表**: MCU のモジュール、フラッシュ / PSRAM、LCD(チップ、バス、ピン、向き、反転)、タッチ(チップ、アドレス、ピン、座標の向き)、
    音声(DAC かアンプか、ピン、ミュート、サンプルの形式)、SD(SDMMC / SDSPI、ピン)、コンソール(USB Serial/JTAG / UART)、
    電源(キー、自己保持、バッテリー)、ボタン、MIDI に使える UART、バックライト、その他(ブザー、マイク、無線モジュール)。
    **それぞれの差を、ファームのどこで吸収するかを列に書く**(ピンの定数だけで済む / ドライバが要る / 機能を持たない)。
  - **0-d ビルドの切り替え方式の設計メモ**。決めること:
    - **a. ボードの選び方**: たとえば Kconfig の choice(`KYBOTOS_BOARD_WAVESHARE_S3_LCD_28` / `KYBOTOS_BOARD_CROWPANEL_ADV_28`)と、
      ボードごとの `sdkconfig.defaults` の断片(`SDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/<board>/sdkconfig.defaults"`)。
      ボードの名前を 1 つ渡せばよい形(CMake の変数か、ラッパーのスクリプトか)。**指定しないときは Waveshare**。
    - **b. ボードの記述の置き場所**: ピンと「持っている機能」(自己保持の電源キー、音声の出口の種類、タッチのチップ、コンソールの経路など)を、
      ボードごとに 1 か所にまとめる。コンパイル時に選ぶ(実行時の自動判別はしない)ことを前提に、ヘッダ・構造体・Kconfig のどれで持つか。
    - **c. ドライバの分け方**: タッチ(CST328 と FT6336 など)、音声(DAC 経由とアンプ直結 + ミュート)、電源キー(ない場合)、コンソール(USB Serial/JTAG と UART)を、
      どこで切り替えるか。ESP-IDF の部品(`esp_lcd_touch_*` などの managed component)を使うか、手で書くか(依存の追加は最小限)。
    - **d. ビルドディレクトリと sdkconfig**: ボードごとに分けて、2 枚を並行してビルドできるようにする。
      **Waveshare の既存のパス(`src/build`、`src/build-dev`)を変えるか**(変えると workflow とこの repo の外のビルドに波及する)。
    - **e. フラッシュ・モニタ・回帰**: ボードごとのポートの既定値、`scripts/device-regress.sh` にボードを渡す方法、hpane のペインの使い分け。
    - **f. 3 枚目以降**: ボードを足すときに触るファイルの一覧(手順として書けること)。
  - **0-e 段階の計画**: CrowPanel を Waveshare の水準へ上げる段階と、各段の完了条件。**「Waveshare の水準」を定義する**(回帰 3 本 PASS、MIDI の入出力、メモリのしきい値など)。
    たたき台(ステップ 0 で見直す):
    - 24: 切り替えの枠組み、Waveshare 不変、CrowPanel が起動してメニューが映る
    - 24a: タッチ(メニューからアプリを起動し、metronome を操作できる)
    - 24b: 音(I2S のアンプ、ミュート)と SD(mp3player、内蔵音源)
    - 24c: コンソール(UART)と回帰 3 本 PASS
    - 24d: MIDI(UART1 の端子)と、電源キーのない操作(BOOT ボタンの割り当てなど)
    - 範囲外として記録だけ: マイク、無線モジュール、ブザー、バッテリー、電源スイッチ(付けるなら別の段)
- **ステップ 1: 切り替えの枠組み**(承認された方式で)。Waveshare のピンとドライバの選択をボードの記述へ移す。
  **Waveshare のファームが同じに動くこと**(ゲート 4)。この repo の外のビルドが変えずに通ることも確かめる。
- **ステップ 2: CrowPanel のボードの記述と起動**: ボードの記述を足し、LCD とバックライトを動かして、**起動からスプラッシュ・メニューの表示まで**を確かめる。
  タッチ・音・SD・コンソールは、ステップ 0 の段階の計画で 24 に入れたものだけを行い、それ以外は無効のまま起動できること
  (SD が無い・音が出ないときも、メニューまでは止まらずに進むこと)。起動ログと画面の写真を記録に残す。
- **ステップ 3: 文書**: `docs/results/phase24.md`、`docs/workflow.md`(ボードの指定、ポート、ビルドディレクトリ)、README(対象のハードウェア)、
  `docs/architecture.md` の該当箇所、roadmap(24a〜 の行)、ボードを足す手順。

### 含まない

- 24a〜 の段階の実装(ステップ 0 の計画に従い、それぞれの指示書で行う)。
- Linux ホストの変更(ボードの切り替えは実機のファームだけ)。
- CrowPanel のマイク、無線モジュール、ブザー、バッテリー、電源スイッチ。
- LVGL・ESP-IDF の版の変更。

## 完了条件

1. 記録 `docs/results/phase24.md` に、実機と Web の調査、比較表、切り替え方式の設計メモ、段階の計画(ユーザー承認済み)がある。
2. ボードの名前の指定だけで、Waveshare と CrowPanel のファームをそれぞれビルドできる。指定しないときは Waveshare。
3. Waveshare: 回帰 3 本 PASS、メモリの値と highmark が Phase 23 と同じ(違えば理由)。この repo の外のビルドが変えずに通る。Host API / ABI・`.wasm` は不変。
4. CrowPanel: 起動してスプラッシュとメニューが映る(写真と起動ログ)。工場出荷時のファームの吸い出しがある(repo の外)。
5. ボードを足す手順が文書になっている。

## 追記の置き場所

スコープを変えるときは本文を書き換えず、この下に「追記 (日付)」節を足す。
