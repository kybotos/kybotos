# Phase 26a 実施記録: CrowPanel の残量計(I/O 基板の MAX17048)

指示書: `docs/prompts/phase26a.md`

## ステップ 1: 実装(2026-11-07、ビルドまで)

ユーザーが不在の間に実装し、両ボードのビルドが通ることまで確かめた。実機の確認はユーザーが戻ってから(ステップ 2)。

### ボードの記述

- `board_pins.hpp`: `KB_BATT_SRC`(`KB_BATT_SRC_ADC` = 既定、Phase 25 の分圧 + ADC / `KB_BATT_SRC_FUEL_GAUGE` = I2C の残量計)と
  `KB_I2C_BUS_RECOVER`(既定 0)を足した。残量計なのに `KB_FG_I2C_ADDR` が無ければビルドを止める。
- `crowpanel_adv28.h`: `KB_HAS_BATT_SENSE 1`、`KB_BATT_SRC KB_BATT_SRC_FUEL_GAUGE`、`KB_FG_I2C_ADDR 0x36`、`KB_I2C_BUS_RECOVER 1`。
- Waveshare の記述は変えていない(ADC のまま)。

### 中身

| どこ | 何をするか |
|---|---|
| `touch.cpp` | `KB_I2C_BUS_RECOVER` のとき、バスを作る前に SDA を見て、LOW なら SCL を最大 9 回たたいて STOP を出す(残量計は電池から給電されていてリセットをまたいで動くので、読み出しの途中でリセットされると SDA を握ったままになりうる)。回復したときだけ警告を出す。`touch_i2c_bus()` でバスを他のデバイスに渡す |
| `battery.cpp`(残量計) | `AttachI2cBus()`: タッチのバスに 0x36 を足し、VERSION を読んで応答を確かめて(ログ `fuel gauge MAX17048 at 0x36, version 0x....`)読み出しのタスクを起動。応答が無ければ警告を出して何もしない(バッジは出ない)。タスクは 2 秒ごとに VCELL / SOC / CRATE を読む(ログは出さない。スタック 2048B は ADC と同じ) |
| 電圧と残量 | 電圧 = VCELL × 78.125 µV。**残量 = チップの SOC**(ModelGauge の推定、0〜100 に丸める)。電圧からの表の値(Phase 25)は比較のためにシリアルにだけ出す |
| USB 給電の推定 | CRATE が +2.0 %/h を超えたら外部給電、−2.0 %/h を下回ったら電池(間は前の状態のまま)。起動時の値は Phase 26 のとおり(キーを押して起動 = 電池)。バッジの見え方(25a の白抜き)がこれに従う |
| `app_main.cpp` | `touch.init()` の直後に `battery::AttachI2cBus(touch_i2c_bus())`(残量計のボードだけ) |
| シリアルの `batt` | 残量計のとき `batt mv=… pct=… src=fg soc=… crate=±… table_pct=… ext=0|1`。ADC のボードは前と同じ `batt mv=… pct=…` |

- **残量計には書き込まない**(既定の設定のまま)。既定ではしばらく充放電が小さいとハイバネートに入り、測るのが 45 秒ごとになる。
  バッジの用途なら足りると見ているが、ステップ 2 の値を見て決める(書き込むならユーザーの承認を取る。指示書のゲート)。
- Host API / ABI・`.wasm` は不変。Linux のホストはこのコードを使わない。

### ビルド

| ボード | 結果 | アプリの大きさ |
|---|---|---|
| CrowPanel(`KYBOTOS_DEV_APPS=1`) | 通った(新しい警告なし) | 0x12fa30 |
| Waveshare(`KYBOTOS_DEV_APPS=1`) | 通った(新しい警告なし) | 0x132cc0 |

## ステップ 2: 実機の確認(2026-11-07)

満充電の直後(Phase 26 の U-40 の測定のすぐ後: テスターで TP1 4.15 V / TP2 4.15 V、USB は挿したまま)に、CrowPanel に `KYBOTOS_DEV_APPS=1` のファームを焼いて確かめた。ログは `captures/phase26a/`。

| 確認 | 結果 |
|---|---|
| 起動時の残量計 | ✅ `BATT: fuel gauge MAX17048 at 0x36, version 0x0012`。スキャンでも 0x36 / 0x38 / 0x48 |
| `batt`(満充電、USB で起動) | ✅ `batt mv=4168 pct=97 src=fg soc=97.2 crate=+0.8 table_pct=96 ext=1`。テスターの 4.15 V と +18 mV(目安の ±20 mV 以内) |
| 画面が点いたまま USB を抜く(電池で動き続ける) | ✅ バッジの白抜きが消え(ユーザーが確認)、そのうち 97% → 96% |
| USB をまた挿す | ✅ CHG の LED が赤(電池で動いているときに挿したので充電が始まった。U-38 の使い方のとおり)、しばらくして 97% に戻り、白抜きになった(ユーザーが確認) |
| リセットを 6 回(モニタの起動し直し) | ✅ 6 回ともタッチと残量計が応答。バスの回復が要った回は無かった(読み出しの最中にリセットが当たったわけではないので、回復の処理そのものはまだ実機で動いていない) |
| USB だけで電池なし | (実施せず。値が出ること自体は Phase 26 で分かっている) |

### 回帰で見つかった内部 RAM の減りと、読み方の変更

最初の形(専用のタスク「battery」、スタック 2048B)で CrowPanel の回帰を回すと、PASS だったが **free_int が 149,196 → 146,752(−2,444B)**、hostapi_check の終了時が 146,104 で
**`MIN_FREE_INT`(146,000)との余裕が 104B** しかなかった。

- 残量計の読み出しは 2 秒ごとに 3 回の短い I2C で、バッジの更新のタイマ(2 秒、LVGL のタスク)と周期が同じ。タッチも同じバスを LVGL のタスクで読んでいる。
- そこで **タスクをやめ、バッジのタイマの先頭で `battery::PollGauge()` を呼ぶ**形にした(`b4ab776`)。残量計のハンドルは、起動時の確認が通ってから atomic で渡す(タイマはその前から動いている)。
- 作り直したあとの `batt`: `mv=4164 pct=97 src=fg soc=96.9 crate=+1.9 table_pct=96 ext=1`。USB の抜き挿しの判定は同じコードなので見え方の確認はやり直していない。

## ステップ 3: 回帰と後片付け(2026-11-07)

### 回帰

| ボード | 結果 | free_int(アプリの開始時) | 前との差 |
|---|---|---|---|
| CrowPanel + I/O 基板(`captures/phase26a-regress-crowpanel/`) | **PASS**(3 本、警告 0) | 149,156 | −40B(Phase 26 の 149,196 から) |
| Waveshare(`captures/phase26a-regress-waveshare/`) | **PASS**(3 本、警告 0) | 147,076 | ±0(終了時 146,428 も同じ。`MIN_FREE_INT` との余裕 428B) |

- Waveshare の `batt` は前と同じ形(`batt mv=4176 pct=97`)。Host API / ABI・`.wasm` は不変。conf は変えていない。

### 後片付け

- 一時のコードは入れていない。
- **Waveshare に別の SD を用意した**(ユーザーが FAT32 にフォーマット。区画 1 つ)。初回の起動でファームが `/sdcard/apps`(埋め込みのアプリ)・`/sdcard/music`(MP3 3 つ)・`/sdcard/data` を作った。
  非公開の sequencer はファームに埋め込んでいないので入っていない。
- 両ボードを普段使い(`KYBOTOS_DEV_APPS=0`)のファームに戻し、両方の SD から検査用の 3 本(hostapi_check / midi_loopback / synth_probe)を `rm` で消した。
  SD のアプリ: CrowPanel は sequencer / metronome / mp3player、Waveshare は metronome / mp3player。

## まとめ

- CrowPanel + I/O 基板で、電池の電圧と残量がバッジと `batt` に出る(電圧はテスターと +14〜18 mV)。USB 給電の見え方は充放電の向きで切り替わる。
- 残るもの: 残量計の設定(ハイバネート、電池のモデル)は既定のまま。SOC の精度を放電のログで見るのは、Phase 25b の表の見直しと一緒に(roadmap)。
