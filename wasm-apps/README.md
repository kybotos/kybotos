# wasm-apps

MidiAppBox 上で動かす WASM アプリ(Rust, `wasm32-unknown-unknown`, no_std)。

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

## アプリ一覧

| アプリ | 内容 |
|---|---|
| `touch_demo/` | Phase 6A 検証。`hostapi_poll_event` のタッチイベントを座標・DOWN/UP カウントで可視化、ボタンタップでクリック音 |
| `mp3player/` | Phase 6B〜。`hostapi_audio_*` で MP3 を制御(PLAY/PAUSE/STOP/VOL±、FINISHED 検知)。6C でファイル列挙+プレイリスト対応 |
| `metronome/` | Phase 7B/7C/7D → **Phase 13 で音楽時間軸 API に全面書き直し**。メトロノーム本体。可変 BPM(40-240、±5/±1・長押し連打加速)・拍子(2/3/4/6)・START/STOP・拍ランプ・音量調整(V-/V+)。クリックは `seq_write`(port=CLICK / OP_TONE)で playback tick に予約し、MIDI Clock は L1 がグリッドから生成する(アプリは `hostapi_midi_send` を呼ばない)。小節頭は 1568Hz のアクセント音 |
| `midi_loopback/` | Phase 9b → **Phase 14 で音楽時間軸 API に移行**。MIDI ループバック診断アプリ。`hostapi_transport_start/stop` + `tempomap_set_tempo/meter`(BPM120固定)で自機 MIDI OUT の 24ppqn クロックを駆動し、`hostapi_midi_recv` で自機 MIDI IN の受信を診断表示(Stage1: 受信生バイトの16進表示・累積バイト数、Stage2: 実測 BPM、Stage3: クロック間隔の min/max/σ・公称値との偏差・受信数 vs 期待数、E1: ヒストグラム・ロバスト統計・外れ値・見かけBPM分布)。可聴クリックは供給しない(受信統計に条件を絞る判断) |
| `seq_smoke/` | Phase 11。新 Host API 12 関数(transport / tempomap / seq / time_us_to_tick)の恒久スモークテスト。タップ不要で一巡し、8 項目の合否をビットで画面表示 + CC#119/#120 で外部出力する。実機と Linux ホストで同一 `.wasm` を走らせて比較できる |
| `sequencer/` | **Phase 18 → 18a で操作系を刷新 → 18b でヘッダに集約 → 19 で Song**。Menu / **Song 一覧 / arrangement / Chapter** / Session 一覧 / Session 画面の 6 画面(**rect 10 / text 12**)。**戻るは HW キーとヘッダ左のパンくず**(最上位の終了は HW キーのみ)、進むのは 1 タップか長押し(成立で点滅)、スクロールは縦スワイプ、**BPM はヘッダ右の長押し + 左右ドラッグ(シャトル)**。ヘッダ下のステータス行に **`1` / `RPT`(文字色で ON/OFF)と ▶ / ■**。Session の単体再生(トグル 4 通り、再生中小節の点滅、小節の長押しで次の小節境界へジャンプ)。**Phase 18c/18d で編集を追加**: 行頭の `-` の長押しで Session / 小節を削除、最後の次の行の `+` のタップで追加、**拍子は小節ごとに持ち、拍子の表示を長押し + 上下左右ドラッグで変更**(左右 = 分子、上下 = 分母)。再生中の Session は編集不可、永続化は Phase 20。**Phase 19 で Song / 19a でタイル表示**: arrangement の画面は**横 4 × 縦 2 のタイル**(左上に `✕` + Chapter 名、合計小節数)で、**タップ = ジャンプ**・**長押しで編集モード**(そのタイルが点滅し `✕` が出る。`✕` = 削除 / 名前 = 名称変更(プリセット + ダッシュ)/ ドラッグ = 並べ替え / 本体 = Session リスト)・**最後の次の `+` で新規 or シャドーを追加**・縦スワイプでスクロール。arrangement を通して再生し、**Session 境界で Program Change をキューモード(+64)で 4 分音符 1 つ前に送る**(SL MK3 の Session 切り替え)。Song / Chapter 枠 / Session 参照の増減も `-` / `+` でできる。曲構造と時間軸は `seqcore`、ジェスチャと画面スタックは `appui`。**Phase 20 で永続化**: Menu に **`Save File` / `Load File`** があり、**Bank(全 Session + 全 Song)を SD カードの 8 スロット**(`BANK1.MBB` 〜 `BANK8.MBB`、データルートは実機 `/sdcard/data` / Linux `./sdcard/data`)に 保存 / 読み込みできる。**空きへの保存は 1 タップ、上書きと読み込みは長押し**、**再生中は読み込めない**。形式は `seqcore::serial` の **`MBBK` v1**(36 B ヘッダ + 明示レイアウト + チェックサム)で、**壊れたファイルは一時 Bank で弾いてから差し替える**。**起動時の自動ロードはしない**ので、デモデータは従来どおり `app_init` で組み立てる。設計は `docs/results/phase18.md` / `phase18a.md` / `phase18b.md`、操作規約は `docs/design/ui-conventions.md` |
| `synth_probe/` | **Phase 21 の検証用**(回帰 6 本には入れない)。タップ不要で 120bpm / 4/4 を走らせ、4 小節を 1 周として **(1) 4 音を同じ tick に (2) 16 分のハイハット 16 発 (3) クリック + ドラム (4) 12 発でボイス 8 本を溢れさせる** を繰り返す。さらに **小節 8 で `test.mp3` を再生し小節 12 で止める**(MP3 との受け渡しを物理操作なしで確認するため)。判定は **Linux の WAV 書き出し**(`MIDIBOX_WAV_OUT=<path>` で起動)と、実機はカメラ録音 + 耳 |
| `appui/` | **アプリではなくライブラリ**(Phase 18a、18b で拡張)。デバイス共通の UI 部品。タップ / 長押し / **シャトル(長押し + ドラッグ)** / スワイプの判定(`Gesture`)と画面スタック(`ScreenStack`)を持つ。Host API に依存しない `no_std` crate で、`.wasm` は作らない。規約は `docs/design/ui-conventions.md`、テストは `cargo test --features std`(18 件) |
| `seqcore/` | **アプリではなくライブラリ**(Phase 16、**Phase 20 で永続化の `serial` を追加**)。Sequencer app のコア(データモデル・解決規則・Transport 状態機械・**Bank のシリアライズ**、`docs/apps/sequencer/spec.md` §3–4)。Host API に依存しない `no_std` crate で、`.wasm` は作らない(ファームにも埋め込まない)。テストは `cargo test --features std`、no_std でビルドできることの確認は `cargo build --release --target wasm32-unknown-unknown` |
