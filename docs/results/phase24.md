# Phase 24 実施記録: ボードをビルドで切り替える(CrowPanel Advance 2.8" への対応の第 1 段)

- 指示書: `docs/prompts/phase24.md`
- 状態: **完了(2026-10-10)**。次は Phase 24a(タッチ)

---

## ステップ 0: 調査と設計(2026-10-10)

### 0-a 実機(CrowPanel)

PC には CH340 経由の `/dev/ttyUSB0` だけをつないだ状態(Waveshare は外していた)。コマンドは §3.2 の docker イメージの `esptool.py` を `hpane.sh` 経由で実行。
ログは `captures/phase24/`(git の対象外)。

**チップとフラッシュ**(`esptool.py flash_id`、`captures/phase24/esptool-flash_id.log`)

| 項目 | 値 |
|---|---|
| チップ | ESP32-S3 (QFN56) revision v0.2、水晶 40MHz |
| PSRAM | **Embedded PSRAM 8MB (AP_3v3)** = パッケージ内蔵の 8MB(S3R8 = オクタル) |
| フラッシュ | 16MB、eFuse 上は quad、3.3V(Manufacturer 0x46, Device 0x4018) |

→ **Waveshare と同じ組み合わせ**(16MB + オクタル PSRAM 8MB)。`sdkconfig.defaults` の PSRAM の設定(OCT 80MHz、CAPS_ALLOC)と、パーティション表はそのまま使える見込み。

**工場出荷時のファームの吸い出し(ゲート 2)**

- `esptool.py -b 921600 read_flash 0 ALL` → `captures/phase24/crowpanel-factory-16MB.bin`(16,777,216 B、249.9 秒)。
  sha256 `f1a2710effbc40ad514a50a1557e5f51d569b6e4067011fe329158005ff2de6c`(`crowpanel-factory-16MB.sha256`)。**repo にはコミットしない**。
- 中身: パーティション表は nvs 0x9000 / otadata 0xe000 / app0 0x10000(10MB)/ spiffs 0xa10000 / coredump 0xaf0000。
  アプリは `arduino-lib-builder`、ESP-IDF v5.1.4、2024-10-08 のビルド。文字列から、**メニューで各部を試す工場の検査プログラム**
  (Display / Touch / SD / Speaker / Microphone / Buzzer / WiFi / BLE / RTC / GPIO / auto test …)と分かる。
- 戻すときは `esptool.py write_flash 0 crowpanel-factory-16MB.bin`。

**工場出荷時のファームの起動ログ**(`captures/phase24/factory-boot.log`、CH340 の UART0、115200)

```
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
mode:DIO, clock div:1
TAMC_GT911::begin --> address 0x5D
Setup done
```

→ **タッチのドライバは GT911(0x5D)**。wiki の 0x38(FT6336 / FT5x06)とは食い違う(0-b の D-02)。PSRAM の初期化ログは Arduino の既定で出ない。

**まだ確かめていないこと**

- **基板の版**(V1.0 / V1.1 / V1.2。シルクの印刷)→ ユーザーに確認を依頼する。V1.0 はマイクのピンが違い、アンプのミュート(IO21)が無い。
- **I2C のスキャン**(GT911 が 0x5D / 0x14 のどちらで応えるか)→ 自前のファームが要るので、ステップ 2 の起動時のログで行う。
- **2 つ目の USB-C(ネイティブ USB)**: 0-b で見つけた。つないだときに `/dev/ttyACM*`(USB Serial/JTAG)として見えるか。

### 0-b Web(参照した情報源)

| 情報源 | 内容 |
|---|---|
| Elecrow wiki `CrowPanel_Advance_2.8-HMI_ESP32_AI_Display` | 端子とピンの表、版ごとの変更(V1.1: アンプの回路を変更・ミュート IO21・マイクを IO9 / 10 に、V1.2: ボタンの部品だけ) |
| GitHub `Elecrow-RD/CrowPanel-Advance-2.8-HMI-ESP32-AI-Display-320x240` | 回路図 V1.0 / V1.1 / V1.2(Eagle と PDF)、データシート(ST7789V、GT911、FT6336U、INMP441 ほか)、Arduino のサンプル |
| 同 `Eagle_SCH&PCB/1.2/readme.md`「ESP32 2.8-Inch Hardware Driver Guide」(2026-07-30) | V1.2 の回路図とサンプルを突き合わせた GPIO 表と、回路図とコードの食い違いの一覧(D-01〜D-08) |

PDF と readme は `captures/phase24/elecrow/` に保存した。**ピンは V1.2 の回路図 PDF のネット名で確かめた**(ゲート 3):
`IO39_TFT_SDA` `IO40_TFT_CS` `IO41_TFT_RS` `IO42_TFT_SCK` `IO38_LED_BK` `IO14_TFT_PWR` / `IO15_SDA` `IO16_SCL` `IO47_TP_INT` `IO48_TP_RST` /
`IO4_SD_MISO` `IO5_SD_SCK` `IO6_SD_MOSI` `IO7_SD_CS` / `IO11_I2S_LRCLK` `IO12_I2S_SDIN` `IO13_I2S_BCLK` `IO21_NS_CTRL`(アンプ NS4168、**「Right channel」**)/
`IO8_BEEP` / `IO9_MIC_CLK` `IO10_MIC_SD` `IO45_SPISW_ITCHING` / `ESP_TXD1` `ESP_RXD1`(J6)/ `OTG_D+` `OTG_D-`(GPIO19 / 20)/ CH340K → UART0。

**回路図とサンプルの食い違いで、このフェーズに効くもの**(readme の D-xx)

- **D-01 / D-02 タッチ**: 回路図は INT=IO47、RST=IO48。サンプルは INT / RST を使わずポーリングし、**アドレス選択の手順を GPIO1 / 2 で行う**(1 と 2 を LOW → 20ms 後に 2 を HIGH → 100ms 後に 1 を入力に戻す → 0x5D)。
  GT911 は「リセット中の INT の状態」でアドレス(0x5D / 0x14)を決める。どちらのピンが効くかは実機で確かめる(readme の推奨は 0x5D と 0x14 をスキャン、応答が無ければ IO48 / IO47 で手順を踏む)。
- **アンプ NS4168**: CTRL(IO21)を**LOW にして鳴らすのがサンプルの確認済みの状態**(トランジスタ経由で、LOW = 動作と読める)。回路図に「Right channel」。
  **NS4168 はモノラルで、I2S の片方のチャンネルだけを鳴らす**。いまのミキサはステレオで出しているので、L と R に同じものを出していれば問題ない(24b で確かめる)。
- **IO14 TFT_PWR**: 部品が NC の注記あり。サンプルは触らない → **触らない**。
- **SD**: SPI(IO4 / 5 / 6 / 7)、データ線に 10kΩ のプルアップ。サンプルは 80MHz を要求(いまの実機は SDSPI 11.43MHz で使っている)。

### 0-c 比較表

「吸収のしかた」の列: **定数** = ボードの記述のピン・値だけで済む / **ドライバ** = コードの分岐か別のドライバが要る / **無し** = 機能を持たない(無効にする)。

| 項目 | Waveshare ESP32-S3-Touch-LCD-2.8 | CrowPanel Advance 2.8"(V1.2 の回路図) | 吸収のしかた |
|---|---|---|---|
| MCU | ESP32-S3R8 | ESP32-S3-WROOM-1(S3R8、rev v0.2) | — |
| フラッシュ / PSRAM | 16MB / 8MB オクタル | 16MB(quad)/ 8MB オクタル(0-a) | 共通(`sdkconfig.defaults` のまま) |
| LCD | ST7789、SPI2:MOSI 45, SCLK 40, CS 42, DC 41, RST 39 | ST7789、SPI:MOSI 39, SCLK 42, CS 40, DC 41, **RST なし**(RC リセット) | **定数**(RST = -1 を許す) |
| LCD の向き・色 | 反転あり、`mirror(false, true)`、LVGL で swap_xy + mirror_x | 反転あり、サンプルは LovyanGFX の `offset_rotation=3` | **定数**(向きの 3 値をボードの記述に。実機で合わせる) |
| バックライト | IO5(HIGH で点灯) | IO38(HIGH で点灯、PWM 可) | **定数** |
| LCD の電源 | — | IO14(TFT_PWR、部品 NC) | 触らない |
| タッチ | **CST328**、I2C:SDA 1, SCL 3, INT 4, RST 2 | **GT911**(0x5D / 0x14)、I2C:SDA 15, SCL 16, INT 47, RST 48(サンプルは GPIO1 / 2 でアドレス選択) | **ドライバ**(GT911 を足す)+ 座標の変換は**定数** |
| I2C の共用 | — | J7(外部の I2C 端子)と共用、4.7kΩ のプルアップ | 無し |
| 音声の出口 | I2S → **PCM5101(ステレオ DAC)**:BCLK 48, WS 38, DOUT 47 | I2S → **NS4168(モノラルの D 級アンプ、R チャンネル)**:BCLK 13, WS 11, DOUT 12、**CTRL=IO21(LOW で動作)** | **定数**(ピン)+ **ドライバ**(アンプの有効化のピン。L / R に同じものを出すことの確認) |
| スピーカー | 本体のスピーカー | 外付けスピーカー(J2、差動。GND に落とさない) | — |
| SD | SDSPI(SPI3):MOSI 17, MISO 16, SCLK 14, CS 21。SDMMC のプローブは PSRAM 有効時に飛ばす | SDSPI:MOSI 6, MISO 4, SCLK 5, CS 7 | **定数**(SDMMC のピンを持たないボードは、プローブ自体を持たない) |
| PC との接続 | ネイティブ USB(USB Serial/JTAG、`/dev/ttyACM0`)だけ | **USB-C が 2 つ**: CH340K → UART0(`/dev/ttyUSB0`、自動書き込み)と、ネイティブ USB(OTG、GPIO19 / 20) | 後述(d / e) |
| ログ | UART0 が主、USB Serial/JTAG が副(`CONFIG_ESP_CONSOLE_UART_DEFAULT` + secondary) | 同じ設定なら、CH340 にも OTG の USB にもログが出る | 共通 |
| シリアルのコマンド窓口 | USB Serial/JTAG(`serial_cmd.cpp`) | **OTG の USB-C をつなげば同じ USB Serial/JTAG**。CH340 だけなら UART0 版が要る | OTG を使えば**共通**(要確認) |
| 電源キー・自己保持 | key=IO6, latch=IO7(長押しで電源断) | **無し**(手元はスイッチもバッテリーも無い)。**IO6 / IO7 は CrowPanel では SD の MOSI / CS** | **無し**(電源キーのタスクを持たない。そのまま焼くと SD の線を叩く) |
| ボタン | BOOT(IO0) | BOOT(IO0)、RESET | 24c で BOOT を戻る / ホームのキーに使うか決める |
| MIDI(自作の回路) | UART1:TX 18, RX 15 | 候補 **J6 の UART1:TX 17, RX 18(3.3V)**。**IO15 は CrowPanel のタッチの SDA** | **定数**(ピン)。回路のつなぎ替えはユーザー |
| バッテリー | あり(電源キーで管理) | 端子と充電回路あり(TP4059、ESP32 からは見えない)。手元は未接続 | 無し |
| その他 | (使っていない部品は省略) | ブザー IO8、マイク(IO9 / 10、IO45 で無線と切り替え)、無線モジュールの端子(IO0 / 1 / 2 / 3 / 9 / 10 / 46) | 無し(範囲外) |

**そのまま焼くと危ないピン**(ゲート 3 の補足): Waveshare の電源キーの IO6 / IO7 = CrowPanel の SD の MOSI / CS、Waveshare の LCD の MOSI IO45 = CrowPanel のマイクと無線の切り替え(ストラッピングピン)、
Waveshare のタッチの IO1〜4 = CrowPanel の無線の端子と SD の MISO、Waveshare の I2S の IO38 = CrowPanel のバックライト。**ボードの記述が CrowPanel を選んでいないファームを CrowPanel に焼かない**(後述の f の確認を入れる)。

### 0-d ビルドの切り替え方式の設計メモ(案。承認を求める)

**ボードの名前**: `waveshare_lcd28`(既定)と `crowpanel_adv28`。

**a. ボードの選び方 — Kconfig の choice + ボードごとの sdkconfig.defaults の断片 + CMake 変数 `KYBOTOS_BOARD`**

- `src/main/Kconfig.projbuild` に `choice KYBOTOS_BOARD`(`KYBOTOS_BOARD_WAVESHARE_LCD28`(既定)/ `KYBOTOS_BOARD_CROWPANEL_ADV28`)。
- `src/boards/<board>/sdkconfig.defaults` に、そのボードの choice と、ボードだけに要る設定(例: SDMMC のプローブを飛ばすかどうか)を置く。
- `src/CMakeLists.txt` の `project()` の前で、`KYBOTOS_BOARD`(CMake のキャッシュ変数。既定 `waveshare_lcd28`)から
  `SDKCONFIG_DEFAULTS = "sdkconfig.defaults;boards/<board>/sdkconfig.defaults"` を組む。
- **sdkconfig は「無いときだけ defaults から作られる」**ので、同じビルドディレクトリでボードを変えると前のボードの設定が残る。
  これを防ぐために、**ボードごとにビルドディレクトリと sdkconfig を分け**(d)、さらに CMake で
  「`KYBOTOS_BOARD` と sdkconfig の `CONFIG_KYBOTOS_BOARD_*` が一致しなければ止める」確認を入れる。
- 比べた別案: (1) **CMake の変数だけ**で `#define` を渡す → menuconfig から見えず、sdkconfig の他の値(PSRAM など)をボードで変えたくなったときに 2 つの仕組みになる。
  (2) **Espressif の BSP コンポーネント** → 2 枚とも公式の BSP が無く、LVGL などの版が lock から動く(U-29)。(3) **ボードごとに別の ESP-IDF プロジェクト** → ソースの共有が面倒で、3 枚目以降で増える。

**b. ボードの記述の置き場所 — `src/components/board/boards/<board>.h` を 1 枚 1 ファイル、コンパイル時に選ぶ**

- 中身は、ピン(LCD / バックライト / タッチ / I2S / アンプの有効化 / SD / MIDI / 電源キー)と、**「持っている機能」の定数**
  (`KB_HAS_POWER_LATCH`、`KB_TOUCH_IC_CST328` / `KB_TOUCH_IC_GT911`、`KB_LCD_RST`(-1 可)、LCD の向きの 3 値、`KB_AMP_EN_PIN` と有効の極性、`KB_HAS_SDMMC_PINS` など)。
- `board_pins.hpp` が `CONFIG_KYBOTOS_BOARD_*` で 1 つを include する。**今は `touch.cpp`(自前の `#define`)、`audio.hpp`(I2S の既定値)、`app_main.cpp`(電源キーの IO6 / 7)に散っているピンも、ここに集める**。
- **実行時の自動判別はしない**(誤ったボードでピンを叩く前に判別する手段が無い)。

**c. ドライバの分け方**

- **タッチ**: `touch` コンポーネントの中で、IC ごとの読み出し(CST328 / GT911)を分け、LVGL の `indev_read_cb`・注入(`tap` など)・座標の変換は共通に残す。
  **GT911 は手で書く**(CST328 と同じく I2C の新 API で数十行。managed component の `esp_lcd_touch_gt911` は依存の追加と lock の変更になり、Waveshare のビルドにも響く)。
- **音声**: I2S のピンをボードの記述から渡す。アンプの有効化のピン(CrowPanel は IO21 を LOW)を `Audio_Init` に足す。**起動時は無効にしておき、I2S が動いてから有効にする**(ポップ音の対策。readme §8)。
- **電源キー**: `KB_HAS_POWER_LATCH` が無いボードでは、`PowerKey` を作らない(タスクも動かさない)。戻る / ホームのキーを何に割り当てるかは 24c。
- **SD**: ピンだけ。SDMMC のピンを持たないボードはプローブのブロックをコンパイルしない。
- **コンソール**: OTG の USB をつなげば、いまの USB Serial/JTAG の `serial_cmd.cpp` が**そのまま**使える(0-a で確かめる)。UART0 版は、OTG が使えないと分かったときだけ作る。

**d. ビルドディレクトリと sdkconfig**

- **Waveshare(既定)は今のパスのまま**: `src/build` + `src/sdkconfig`(回帰用の `KYBOTOS_DEV_APPS=ON` も今のまま)。
  → workflow §3.2 / §3.4 のコマンドと、この repo の外でファームを組む側(`-B` / `-DSDKCONFIG=` を自分で渡している)は**何も変えずに済む**。
- **それ以外のボード**は `src/build-<board>/` に、sdkconfig もその中(`-B build-<board> -DSDKCONFIG=build-<board>/sdkconfig -DKYBOTOS_BOARD=<board>`)。`.gitignore` に `src/build-*/` を足す。
- 毎回この 3 つを手で書くと間違えるので、**`scripts/fw.sh <board> build|flash|monitor`** にまとめる(ボードの名前から、ビルドディレクトリ・sdkconfig・既定のポートを決める。docker の起動は §3.2 と同じ形)。
  Waveshare もこれで同じものを呼べるが、§3.2 の生のコマンドもそのまま使える。

**e. フラッシュ・モニタ・回帰**

- **ポート**: 2 枚を同時につなぐと、どちらも USB Serial/JTAG なら `/dev/ttyACM0` / `ACM1` の順番が挿す順で変わる。
  **`/dev/serial/by-id/...` の固定名をボードごとに環境変数で持つ**(`KYBOTOS_PORT_<BOARD>`。値はユーザーの環境なので repo に書かず、未設定なら `KYBOTOS_PORT` → `/dev/ttyACM0`)。
  docker の `--device` に by-id のパスを渡せるかは 24 で確かめる(だめなら `readlink -f` した実体を渡す)。
- **回帰**: `scripts/device-regress.sh` はもう `--build-dir` と `KYBOTOS_PORT` を受ける。`--board <board>` を足し、build-dir とポートをボードから決める。
  **ボードごとのしきい値**(`MIN_FREE_INT` など)は、24b で CrowPanel の基準値を取ってから、conf にボード別の上書きを足すか決める。
- **hpane のペイン**: 増やさない。2 枚を並べて動かすのは回帰のときだけで、同時には回さない(§3.4 の注意と同じ趣旨)。

**f. ボードを足す手順(3 枚目以降)**

1. `src/components/board/boards/<board>.h`(ピンと機能)。
2. `src/main/Kconfig.projbuild` の choice に 1 行と、`board_pins.hpp` の include の分岐に 1 行。
3. `src/boards/<board>/sdkconfig.defaults`(choice を選ぶ行と、ボード固有の設定)。
4. 新しい IC があればドライバ(c)。
5. `scripts/fw.sh` のボードの一覧(既定のポートの環境変数の名前)。
6. 起動ログの最初に**ボードの名前を出す**(`board: crowpanel_adv28`)。焼き間違いに気づくため。`fw.sh flash` は、ビルドディレクトリの sdkconfig のボードと引数のボードが一致しなければ止める。

### 0-e 段階の計画(案。承認を求める)

**「Waveshare の水準」の定義(案)**: (1) 回帰 3 本(metronome / mp3player / hostapi_check)が `device-regress.sh` で PASS、
(2) 開始時の `free_int` / `largest_int` と highmark が Waveshare と同じ水準(しきい値を割らない)、(3) MIDI の出力と入力(`midi_loopback`)、
(4) 戻る / ホームのキー。**電源キーでの電源断とバッテリーは含めない**(CrowPanel に無い)。

| 段 | 内容 | 完了条件 |
|---|---|---|
| **24** | 切り替えの枠組み(a〜f)、Waveshare 不変。CrowPanel は **LCD とバックライト**でスプラッシュとメニューまで。**OTG の USB で `ping` / `heap` / `texts` が返る**ところまで(使えれば)。タッチ・SD・音は無効、アンプは止めておく | Waveshare の回帰 3 本 PASS と数値が Phase 23 と同じ。CrowPanel の起動ログ(ボード名、I2C のスキャン)と画面の写真 |
| **24a** | **タッチ**(GT911。INT / RST とアドレス選択の手順を実機で決め、座標を合わせる) | 指でメニューからアプリを起動でき、metronome を操作できる。注入(`tap`)と指が同じ座標になる |
| **24b** | **SD と音**(SDSPI、NS4168 の有効化、L / R の確認) | mp3player と内蔵音源が鳴る(ユーザーの耳)。**回帰 3 本 PASS**、CrowPanel の基準値を記録 |
| **24c** | **MIDI と、電源キーのない操作**(J6 の UART1、BOOT ボタンの割り当て) | MIDI の出力と入力、戻る / ホーム。ここで「Waveshare の水準」に届く |
| 範囲外 | マイク、無線モジュール、ブザー、バッテリー、電源スイッチ、LCD の電源(IO14) | 付けるなら別の段 |

指示書のたたき台(24 / 24a / 24b / 24c / 24d)から変えた点: **コンソールを独立の段(旧 24c)にしない**。OTG の USB で USB Serial/JTAG がそのまま使える見込みのため、24 で疎通だけ見る。
使えなかった場合は、UART0 版のコンソールを 24b の前に挟む。

### ユーザーに確かめてほしいこと

1. **基板の版**(シルクの `V1.x`)。
2. **2 つ目の USB-C(OTG)を PC につなげるか**。つなげるなら、CH340 と両方つないだままでよい(書き込みはどちらからでもできる)。
3. 設計メモ(a〜f)と段階の計画(24〜24c)、「Waveshare の水準」の定義でよいか。

### 0-f ユーザーの回答と承認(2026-10-10)

1. **基板の版は V1.2**(シルク)。0-b の V1.2 の回路図とガイドがそのまま当てはまる。
2. **OTG の USB-C(基板の「USB1」)につなぎ替えた**。`303a:1001 Espressif USB JTAG/serial debug unit` として
   **`/dev/ttyACM0`** に見え、by-id は `usb-Espressif_USB_JTAG_serial_debug_unit_44:1B:F6:8A:6F:04-if00`(MAC が 0-a と一致)。
   → **シリアルのコマンド窓口は USB Serial/JTAG のまま使える見込み**(疎通はステップ 2 で確かめる)。CH340 側は外した。
   - **注意: CrowPanel も Waveshare と同じ `/dev/ttyACM0` になる。** 今の §3.2 のコマンドのまま焼くと、Waveshare 用のファームが CrowPanel に入る。
     e の by-id のポートと、f の 6(`fw.sh flash` のボードの一致の確認)を、ステップ 1 で先に入れる。
3. **設計メモ(a〜f)、段階の計画(24 / 24a / 24b / 24c)、「Waveshare の水準」の定義を承認**。

---

## ステップ 1: 切り替えの枠組み(2026-10-10)

0-d の a〜f のとおりに入れた(コミット `2658988`)。

| 変更 | 内容 |
|---|---|
| `src/main/Kconfig.projbuild` | choice `KYBOTOS_BOARD`(`KYBOTOS_BOARD_WAVESHARE_LCD28` 既定 / `KYBOTOS_BOARD_CROWPANEL_ADV28`)と、名前の文字列 `KYBOTOS_BOARD_NAME` |
| `src/boards/<board>/sdkconfig.defaults` | choice を選ぶ 1 行(CrowPanel はチップが同じなので、それ以外は共通の defaults のまま) |
| `src/CMakeLists.txt` | `KYBOTOS_BOARD`(キャッシュ変数、既定 `waveshare_lcd28`)から `SDKCONFIG_DEFAULTS` を組む。`project()` の後で sdkconfig のボードと食い違えば `FATAL_ERROR` |
| `src/components/board/boards/{waveshare_lcd28,crowpanel_adv28}.h` | ピンと機能(`KB_TOUCH_IC`、`KB_HAS_POWER_LATCH`、`PIN_AMP_EN` / `KB_AMP_EN_ON_LEVEL`、LCD の向きの 5 値、`PIN_SDMMC_*` の有無)。`board_pins.hpp` が `CONFIG_KYBOTOS_BOARD_*` で 1 つを include |
| 散っていたピンを集めた | `touch.cpp` の自前の既定値、`audio.hpp` の I2S の既定値(`Mp3Player()` がボードのピンを使う)、`app_main.cpp` の電源キーの IO6 / 7 |
| `touch.cpp` | `KB_TOUCH_IC_NONE` のボードは I2C を 0x08〜0x77 でスキャンしてログに出すだけ(RST / INT に触らない)。LVGL の入力デバイスは登録するので、注入(`tap`)は効く |
| `audio.cpp` | `PIN_AMP_EN` があれば、起動時にアンプを**止めた状態**に置く(鳴らすのは 24b) |
| `app_main.cpp` | 最初に `APP: board: <board>`。`KB_HAS_POWER_LATCH` が 0 のボードは電源キーのタスクとコールバックを作らない |
| `scripts/fw.sh`(新規) | `fw.sh <board> build / flash / monitor / info`、`fw.sh list`。flash / monitor は sdkconfig のボードを確かめる。ポートは `KYBOTOS_PORT_<BOARD>`(by-id を `readlink -f` して docker に渡す) |
| `scripts/device-regress.sh` | `--board <board>`(ビルドディレクトリとポート)。ポートを `readlink -f` |
| `.gitignore` | `src/build-*/` |

0-d の f の 5「`fw.sh` のボードの一覧」は要らなくなった(`fw.sh` は `src/boards/` を見る)。

**気づいたこと**: 旧 `board_pins.hpp` の `static constexpr i2c_port_t I2C_TOUCH_PORT = I2C_NUM_0` は効いていなかった。
`touch.cpp` は `#ifndef I2C_TOUCH_PORT`(マクロの有無)で見ていたので、`I2C_NUM_1` が使われていた。挙動を変えないよう、ボードの記述は `I2C_NUM_1` をマクロで書いた
(回帰のログで `Touch online port=1 addr=0x1A`)。

**確認**

| 確認 | 結果 |
|---|---|
| Waveshare を §3.2 の**今までのコマンドのまま**ビルド | 通る(`Kybotos board: waveshare_lcd28`)。sdkconfig に `CONFIG_KYBOTOS_BOARD_WAVESHARE_LCD28=y` が足された。警告は今回触っていない 2 件(`midi.cpp` の `uart_config_t::flags`、esp-audio-player)だけ |
| 食い違いの確認 | CrowPanel のビルドディレクトリに `-DKYBOTOS_BOARD=waveshare_lcd28` → `sdkconfig is for board 'crowpanel_adv28' but KYBOTOS_BOARD is 'waveshare_lcd28'` で止まる(戻して再構成し直した) |
| **Waveshare の実機の回帰 3 本**(`fw.sh waveshare_lcd28` + `KYBOTOS_DEV_APPS=1` で焼き、`device-regress.sh --board waveshare_lcd28`。`captures/phase24-waveshare/report.md`) | **PASS**。開始時の free_int **150,232**、largest_int **98,304**、metronome の反復 3 回 +0、mp3player −472 / hostapi_check −176(`EXPECT_DELTA` どおり)、WARN / ERROR 0 件、metronome の highmark **25,744**。**Phase 23 と 1 バイトも同じ**。起動ログに `APP: board: waveshare_lcd28` |
| この repo の外のビルド(sequencer を埋め込む側の、`-B` / `-DSDKCONFIG=` を自分で渡すスクリプト)を**変えずに**ビルド | 通る(既定の Waveshare として組まれ、その sdkconfig に `KYBOTOS_BOARD_NAME="waveshare_lcd28"`) |
| Linux ホスト | 変更なし(ホスト側のコードに触れていない) |

## ステップ 2: CrowPanel の起動(2026-10-10)

`fw.sh crowpanel_adv28 build` → `flash`(`KYBOTOS_PORT_CROWPANEL_ADV28` に by-id)。起動ログ `captures/phase24/crowpanel-boot.log`:

```
octal_psram: vendor id : 0x0d (AP) / density : 0x03 (64 Mbit)
esp_psram: Found 8MB PSRAM device / Speed: 80MHz / SPI SRAM memory test OK
APP: board: crowpanel_adv28
DISPLAY: lvgl draw buf: active 0x3c120998 (PSRAM) size 19200, cfg 19200 B x2
TOUCH_CST328: I2C quick scan (port=1, 100kHz, 0x08-0x77):
TOUCH_CST328:   - found addr 0x38 (responded to receive)
W TOUCH_CST328: no touch driver for this board (SDA=15 SCL=16); touch injection only
AUDIO/MP3: amplifier enable pin GPIO21: off
APP: Audio_Init: free heap 225928 -> 179700 (delta 46228)
APP: heap after seq init: free 176108, largest block 131072
SDCARD: Using SPI host=2 MOSI=6 MISO=4 SCLK=5 CS=7      (SDHC、約 32GB)
WASM/LAUNCH: menu: 3 app(s) listed
KBCMD: ready
```

| 確認 | 結果 |
|---|---|
| 画面 | スプラッシュからメニューまで。**向きは Waveshare と同じ設定で正しい**(ユーザーが目で確認。色の反転・欠けの指摘なし)。LCD の向きの 5 値は Waveshare と同じ値のまま |
| シリアルのコマンド窓口(ネイティブ USB) | `ping` → `pong`、`heap`、`ls`(sequencer / metronome / mp3player。カードは Waveshare で使っていたもの)、`texts` → `idle` |
| アプリ | `run metronome` → `texts` で `Metronome` / `120bpm` / `4/4` / `flick up / down` → `tap 281 202` 2 回(再生 / 停止)→ `stop`。**停止時の free_int が開始時と同じ(155,448)**、metronome の highmark **25,744**(Waveshare と同じ) |
| メモリ | メニューの状態で free_int **172,424** / largest_int 122,880(Waveshare は 150,232 / 98,304)。電源キーのタスクと CST328 の分などが無いぶん多い。アプリ実行中の free_int は 155,344 |
| タッチ | **0x38 だけが応答した**(GT911 の 0x5D / 0x14 は応答なし)。工場出荷時のファームが GT911 のドライバで 0x5D を使っていたのとは違い、wiki とデータシートの **FT6336U(FT5x06 系、0x38)** と合う。24a は FT6336 として進める(チップ ID のレジスタで確かめる) |
| SD | **指示書の段階の計画では 24b だが、回路図で確かめたピンでそのままマウントできた**(読み書きの検証と速度は 24b) |
| 音 | I2S は CrowPanel のピンで動いている。アンプは止めたまま(音は出ない) |

カメラの静止画は最初の 2 枚が全面黒(平均輝度 0。カメラが向いていなかった)。カメラを向け直して撮り直した:

- `captures/phase24/cam_still_153604.png`: メニュー(`Kybotos Menu`、ヘッダ右の `Settings`、緑の行、`SD ready`)。向き・配色とも Waveshare と同じ。
- `captures/phase24/cam_still_153632.png`: シリアルの `run metronome` で起動した metronome(ヘッダ `120bpm 4/4`、▶、巨大な `120` と `4/4`、拍の枠)。

撮り直しのときの `ls` は 6 本(hostapi_check / midi_loopback / synth_probe を含む)で、最初の起動の 3 本から増えていた。
CrowPanel に焼いたのは `KYBOTOS_DEV_APPS` を渡していない普段使いのファームで、検査用アプリを置くファーム(ON)は Waveshare にしか焼いていないので、
**ユーザーは 1 枚の SD カードを 2 枚のボードで共用している**(Waveshare の回帰の後にカードを CrowPanel へ移した)。ファームは SD のアプリを消さないので、どちらのボードのランチャーにも、もう片方が置いたアプリが並ぶ(`.wasm` はボードによらないので、そのまま動く)。

## ステップ 3: 文書(2026-10-10)

- `README.md` / `README.ja.md`: ボードの表(名前と対応の状態)、既定以外のボードは `fw.sh`。
- `docs/architecture.md` 11-13: ボードの切り替えの決定(選び方、ボードの記述、焼き間違いの防止、比べた別案)。
- `docs/workflow.md` §3.2「ボードの指定」(`fw.sh`、`KYBOTOS_PORT_<BOARD>` に by-id、生のコマンドは常に既定のボード、ボードを足す手順)、
  §3.4 `--board` と CrowPanel のつなぎ方(ネイティブ USB 側)。
- `docs/lessons.md`: 2 件(下)。
- `docs/roadmap.md`: 24 を done に、24a / 24b / 24c を planned で足した。

## 完了条件の確認

| 完了条件 | 結果 |
|---|---|
| 1. 調査、比較表、設計メモ、段階の計画(承認済み) | ✅ ステップ 0(0-a〜0-f) |
| 2. ボードの名前の指定だけで両方をビルドできる。指定しないときは Waveshare | ✅ `fw.sh <board> build`。§3.2 の生のコマンドは Waveshare |
| 3. Waveshare: 回帰 3 本 PASS、数値が Phase 23 と同じ、外のビルドが変えずに通る、Host API / ABI・`.wasm` 不変 | ✅ 1 バイトも同じ |
| 4. CrowPanel: スプラッシュとメニュー(写真と起動ログ)、工場出荷時のファームの吸い出し | ✅ |
| 5. ボードを足す手順 | ✅ workflow §3.2 |

## 次の段(24a)への引き継ぎ

- **タッチは 0x38 の FT6336U(FT5x06 系)**として書く(0x5D / 0x14 は応答なし)。チップ ID(FT6336 の 0xA3 など)を読んで確かめる。
  INT=IO47、RST=IO48(回路図)。リセットで GPIO1 / 2 を叩くサンプルの手順(GT911 のアドレス選択)は要らない見込み。
- 座標の向き: 液晶は Waveshare と同じ向きの設定で正しく出た。タッチの生の座標の向きと範囲は実機で合わせる(`map_basic_to_display` の回転)。
- `KB_TOUCH_IC` に `KB_TOUCH_IC_FT6336` を足し、`touch.cpp` の CST328 の読み出しと並べる(LVGL の読み取り・注入・座標の変換は共通)。
- CrowPanel の開始時の free_int は Waveshare より約 22KB 多い(172,424)。24b で回帰を回すときは、ボード別の基準値として記録する。
