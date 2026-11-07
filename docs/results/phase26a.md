# Phase 26a 実施記録: CrowPanel の残量計(I/O 基板の MAX17048)

指示書: `docs/prompts/phase26a.md`

## ステップ 1: 実装(2026-11-07、ビルドまで)

ユーザーが不在の間に実装し、両ボードのビルドが通ることまで確かめた。**実機の確認(ステップ 2)はまだ**。

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

## ステップ 2: 実機の確認

(未実施。指示書のステップ 2 の項目を、ユーザーが戻ってから行う。満充電の電圧の測定(roadmap U-40)は 26a の実機確認の前に済んだ: TP1 4.15 V。)
