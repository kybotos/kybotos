# Kybotos

[English](README.md) | **日本語**

Kybotos(キボトス)は、サンドボックス化された WASM アプリを小さな音楽デバイスで動かすためのプラットフォームです。
アプリは Host API だけを通して、画面・タッチ・音・MIDI・音楽の時間軸(テンポ / 拍子 / 予約発音)を使います。
同じ `.wasm` が、ESP32-S3 の実機でも Linux のホストでも動きます。

[![Demo video](https://img.youtube.com/vi/UdiFrxvP_qg/0.jpg)](https://youtu.be/UdiFrxvP_qg)

## 構成

| パス | 中身 |
|---|---|
| `shared/hostapi_defs.h` | **Host API の定義**(ホストとアプリの契約)。解説は `docs/hostapi.md` |
| `src/` | ESP32-S3 のファームウェア(ESP-IDF 5.5、WAMR 2.4)。ランチャー、ランタイム、音・MIDI・表示・タッチ・SD |
| `hosts/linux/` | Linux のホスト(SDL2 / ALSA)。実機と同じ `.wasm` を同じ Host API で動かす開発・検証用 |
| `shared/` | 両ホストで共有するコード(音楽の時間軸のスケジューラ `seq_core.c` など) |
| `wasm-apps/` | サンプルアプリ(Rust, `wasm32-unknown-unknown`, no_std)と、UI 部品のライブラリ `appui` |
| `scripts/` | 回帰・測定・キャプチャのスクリプト |
| `docs/` | アーキテクチャ、Host API、UI の規約、ロードマップ、各フェーズの記録 |

対象のハードウェアは **Waveshare ESP32-S3-Touch-LCD-2.8**(320×240 のタッチ液晶、I2S の音声出力、SD カード)に、
MIDI の入出力(UART)をつないだものです。

ボードはビルドのときに選びます(Phase 24)。

| ボード | 名前(`KYBOTOS_BOARD`) | 状態 |
|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-2.8 | `waveshare_lcd28`(既定) | すべての機能 |
| Elecrow CrowPanel Advance 2.8"(V1.2) | `crowpanel_adv28` | 対応中: 画面・SD・シリアルのコマンド(ネイティブ USB の USB-C)。タッチ・音・MIDI は順に対応する(`docs/roadmap.md`) |

## ファームウェアのビルドと書き込み

ESP-IDF の入ったコンテナイメージを使います。

```
docker run --rm -v ${PWD}:/workspaces/kybotos -w /workspaces/kybotos/src \
  ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5 \
  bash -c 'source /opt/esp-idf/export.sh && idf.py build'

DEV=/dev/ttyACM0
docker run --rm -it -v ${PWD}:/workspaces/kybotos -w /workspaces/kybotos/src \
  --device=${DEV} --group-add $(stat -c '%g' ${DEV}) \
  ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5 \
  bash -c "source /opt/esp-idf/export.sh && idf.py -p ${DEV} flash"
```

- 上のコマンドは既定のボード(Waveshare)です。他のボードは `scripts/fw.sh <ボード> build|flash|monitor` を使います
  (ボードごとに `src/build-<ボード>/` でビルドし、別のボード用のビルドは焼かない。`docs/workflow.md` §3.2)。
- 使う部品(managed component)の版は `src/dependencies.lock` で固定しています。ESP32-S3 はメモリに余裕が少ないので、
  版を上げるときは回帰で基準値を取り直します(`docs/workflow.md` §3.2)。
- 初回の起動で、ファームに埋め込んだアプリ(metronome と mp3player)が SD カードの `/sdcard/apps` に置かれ、ランチャーに並びます。
  `wasm-apps/dev/` の検査用・診断用アプリは、`idf.py -DKYBOTOS_DEV_APPS=ON build` でビルドしたときだけ入ります(回帰に使います)。
- ファームは SD のアプリを消しません。古いファームから更新すると、それが置いたアプリ(touch_demo / seq_smoke / midi_loopback /
  synth_probe)がランチャーに残るので、シリアルコンソール(`idf.py monitor` で `rm touch_demo` のように入力)の `rm <app>` で消してください。

## Linux ホスト

```
sudo apt install cmake gcc libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev libasound2-dev
cd hosts/linux
cmake -B build && cmake --build build -j
./build/kybotos_host                                        # ランチャー(../../wasm-apps を探す)
./build/kybotos_host ../../wasm-apps/metronome/metronome.wasm  # 1 本だけ起動
```

詳しくは `hosts/linux/README.md`。

## アプリを作る

- アプリは Rust の `cdylib` を `wasm32-unknown-unknown` 向けにビルドした `.wasm` です。手順は `wasm-apps/README.md`。
- Host API は `shared/hostapi_defs.h` と `docs/hostapi.md`。タップ・長押し・スワイプと画面スタックは `wasm-apps/appui`、
  操作の規約は `docs/design/ui-conventions.md`。
- この repo の外で作ったアプリも、ビルド時に `KYBOTOS_EXTRA_APPS`(`.wasm` の絶対パスの一覧)を渡せばファームに埋め込めます
  (`src/components/wasm_runtime/CMakeLists.txt`)。Linux ホストは `.wasm` のパスを引数に取るので、どこにあるアプリでも起動できます。

## 回帰

metronome / mp3player と、Host API をひととおり叩いて合否を画面に出す検査アプリ hostapi_check(`wasm-apps/dev/`)の 3 本を、
実機と Linux ホストで回します。タップは実機のシリアルコンソールと Linux ホストのコマンドの入口(FIFO)から注入し、画面の文字を
読み出して手順ごとに確かめます。ヒープの差分・警告・WAMR のメモリ消費も機械的に判定します(`scripts/device-regress.sh` /
`scripts/linux-regress.sh`、手順は `docs/workflow.md`)。

## ライセンス

**Apache License 2.0**(`LICENSE`、`NOTICE`)。

アプリは WASM のサンドボックスの中で動き、Host API を通してだけホストを呼び出します。
**Host API を使うだけのアプリは、このリポジトリのコードの派生物とは考えていません。** アプリは、作者が選んだライセンス
(非公開・有料を含む)で配布できます。

### サードパーティ

ビルド時に ESP-IDF の Component Manager が取得する部品(repo には含まれません):

| 部品 | ライセンス |
|---|---|
| LVGL | MIT |
| esp_lvgl_port / esp_lcd_touch(Espressif) | Apache-2.0 |
| WAMR(WebAssembly Micro Runtime) | Apache-2.0 WITH LLVM-exception |
| esp-audio-player | Apache-2.0 |
| esp-libhelix-mp3 | ラッパーは Apache-2.0。中の **Helix MP3 デコーダーは RealNetworks Public Source License(RPSL)** |

- **ファームウェアのバイナリを配布する場合**、RPSL により、Helix のソースコードが RPSL の条件で入手できることの通知
  (ドキュメントと、著作権表示を置く場所)が必要です。このリポジトリは現在バイナリを配布していません。
- `hosts/linux/font8x8_basic.h` はパブリックドメイン(Daniel Hepper)。
- `src/components/wasm_runtime/assets/*.mp3` はこのプロジェクトで生成したテスト用の音で、この repo のライセンスに従います。
