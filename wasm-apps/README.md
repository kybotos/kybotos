# wasm-apps

Kybotos 上で動かす WASM アプリ(Rust, `wasm32-unknown-unknown`, no_std)。

ビルド済み `.wasm` はファームウェアに埋め込むためリポジトリにコミットする
(ESP-IDF ビルダーコンテナに Rust が無いため、ビルドはホスト側で行う)。

## 必要なもの

```
rustup target add wasm32-unknown-unknown
```

## ビルド手順(全アプリ共通)

```
cd wasm-apps/<app>
cargo build --release
cp target/wasm32-unknown-unknown/release/<app>.wasm ./<app>.wasm
```

各 `<app>.wasm` が `src/components/wasm_runtime` の CMake から EMBED_FILES で参照される。
この repo の外のアプリも、ビルド時に `KYBOTOS_EXTRA_APPS`(`.wasm` の絶対パスの一覧)を渡せば
同じように埋め込まれ、初回に SD の `/sdcard/apps` へ置かれる(`src/components/wasm_runtime/CMakeLists.txt`)。
Linux ホストは `.wasm` のパスを引数に取るので、どこにあるアプリでも起動できる。

## アプリ一覧

| アプリ | 内容 |
|---|---|
| `touch_demo/` | Phase 6A 検証。`hostapi_poll_event` のタッチイベントを座標・DOWN/UP カウントで可視化、ボタンタップでクリック音 |
| `mp3player/` | Phase 6B〜。`hostapi_audio_*` で MP3 を制御(PLAY/PAUSE/STOP、FINISHED 検知)。6C でファイル列挙+プレイリスト対応。**Phase 21c で VOL±(`V-` / `V+`)を外した**(音量は装置の設定のミキサー) |
| `metronome/` | Phase 7B/7C/7D → **Phase 13 で音楽時間軸 API に全面書き直し**。メトロノーム本体。可変 BPM(40-240、±5/±1・長押し連打加速)・拍子(2/3/4/6)・START/STOP・拍ランプ。拍の音は `seq_write` で playback tick に予約し、MIDI Clock は L1 がグリッドから生成する(アプリは `hostapi_midi_send` を呼ばない)。**Phase 21c で音を内蔵音源へ移した**(`port=SYNTH`、小節頭 = note 34 Metronome Bell / 他 = 33 Metronome Click。音量はミキサーの Synth で、**Click の MUTE / 音量は効かない**)。**音量調整(V-/V+)は 21c で外した** |
| `midi_loopback/` | Phase 9b → **Phase 14 で音楽時間軸 API に移行**。MIDI ループバック診断アプリ。`hostapi_transport_start/stop` + `tempomap_set_tempo/meter`(BPM120固定)で自機 MIDI OUT の 24ppqn クロックを駆動し、`hostapi_midi_recv` で自機 MIDI IN の受信を診断表示(Stage1: 受信生バイトの16進表示・累積バイト数、Stage2: 実測 BPM、Stage3: クロック間隔の min/max/σ・公称値との偏差・受信数 vs 期待数、E1: ヒストグラム・ロバスト統計・外れ値・見かけBPM分布)。可聴クリックは供給しない(受信統計に条件を絞る判断) |
| `seq_smoke/` | Phase 11。新 Host API 12 関数(transport / tempomap / seq / time_us_to_tick)の恒久スモークテスト。タップ不要で一巡し、8 項目の合否をビットで画面表示 + CC#119/#120 で外部出力する。実機と Linux ホストで同一 `.wasm` を走らせて比較できる |
| `synth_probe/` | **Phase 21 の検証用**(回帰には入れない)。タップ不要で 120bpm / 4/4 を走らせ、4 小節を 1 周として **(1) 4 音を同じ tick に (2) 16 分のハイハット 16 発 (3) クリック + ドラム (4) 12 発でボイス 8 本を溢れさせる** を繰り返す。さらに **小節 8 で `test.mp3` を再生し小節 12 で止める**(MP3 との受け渡しを物理操作なしで確認するため)。判定は **Linux の WAV 書き出し**(`KYBOTOS_WAV_OUT=<path>` で起動)と、実機はカメラ録音 + 耳 |
| `appui/` | **アプリではなくライブラリ**(Phase 18a、18b で拡張)。デバイス共通の UI 部品。タップ / 長押し / **シャトル(長押し + ドラッグ)** / スワイプの判定(`Gesture`)と画面スタック(`ScreenStack`)を持つ。Host API に依存しない `no_std` crate で、`.wasm` は作らない。規約は `docs/design/ui-conventions.md`、テストは `cargo test --features std`(18 件) |

Sequencer(`sequencer` / `seqcore`)は非公開の app-sequencer で開発している。この repo の Host API と `appui` の上に載る
(プラットフォーム側の経緯は `docs/results/phase16-21-platform.md`)。
