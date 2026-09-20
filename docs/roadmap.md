# MidiAppBox ロードマップ

**このファイルがフェーズ計画の唯一の情報源である。**

- フェーズ計画に変更が生じたら、**このファイルだけを更新する**。
- 更新のタイミングは**次フェーズの指示書(`docs/prompts/phaseXX.md`)を書くとき**。
  そのとき、完了したフェーズの実績と、新たに判明した課題を反映する。
- `docs/results/` に書かれたフェーズ計画は**当時の判断の記録(スナップショット)**であり、書き換えない。
- 現在地の詳細は `docs/status.md`、各フェーズの詳細は `docs/results/phaseXX.md` を参照する。

各フェーズは次のサイクルで進める。

```
docs/roadmap.md            ← 本ファイル。フェーズの目的・状態・完了条件の要点
docs/prompts/phaseXX.md    ← フェーズ契約。着手前にコミットし、スコープ変更は末尾に日付付きで追記
docs/results/phaseXX.md    ← 調査・計画・実施記録・実測値・トラブル(実装記録もここ。docs/dev-log.md は 2026-08-23 に廃止)
```

- 状態: `planned` / `in progress` / `done` / `deferred`
- 承認ゲート: Host API / ABI の変更、回帰 conf のスキーマ変更は必ず明示承認を経る
- 各フェーズの指示書は前フェーズの results を前提にして書く。results に書かれていない前提は、指示書に書き込んでから着手する

---

## ① フェーズ計画

### Sequencer App(現在のトラック)

仕様: `docs/apps/sequencer/spec.md`

| Phase | 目的 | 状態 | 完了条件の要点 | 依存 |
|---|---|---|---|---|
| **16** | シーケンサーコア(データモデル + 解決規則 + Transport 状態機械)を host 非依存の `no_std` crate として実装し、単体テストする。~~SL MK3 の PC 挙動を実機で確認する~~(公開仕様どおりと確認済みのため削除)。Host API のギャップを分析する | **done**(2026-09-13) | **`wasm-apps/seqcore/`**(依存 0、no_std、**34 tests**)。Q1: ch16 PC 0..=63 は即時・+64 でパターン末尾へキュー → Session 境界はキューモード、**`PC_LEAD_TICKS = 24`**(暫定)。Q3: 上限定数は据え置き(**Bank 9,842 B**)。Q4: **H1–H6 は既存 API で足りる。新規は H8(テンポ / 拍子マップのリセットと枯渇回避)と H9(境界同期の停止)**。指示書: `docs/prompts/phase16.md` / 記録: `docs/results/phase16.md` | Phase 15 |
| **17** | Host API の追加: **テンポ / 拍子マップのリセットと長時間再生での枯渇回避(H8、必須)、小節境界での停止(H9、推奨)**。Linux/SDL と実機の両方に実装する。拍・小節イベントの通知と境界同期の locate は不要と判定済み(Phase 16) | **done**(2026-09-13) | **方式 B'**: `hostapi_tempomap_clear()`(STOPPED のみ)+ **満杯時だけ通過済み区間を畳む**(小節番号の起点を保持)+ **`HOSTAPI_SEQ_OP_STOP`**(その tick のクロックは出さない)。既存アプリの挙動は不変。**Linux の C 単体テスト 10/10**(偽の時計)、selftest と seq_smoke(12 項目)が両ホストで全 PASS、**V3: 最終区間 288 発・Stop 後 0 発**(両ホスト)、**V4 metronome 100.00% / 外れ値 0 / 単峰 / 20832.8µs**、回帰 5 本 PASS。指示書: `docs/prompts/phase17.md` / 記録: `docs/results/phase17.md` | 16 |
| **18** | Session 画面 + 単体再生。メトロノーム、`1` / 繰り返しトグル、点滅表示。Menu と Session 一覧 | **done**(2026-09-13) | **`wasm-apps/sequencer/`**(`.wasm` 14,819 B)。3 画面 / トグル 4 通り / 長押しジャンプ / BPM± を実機の録画(T1〜T8)で確認。計画ロジックは **`seqcore::timeline::Planner`**(テスト 34 → 44)。S02「全小節 1 回」= **720 発ちょうど・停止後 0 発**(実機・Linux)。**回帰 6 本 PASS**(U-2 の反復 3 回判定を追加)。**Linux の画面キャプチャが取れるようになった**(ウィンドウ指定の `import -window` / `xwd -id`、U-12 クローズ)。実機の WAMR プール残 **5.7KB**、Linux はプールを 96KB に拡大。指示書: `docs/prompts/phase18.md` / 記録: `docs/results/phase18.md` | 17 |
| **18a** | **対話規約の確定と Sequencer への適用**(Phase 18 の試用で判明したずれ)。HW ボタンで 1 階層戻る・最上位でアプリ終了、`BACK` / `OPEN` / スクロールボタンの廃止、長押しの点滅フィードバック、スワイプでのスクロール、下段(BPM / トグル)の置き直し | **done**(2026-09-20) | **規約は `docs/design/ui-conventions.md`(新設)**、決定記録は `architecture.md` §11-11。Host API は非破壊に 2 つ追加 — **`HOSTAPI_EV_TOUCH_MOVE`**(8px 間引き + 末尾 MOVE の畳み込み)と**任意 export `app_key(key_id, action) -> i32`**(戻り値 0 = ホストの既定動作 = 停止。**既存 5 本の `.wasm` は未変更のまま従来どおり**)。Linux は Backspace = 戻る / ESC = 強制終了。**新 crate `wasm-apps/appui`**(ジェスチャ + 画面スタック、テスト 16 件)。sequencer は 7 行 + 下段 3 セル + Tempo 画面、**`.wasm` 15,255 B**。実機は T1〜T10 を録画で確認(強制ホーム・metronome の互換を含む)、S02 = 720 発ちょうど / 停止後 0 発、**回帰 6 本 PASS**。**実機 WAMR プールの残りは 3,432 B**(U-16)。指示書: `docs/prompts/phase18a.md` / 記録: `docs/results/phase18a.md` | 18 |
| **18b** | **ヘッダに操作を集約する**(18a の試用で出た 3 点)。パンくず(ヘッダ左)のタップで 1 階層戻る、BPM は Tempo 画面をやめて**長押し + 左右ドラッグのシャトル**、下段 3 ボタンをやめて**1 行のステータス表示**(`1` / `RPT` は**文字色**で ON/OFF、Play/Stop は **▶ / ■**) | planned(指示書作成済み) | ステップ 0 の承認(**色付きテキストの import 追加** = roadmap U-15 に着手、**▶ / ■ が両ホストで出るかの実測**、ステータス行のスロット予算、シャトルの段階表)→ 実装。**Host API の追加は色付きテキスト 1 シンボルだけ**。**実機プールの余裕が 2KB を切ったら報告して停止**(U-16)。**今回は回帰テストを行わない**(ユーザー指示の例外。次フェーズで必ず回す)。指示書: `docs/prompts/phase18b.md` | 18a |
| **19** | Song / Chapter 画面と arrangement 再生。Session 境界での PC 送信、SL MK3 との end-to-end | planned | SL MK3 の Session 切り替えデモ動画。**着手前に実機 WAMR プールの余裕(Phase 18 時点で 5.7KB)と `.wasm` 16KB の扱いをステップ 0 で決める** | 18b |
| **20** | 永続化(SD カード上の Bank)と最低限の編集(追加 / 削除 / 並べ替え)。Song / Chapter へのトグル横展開の設計 | planned | Bank のファイル形式、編集 UI、Q7 の設計判断 | 19 |
| 21〜 | 拡張: ドラムマシン → コードプレーヤー → フレーズ録音(順序は 20 の完了時に再評価) | deferred | — | 20 |

### マイルストーン

- **Ogaki Mini Maker Faire 2026(12/5–6)**: 出展の最低ラインは Phase 19 の完了(SL MK3 と組み合わせた弾き語りデモ)。出展申込は 9 月中に IAMAS 公式で確認する。

### 完了したフェーズ(要約)

| Phase | 内容 | 完了 | 記録 |
|---|---|---|---|
| 0〜5 | PoC。ESP32-S3 上の WAMR、Host API v0/v1、SD カードからアプリを起動するランチャー(10 サイクル leak-free) | — | `phase00.md` / `phase01-03.md` / `phase04.md` / `phase05.md` |
| 6 | タッチ入力(6A)、MP3 再生とファイル列挙(6B/6C)、デモモード分岐の解消(6D) | — | `phase06.md` |
| 7 | 予約発音(7A)、メトロノーム(7B)、DMA 二重クリック修正(7B-fix)、トーンパレット(7C)、テンポ 1 刻み・音量(7D) | — | `phase07.md` |
| 8a / 8b / 8c | MIDI OUT 疎通 / MIDI Clock 出力 Host API / MIDI IN ハードウェア検証 | — | `phase08a.md` / `phase08b.md` / `phase08c.md` |
| 9a / 9b / 9c | `hostapi_midi_recv` / ループバック診断アプリ / **クロック欠落の原因特定(毎拍の位相リセット)** | 〜2026-08-23 | `phase09a.md` / `phase09b.md` / `phase09c.md` |
| 10 | 新アーキテクチャの調査と設計確定(L0〜L3、Clock Authority、音楽時間軸 API) | 2026-09-05 | `phase10.md` |
| 11 | 音楽時間軸 API 12 関数を実機・Linux の両方に実装(`shared/seq_core.c` に共通化) | 2026-09-06 | `phase11.md` |
| 12 | 基盤整備(16MB パーティション、アプリ 10→6 本、自動回帰スクリプト、PSRAM 可否) | 2026-09-06 | `phase12.md` |
| 13 | metronome を新 API で書き直し。**クロック欠落 0 / 100.00% / BPM 単峰** | 2026-09-06 | `phase13.md` |
| 14 | 旧経路の削除(`click_schedule` / `tone_schedule` / `midi_send` の副作用)。回帰対象は 5 本に | 2026-09-06 | `phase14.md` |
| 15 | PSRAM 本番反映(WASM linear memory と LVGL バッファを PSRAM へ、回帰指標を 4 値に改訂) | 2026-09-12 | `phase15.md` |
| (番外) | check-workflow / check-workflow-routine(herdr 運用の確立)、av-sync-fix、screensaver | 2026-07〜09 | 同名の `docs/results/*.md` |

### 計画の変更履歴

| 日付 | 変更 |
|---|---|
| 2026-09-20 | **Phase 18a 完了。続けて Phase 18b の指示書を作成した**(`docs/prompts/phase18b.md`)。18a の成果を試用したユーザーから 3 点 — (a) **戻るはパンくず(ヘッダ左)のタップでもできるのが自然**、(b) **BPM は Tempo 画面に入るのではなく、長押し + 左右ドラッグ(シャトル)で変えたい**、(c) **下段の `1` / `RPT` / `PLAY` の個別ボタンは冗長。ヘッダのような 1 行にまとめ、文字色と ▶ / ■ で状態を示したい**。**決定(ユーザー回答)**: 文字色は **`hostapi_draw_text` を変えずに色付き import を 1 つ追加**(= roadmap **U-15 に着手**。既存 `.wasm` は再ビルド不要)、BPM は**シャトル方式**(変位 = 速さ、段階的)、パンくずタップは **1 階層戻る**。**今回は例外的に回帰テストを行わない**(ユーザー指示。次フェーズで必ず回す)。**U-16(実機 WAMR プールの残り 3,432 B)は 18b でも「2KB を切ったら停止」のゲートとして扱い、拡大自体は Phase 19 のステップ 0 で決める** |
| 2026-09-20 | **Phase 18 完了。あわせて Phase 18a の指示書を作成した**(`docs/prompts/phase18a.md`)。Phase 18 の成果を実際に試用したユーザーから、**この PoC で初めて登場した「階層」の扱いが目指す操作イメージと合っていない**という指摘が出たため、Phase 19(Song / Chapter で階層がさらに増える)の前に**デバイス共通の対話規約を決める回**を挟む。要求は 5 点 — (a) `BACK` ボタンを廃止し **HW ボタン(電源キー短押し)で 1 階層戻る・最上位でアプリ終了**(iOS のイメージ)、(b) `OPEN` ボタンを廃止し**タップまたは長押しで進む。長押しは成立したら点滅**でフィードバックする、(c) スクロールボタンを廃止し**スワイプでスクロール**、(d) 下段の BPM± / トグルが画面を専有しすぎるので置き直す、(e) 思想として**専用ボタンを極力設けずジェスチャに寄せる**。**スコープの決定(ユーザー回答)**: 18a は設計 + 実装まで(**Host API の追加を含む** — 戻るは現状の「短押し = 即終了」では実現できない)、適用は **sequencer のみ**(既存 5 本は `.wasm` 未変更のまま従来どおり動くことを保証)、スワイプは **`TOUCH_MOVE` の追加**(12 バイト ABI のまま type 追加)、下段の扱いはステップ 0 で案を比較して承認。**描画 API(gfx)の拡張は 18a に含めない**(U-15、足りなければ報告して止まる)。あわせて **U-2 と U-12 をクローズ**(Phase 18 で実装 / 解決)、**Phase 18 の残課題から U-16〜U-18 を起こした** |
| 2026-09-13 | **Phase 18 の指示書を作成**(`docs/prompts/phase18.md`)。**棚卸しで 3 点を取り込んだ**: (a) **描画 API の制約** — draw_text / fill_rect は座標キーの retained モデルで各 16 スロット、**一度使った座標は解放されず、画面を消す API も無い**。1 つのアプリで 3 画面を切り替えるには座標の組を使い回す必要があるので、**スロットの予算表をステップ 0 の必須論点**にした。重なり順が実機(後から作ったものが上)と Linux(rect → text)で違う点も明記した。**Host API は変えず、足りなければ報告して止まる**ゲートにした。(b) roadmap の完了条件にあった「SDL での動作動画」は、この環境で **Linux の画面キャプチャが取れない**(U-12)ため、**動画は実機のみ・Linux はログ**に改めた。(c) **U-2(PSRAM の N 回反復リーク判定)を Phase 18 に取り込んだ**(Bank を持つ Sequencer を回帰に加えるフェーズのため)。あわせて Phase 16 の残課題(Session scope の PC、トグル変更の締め切り)と spec の Q5 / Q6 をステップ 0 で決めることにした |
| 2026-09-13 | **Phase 17 完了。** 方式 B'(`tempomap_clear` + 満杯時の畳み込み + `OP_STOP`)。**設計メモから変えた点が 3 つ**(`results/phase17.md`「仕様からの逸脱」): 遅れて発火した `OP_STOP` の停止位置の式、テンポを畳む範囲を現在のテンポ区間の開始まで、`set_meter` の挿入位置の再探索。検証の副産物として、**`midi-clock-probe` が停止中のクロックを黙って捨てていた**ことが分かり集計行を足した。実機と Linux の回帰を同時に走らせると、実機の MIDI 出力を Linux ホストが受けて警告が出る(運用上の注意として Phase 18 の指示書に反映) |
| 2026-09-13 | **Phase 17 の指示書を作成**(`docs/prompts/phase17.md`)。内容は Phase 16 の結論どおり H8(マップのリセットと枯渇回避、必須)と H9(境界停止、推奨)に絞った。**棚卸しで 2 点を取り込んだ**: (a) 自動剪定は **metronome が再生中に `transport_locate(0)` で後ろへ戻る**こと、**seq_smoke が `set_loop` を検査する**ことと衝突しうるので、ループ / locate と共存する剪定規則をステップ 0 の必須論点にした。(b) L1 のディスパッチャに手が入るので、**metronome の MIDI クロック絶対値目標(V4)を完了条件に入れた**。あわせて、マップ上限・キュー深さを増やして解決することを禁止した(internal 静的 BSS、§9)。`midi-clock-probe` は再生区間しか集計しないので、停止後クロック数の集計は必要ならツール変更として提案する |
| 2026-09-13 | **Phase 16 完了。** `wasm-apps/seqcore/`(依存 0・no_std・34 tests)。**Phase 17 の中身を指示書の想定から変えた**: 当初は「境界同期のテンポ / 拍子切替、拍・小節イベント」だったが、H1–H3 は既存 API(未来の at_tick 指定、`get_position` のポーリング)で足りると判定した。song tick を単調なタイムラインとして使えば境界同期の locate も要らない。**残る穴はテンポ / 拍子マップ(H8: `transport_start` で消えない・上限 32 件で長時間再生すると枯渇)と境界停止(H9)**なので、Phase 17 はこの 2 点に絞る。方式は A(seq 制御 op)/ B(`tempomap_clear` + 剪定 + `OP_STOP`、推奨)/ C(start でクリア、既存契約を壊すため不可)を比較済み。**指示書から変えた点**: SL MK3 実験の削除(公開仕様で回答)、`Bank.sessions` を `Option` 配列にしない(niche で `.wasm` が太る) |
| 2026-09-13 | **本ファイルを「① フェーズ計画 / ② フェーズ未割当の課題」の 2 部構成に再編した**(Phase 16 のステップ 0)。あわせて次のずれを直した: (a) 仕様のパスを `docs/sequencer/spec.md` から **`docs/apps/sequencer/spec.md`** へ。(b) サイクル図の `docs/dev-log.md` は 2026-08-23 に `docs/results/` へ分割済みなので削除。(c) 完了フェーズの表を `docs/status.md` と照合して書き直した(旧表は 9a に「MIDI Start/Stop/Continue」と書いていたが、実際の担当は Phase 11 の `transport_*`)。(d) 「割り込み候補」と「保留 / 見送り」を ② に統合した。**MIDI Clock 精度の項目(U-1)は再発が無いことをユーザーが確認したのでクローズし、Phase 19 の依存から外した。** あわせて **Phase 16 のスコープから SL MK3 の実機実験を外した**(SL MK3 の PC 仕様は公開情報どおりとユーザーが確認。指示書に追記済み) |
| 2026-09-13 | ロードマップを新設し、Sequencer トラック(Phase 16〜21)を計画した(`952ebaf`) |

---

## ② フェーズ未割当の課題

**番号を振り直さずに課題を置いておく場所。** 着手できる状態になったら ① のフェーズに移すか、新しいフェーズを起こす。
クローズした課題は行を消さず、取り消し線と経緯を残す。

| # | 課題 | 出所 | 温度感 |
|---|---|---|---|
| ~~U-1~~ | **✅ クローズ(2026-09-13、ユーザー確認。再発なし)。** ~~MIDI Clock 精度: 120bpm の送信が SL MK3 で 115–119bpm と検出される問題~~。原因は Phase 9c で特定した毎拍の位相リセット(9c の BPM 分布の 115 / 120 二峰性と一致)。Phase 11 でグリッド生成に置き換え、Phase 14 で旧経路を削除した。Phase 13 で SL MK3 の検知テンポが表示値と一致することをユーザーが目視で確認し、Phase 15 の T-1 でも欠落 0 / 100.00% | `phase09b.md` / `phase09c.md` / `phase13.md` / `phase15.md`、spec Q8 | — |
| ~~U-2~~ | **✅ クローズ(2026-09-20、Phase 18 で実装)。** ~~PSRAM のリーク監視が「1 回の起動→停止の差分」までしかない~~。`device-regress.sh` に反復判定を入れた(既定 3 回、seq_smoke は 1 回。各回の差分 +0 に加えて **N 回の終了時 `free_int` / `free_psram` がすべて同じ**ことを判定する) | `phase15.md` 申し送り / `phase18.md` | — |
| ~~U-3~~ | **➡ Phase 17 へ移した(2026-09-13)。** ~~テンポ / 拍子マップの上限が 32 件で、エントリを消す語彙が無い~~。演奏中のテンポ変更を小節頭に積み続けると枯渇する(metronome は locate(0) で回避)。Phase 16 の分析で、**`transport_start` でもマップが消えない**ことも分かり、Sequencer の要求 H8 になった | `hostapi.md` §6 要件 1 の注記、`phase16.md`「Host API ギャップ分析」 | — |
| U-4 | **`hostapi_midi_recv` のタイムスタンプに線速補正を適用していない**(意味の変更を伴う) | `hostapi.md` §7 / `architecture.md` §11-4 | フレーズ録音系(21〜)の着手前 |
| U-5 | **内蔵音源ポートの追加(移行ステップ 6)** | `architecture.md` §10、`phase14.md` | ドラムマシン拡張(21〜)の前提 |
| U-6 | **Strategy B(`.wasm` バッファを PSRAM に置く)。** 現状はすべての `.wasm` が 16KB 未満なので internal のまま | `phase15.md`、`architecture.md` §9 | いずれかの `.wasm` が 16KB を超えたとき(Phase 18 で計測。**非ゼロ初期値の static データは `.wasm` を太らせる**点に注意) |
| U-7 | **IDF 6.0 への移行。** `espressif/wasm-micro-runtime` の IDF 6 対応待ち(W^X / `MALLOC_CAP_EXEC`) | `docs/notes/idf6-migration-notes.md` | コンポーネント側の対応が公開されたら |
| U-8 | **SDMMC ネイティブモードと PSRAM の共存。** 同じピンを SDMMC → SPI3 と再初期化する 2 段遷移が PSRAM 有効時に不安定。main は SDSPI 固定で運用中 | `phase15.md` ステップ 4 | SD から高速転送が必要になったときだけ |
| U-9 | **エクスプレッションペダル**(ADS1115 経由の `hostapi_analog_read`) | 旧ロードマップ「割り込み候補」 | Sequencer 完了後 |
| U-10 | **ブラウザを第 3 ホストにする**(TypeScript で Host API を実装)。移植点は `shared/seq_core.c` のフック 7 個 | 旧ロードマップ「割り込み候補」、`phase11.md` | Phase 20 以降。Sequencer が最初の移植対象になる |
| U-11 | **Linux ホストの回帰を `timeout N ./build/midibox_host <wasm>` 方式にする。** SIGTERM が `SDL_QUIT` に変換され、xdotool なしで `app stopped` まで完走する | `docs/lessons.md`(14) | workflow §1 の変更にあたるため**ユーザー承認が要る**。急がない |
| ~~U-12~~ | **✅ クローズ(2026-09-20、Phase 18 で解決)。** ~~Linux ホストの画面キャプチャの自動化~~。x11grab が黒くなるのは画面全体を読むためで、**ウィンドウ ID を指定する `import -window` / `xwd -id` なら取れる**。`scripts/screen-still.sh` / `screen-rec.sh` を切り替え済み(手順は `docs/workflow.md` §3.6) | `check-workflow.md` / `phase18.md` | — |
| U-13 | **120bpm 以外での系統誤差の確認、Song Position Pointer の送出** | `phase09c.md`(持ち越し)、`hostapi.md` §3 | Song の途中から再生する機能を作るとき(SPP) |
| U-14 | **MIDI IN の受信ダンプ機能は `feature/midi-in-rx-dump` ブランチにしか無い**(main 未マージ) | `phase08c.md` | 必要になったとき |
| U-15 | **描画 API(gfx)の制約。** draw_text / fill_rect は座標キーの retained モデルで各 16 スロット、一度使った座標は解放されない、画面を消す・移動する API が無い、~~文字色は白固定~~、重なり順がホストで違う(実機は作成順、Linux は rect → text) | Phase 18 の指示書作成時の棚卸し(2026-09-13)、`phase18.md`、`phase18a.md`(rect 12 / text 12 で 4 画面) | **➡ 文字色は Phase 18b で着手**(色付き import を追加)。**スロット数・解放・消去 API は未解決のまま**で、Song / Chapter 画面が増える Phase 19 で再燃しうる。拡張は Host API 変更なので承認ゲート |
| U-16 | **実機 WAMR プールの余裕が 3,432 B しかない**(Phase 18a の実測。sequencer のロードに 45,528 B / 48,960 B)。**`.wasm` +436 B に対しプールは +2,312 B 増えた(限界費用は約 5 倍)**ので、UI を足すフェーズではここが先に効く。候補: (1) プールを PSRAM に置いて広げる、(2) internal のまま増やす(`largest_int` 57,344 との兼ね合いを実測)、(3) アプリのコードを削る | `phase18.md` / `phase18a.md` 申し送り | **Phase 19 のステップ 0 で必ず決める。** Phase 18b でも「余裕 2KB を切ったら停止」のゲートとして扱う |
| U-17 | **新しい対話規約(HW ボタンで戻る / 長押し / スワイプ)を既存アプリへ横展開する。** mp3player のスクロールボタンなどが対象。Phase 18a は sequencer だけに適用し、既存 5 本は回帰の基準として据え置く | Phase 18a の指示書作成時のユーザー回答(2026-09-20) | 規約が `docs/design/ui-conventions.md` に固まり、実アプリ 1 本で運用してから |
| U-18 | **`device-regress.sh` は「アプリが `stop` を待たずに自分で止まった」を「停止しない」と判定する。** `stop` の応答が `stop idle` なら別の理由(「保持中に停止した」)として出すと切り分けが速い。あわせて**電源キー短押しがログを出さない**ため、消去法でしか原因を言えなかった(短押しのログ出力は Phase 18a のステップ 0 の論点に入れた) | `phase18.md` 残課題 | `scripts/` の変更なので提案 → 承認。Phase 18a のついでに直してもよい |
