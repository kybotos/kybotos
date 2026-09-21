# Phase 21: ドラムマシンの土台(WAMR プールの PSRAM 化 + 内蔵音源ポート)

- 契約日: 2026-09-21
- 参照: `docs/results/phase20.md`(直前の到達点)、`docs/architecture.md` §3 / §7 / §9 / §10(ステップ 6)、
  `docs/hostapi.md` §5 / §7、`shared/hostapi_defs.h`、`shared/seq_core.c` / `seq_core.h`、
  `src/components/audio/audio.cpp` / `audio.hpp`、`src/components/seq/clock_authority.hpp` / `seq.cpp`、
  `src/components/wasm_runtime/wasm_runtime.cpp`、`hosts/linux/hostapi_sdl.c` / `hostapi_seq.c`、
  `docs/results/phase07.md`(7B-fix の二重クリック)、`docs/results/phase13.md`(V4 の精度目標)、
  `docs/results/phase19b.md`(`.wasm` バッファの PSRAM 化 = U-6 の前例)
- 結果報告先: `docs/results/phase21.md`

## 目的

**ドラムマシン(Phase 21a のパターン画面)が載るための土台を 2 つ作る。**

1. **メモリの余裕**: WAMR プールを internal の静的 BSS から **PSRAM** へ移し、
   `.wasm` の伸びしろを internal RAM の天井から切り離す。
2. **内蔵音源ポート(roadmap U-5)**: `HOSTAPI_PORT_SYNTH` を実装し、
   **複数の音が同時に鳴る**ようにする。

Phase 21a(パターン画面)は本フェーズでは**作らない**。ただし 21a の形は既に決めてあるので、
本フェーズの設計はそれを満たすこと(下記「Phase 21a の前提(決定済み)」)。

## 決定済みのスコープ(2026-09-21 のユーザー回答)

- **ドラムの音は内蔵音源で出す。** roadmap **U-5(内蔵音源ポートの追加、移行ステップ 6)を
  ドラムマシン画面より先にやる**(U-5 の温度感欄に「ドラムマシン拡張(21〜)の前提」と書いてあるとおり)。
- **音の作り方は「まず合成、後でサンプル」。** Phase 21 は **ノイズ + 減衰サインの合成**(808 / 909 系)で
  ポリフォニックなミキサの土台を作り、**ボイスの中身を後からサンプル再生に差し替えられる境界**にしておく。
  サンプル(WAV)の読み込みは本フェーズに含めない。
- **メモリの手当ては WAMR プールを PSRAM へ移す**(プールを 128KB に増やす案・別アプリに分ける案は採らない)。
- **フェーズは 2 つに割る**: **Phase 21 = メモリ + 音源**、**Phase 21a = パターン画面**。
  19b で U-6 を先にやってから機能を積んだのと同じ順序。
- **パターンの永続化(SD への保存)は Phase 21a にも含めない**(さらに次)。

### Phase 21a の前提(決定済み。本フェーズでは実装しない)

設計の受け皿としてだけ書く。**この形が成立するように Phase 21 の音源とメモリを作ること。**

- **Menu に `Drum Machine` を足す**(現在 `Session` / `Song` / `Save File` / `Load File` の 4 行)。
- **Pattern は Session に紐付ける**のが最終形だが、**紐付け自体は後のスコープ**。
  Session の長さと Pattern の長さは**一致しなくてよく**、**開始する小節を指定して繰り返し再生**する。
- **パターン画面は Song のタイルと同形の 4 列 × 2 行**。
  **1 画面 = 1 拍(16 分音符 × 4)= 4 列**、**縦は 2 サウンド**。
  **横スクロールで拍・小節を移動、縦スクロールでサウンドの組を切り替える。**
  → **Song タイルの座標(`TILE_*`、75×88px)をそのまま使い回せるので描画スロットは増えない。**
- **サウンドは Hi-Hats / Kick / Snare / Crash あたりの 4 音**(2 音ずつ表示)。
- **タイルの有効 / 無効で ON/OFF**、**長押しで領域によって ON/OFF やベロシティを設定**(Song タイルと似た I/F)。

## 前提(調査済み。ただし着手時にソースで確認すること)

- **P1: 内蔵オーディオは完全な単音・逐次ブロッキング合成である。**
  `src/components/audio/audio.cpp` の `tone_write_now()` が
  **1 音を最後まで `i2s_write` で書き切り、続けて DMA リング 6 本をゼロで埋める**(7B-fix の
  二重クリック対策)。依頼は深さ **4** の `tone_queue_` に積むだけで、`click_task_loop` が
  **1 つずつ直列に再生する**。したがって **Kick と Hi-Hat の同時発音は原理的にできない**。
  発音のたびに `ensure_i2s(44100, 16, true)` で I2S を戻す点にも注意。
- **P2: Linux ホストは既にコールバック(pull)型のミキサになっている。**
  `hosts/linux/hostapi_sdl.c` の `audio_callback()` が `Voice s_voice`(単声、再帰振動子)を
  合成し、`host_seq_on_audio()` でフレーム数を Clock Authority へ渡す。
  **N ボイス化は素直**で、**本フェーズの大工事は実機側**である。
- **P3: L0 は SYNTH ポートを捨てている。** `shared/seq_core.c` の `port_dispatch()` は
  `HOSTAPI_PORT_DIN_OUT` と `HOSTAPI_PORT_CLICK` だけを処理し、**`USB_MIDI` / `SYNTH` は
  `default: break`**(「予約のみ」とコメントがある)。**`seqcore_hooks_t` にフックを 1 つ足せば通せる。**
  ディスパッチは**ロックの外**、実機は `ESP_TIMER_TASK` コンテキスト、
  **`FIRE_ADVANCE_US = 20µs`** 早めに発火する。`EMIT_MAX = 16`。
- **P4: Clock Authority のレートマスターは I2S の `on_sent`(ISR)だが、実機の `NowUs()` は
  「esp_timer µs + 固定オフセット」に帰着している**(`clock_authority.hpp` の冒頭。P10-2 で
  I2S と esp_timer が同一 XTAL の厳密同比であることを実証済み)。
  **ミキサ化で時間軸の意味は変わらない見込みだが、`OnFormatChanged` のアンカー張り替えと
  `on_sent` の発火が途切れないことは実測で確かめること。**
- **P5: MP3 は同じ I2S を別経路で叩く。** `esp_audio_player` が `Mp3Player::write_fn` 経由で
  書き、**レートも 22.05kHz 等へ変わる**。**mp3player は回帰 6 本の 1 本**なので、
  ミキサと MP3 の共存(または排他)の扱いを決めて壊さないこと。
- **P6: WAMR プールは internal の静的 BSS。** `src/components/wasm_runtime/wasm_runtime.cpp` の
  **`static uint8_t s_wamr_heap[112 * 1024]`** を `wasm_runtime_full_init` に
  `Alloc_With_Pool` で渡しているだけなので、**確保方法の差し替えは局所的**。
  **19b で `.wasm` バッファを `heap_caps_malloc(MALLOC_CAP_SPIRAM)` へ移した前例**があり、
  そのとき **PSRAM 上のバイトコード実行で遅くならない**ことも実測済み(`app_tick` avg 2,332 → 1,207µs)。
  **`architecture.md` §9 の表で「WAMR プール = internal 固定」と宣言している**ので、
  ここを書き換えるのは方針変更にあたる(ゲート 4)。
  なお internal 固定の元の理由(最大連続ブロックを linear memory に残す)は
  **Phase 15 の PSRAM 化で既に失効している**(§9 に明記済み)。
- **P7: メモリの現在地**(Phase 20 実測): `.wasm` **39,383 B**、
  実機プール **101,384 / 114,496(残り 13,112 B)**、停止時 `free_int` **39,900** /
  `largest_int` **15,360**、しきい値 **`MIN_FREE_INT=32000` / `MIN_LARGEST_INT=8192`**。
  **限界費用は `.wasm` +1 B につきプール +2.42 B** なので、
  **残り 2KB のゲートまで `.wasm` の伸びしろは約 4.6KB しかない**
  (Phase 20 の永続化は +8,031 B だった)。**これが本フェーズでプールを動かす理由である。**
  Linux ホストのプールは `hosts/linux/main.c` の **192KB**。
- **P8: 描画スロット**は rect **18〜19 / 24**、text **29 / 32**。**本フェーズでは増やさない**
  (音源とメモリだけなので画面は増えない)。
- **P9: L0 キューの深さは 256 イベント(4KB、internal 静的 BSS)。**
  `architecture.md` §9 の表で **4 声部 × 16 分 = 128 イベント/小節 → 256 で 2 小節ぶん**と
  見積もってある。**Planner は 1 小節先読み**なので 21a でも足りる計算だが、
  **アプリ側の `PEND_MAX = 40`(`wasm-apps/sequencer/src/lib.rs`)は 21a で要拡大**
  (linear memory は PSRAM なので `.bss` の拡大はコストゼロ)。**本フェーズでは触らない。**
- **P10: 音量はマスター 1 つ。** `hostapi_audio_set_volume` は **MP3 とクリックの両方**に効く
  (v2 で再定義済み)。内蔵音源のレベルをこれとどう関係づけるかは設計事項。
- **P11: 検証用アプリを足すのは安い。** `.wasm` はファームに `EMBED_FILES` で埋め込み、
  `launcher.cpp` が SD へ seed する(現在 6 本)。**7 本目を足しても消えるのは flash だけ**で、
  RAM も WAMR プールも食わない(起動したアプリぶんしか載らない)。

## ゲート(必須)

1. **ステップ 0(設計メモ)の報告 → 承認**を経てから実装に入る。
2. **Host API の追加は承認ゲート**(既定の運用)。語彙・シグネチャ・戻り値を設計メモで提案し、
   **承認を得てから**実装する。**既存 API のシグネチャと意味は変えない**
   (既存 5 本の `.wasm` を再ビルドしない。X-macro への追加なら既存アプリは不変 — 18b / 19a / 20 の前例)。
3. **`seqcore_hooks_t` の拡張は両ホストで同一の契約にする**(片方だけに生やさない)。
4. **WAMR プールの PSRAM 化は `architecture.md` §9 の方針変更**にあたるので、
   **実測(プール消費 / `free_int` / `largest_int` / `app_tick` の avg・max)を添えて承認を得る。**
   **うまくいかなかったら静的 BSS に戻して報告する**(粘らない)。
5. **MIDI Clock の精度が Phase 13 の V4 目標から悪化したら止まる。**
   目標は **欠落 0 / clocks÷expected 100.00% / BPM 単峰 / 平均間隔 20832.8µs**
   (`scripts/midi-clock-probe.sh`、`docs/workflow.md` §3.5)。
   **オーディオ出力の作り直しは Clock Authority のレートマスターに触る**ので、これは必須の観測点。
6. `scripts/` / `docs/workflow.md` / `CLAUDE.md` の変更は提案 → 承認。
7. **一時コード(計測・検証用)は入れた直後にも `git diff` で確認**し、撤去したことも `git diff` で確認する
   (`docs/workflow.md` §3.2。Phase 18a / 19 の教訓)。

## スコープ

### 含む

#### ステップ 0: 設計メモ(実装なし・承認ゲート)

`docs/results/phase21.md` のステップ 0 節に、次をすべて書く。

- **a. WAMR プールの PSRAM 化**
  - 確保の方法(`heap_caps_malloc(MALLOC_CAP_SPIRAM)` / `heap_caps_calloc` / アラインメント)、
    **失敗したときの扱い**(internal へフォールバックするか、起動を止めるか)。
  - **プールをいくつにするか。** 単に今の 112KB を移すのか、**21a の見込みから逆算して増やす**のか。
    増やすなら **`.wasm` +1 B ≒ プール +2.42 B**(P7)からの試算を書く。
  - **PSRAM 上でインタプリタを回す影響**: `create_exec_env` の **WASM スタック 8KB もプール上**に載る。
    19b の前例(バイトコードは PSRAM で遅くならなかった)との違い(スタックは呼び出しごとに触る)を
    評価する観点と、**実測項目**(`app_tick` の avg / max、jitter 統計)を決める。
  - **internal で取り戻せる量**(静的 −112KB)と、それを**何に使わないか**
    (**L0 キュー・テンポマップ・タスクスタック・DMA は internal 固定のまま**。§9 の表は動かさない)。
  - **回帰しきい値(`scripts/device-regress.conf` の `MIN_FREE_INT` / `MIN_LARGEST_INT`)を
    どう書き換えるか。** 今回は**値が上がる**方向なので、新しい基準値に合わせて**引き上げる**
    (下げっぱなしにしない)。**PSRAM 側の `MIN_FREE_PSRAM` も見直す**(プールぶん減る)。
  - **`architecture.md` §9 の表と「WAMR プールの大きさ」節をどう書き換えるか。**

- **b. ミキサの設計(実機。本フェーズの本体)**
  - **ブロック長**(例: 240 フレーム = 5.44ms @44.1kHz)、**ボイス数**、
    **常時書き込みにするか / 鳴っているときだけ書くか**。
  - **7B-fix の二重クリック対策をどう引き継ぐか。** 現在は「発音のたびに DMA リング 6 本をゼロ埋め」
    だが、**常時書き込みなら原理的にアンダーフローしない**ので不要になる可能性がある。
    **不要と判断するなら、その理屈と実測(1 拍ごとのクリックで再発しないこと)を書く。**
  - **MP3 との共存**(P5): 排他にする / ミキサに合流させる / MP3 中は SYNTH を鳴らさない。
    **mp3player の回帰を壊さないこと**が条件。**レート切替(`ensure_i2s` / `OnFormatChanged`)の
    扱い**も書く。
  - **タスクの優先度・スタック(静的 BSS)・CPU 見積もり**。既存の `click` タスクは
    優先度 18 / スタック 4096(静的)。**ヒープから取らない**(6B / 7B-fix の教訓)。
  - **CLICK ポート(`tone_define` / `tone_play` / `OP_TONE`)をどうするか。**
    **推奨: 残したままミキサのボイス 1 本に載せ替える**(メトロノームの挙動は不変)。
    `metronome` / `sequencer` / `seq_smoke` の `.wasm` を**再ビルドしないこと**が条件。

- **c. 発音タイミングの精度**
  - L0 のディスパッチは µs 精度(`FIRE_ADVANCE_US = 20`)だが、**ミキサはブロック単位**。
    **ブロック内のサンプルオフセットで発音するか、ブロック先頭に丸めるか。**
    丸めるなら **ブロック長がジッタの上限**になるので、**数値で許容を決める**
    (クリックは既に同型で運用できているが、ハイハットは目立つ)。
  - **Linux 側は現在「次のバッファ先頭で発音」**(`s_click_asap`)。**両ホストの差を明記**し、
    そろえるなら同じ方式にする。
  - **`app_tick`(100ms)のジッタとは無関係**であること(発音は L0 のタイマから直接来る)を確認する。

- **d. 音色(合成)とサンプルへの差し替え境界**
  - **Kick / Snare / Closed Hi-Hat / Crash** の 4 音を、**ノイズ生成器 + 減衰エンベロープ +
    ピッチ掃引**でどう作るか(既存の再帰振動子を土台にしてよい)。
    **ノイズは xorshift 等の整数乱数で十分**(`libm` をサンプルごとに呼ばない — 既存コードと同じ規律)。
  - **`HOSTAPI_WAVE_*` に波形を足すか**(現在 `SINE` のみ。追加は非破壊)。
  - **「後でサンプルに差し替える」境界**: ボイスの実体(`render(voice, out, n)`)だけを
    差し替えれば済む形になっていること。**サンプルの読み込み・PSRAM への配置は本フェーズでは作らない**
    が、**置き場の当たり**(PSRAM、`hostapi_fs_read` の再利用)は一段落書いておく。

- **e. Host API の語彙(承認の本体)**
  - **アプリから SYNTH をどう叩くか。** **推奨: 新規関数を足さず、
    `seq_write(port=HOSTAPI_PORT_SYNTH, status=0x99, data1=note, data2=velocity)` で
    MIDI の語彙をそのまま使う**(`architecture.md` §7 のポート抽象の趣旨。
    **X-macro への追加がゼロ**なら既存アプリへの影響もゼロ)。
    - このとき **note 番号の割り当て**をどうするか(**GM ドラム準拠**が素直:
      Kick 36 / Snare 38 / Closed HH 42 / Crash 49)。
    - **Note Off / ゲートタイム**をどう扱うか(**打楽器はワンショットなので Note Off を無視する**のが
      素直。そうするなら **21a のイベント数が半分になる**ので、P9 の見積もりに効く。
      無視する契約を `hostapi_defs.h` に明記すること)。
    - **音色の定義**を足すか(`tone_define` 相当)。**推奨: Phase 21 では固定 4 音**にし、
      定義関数は作らない(語彙を増やさないことが層の切り方の検証 — `hostapi.md` §0)。
  - **即時発音の口**が要るか(`tone_play` 相当)。**推奨: 要らない**
    (予約は `seq_write`、UI のプレビューが要るなら 21a で判断する)。
  - `shared/hostapi_defs.h` のコメントに書く**契約**(同時発音数、溢れたときの規則 =
    **一番古いボイスを奪う / 捨てる**、`transport_stop` / アプリ破棄での消音)。

- **f. 音量**: マスター音量(P10)との関係、クリックとの相対レベル、**クリッピングの回避**
  (4 音同時でサチュレートしないヘッドルーム)。

- **g. 検証の入口(重要)**: **4 音同時に鳴ることを何で確かめるか。**
  - 選択肢: (i) **検証用アプリ `wasm-apps/synth_probe/` を 1 本足す**(P11。回帰 6 本には入れない)、
    (ii) `seq_smoke` に項目を足す(**回帰の基準 `.wasm` が変わる**ので慎重)、
    (iii) `metronome` のクリックを一時的に差し替える(**撤去が要る**)。
  - **推奨は (i)**。**ただし 21a でドラム画面ができたら要否を再評価する**(残すか消すか)。
  - **音は耳で判定する**ことになるので、**カメラ録画(`docs/workflow.md` §3.3)で残す**。
    加えて **Linux ホストの出力を WAV に落として波形で確かめられるか**を検討する
    (できるなら機械判定の材料になる)。

- **h. メモリ見込みと検証計画**
  - ミキサの静的バッファ(internal / PSRAM の別)、タスクスタック、`.wasm` への影響(**ゼロのはず**)。
  - 検証の順序(Linux → 実機)、**V4 の再測**(ゲート 5)、**回帰 6 本**。

#### ステップ 1: WAMR プールの PSRAM 化(先に余裕を作る)

- 19b と同じ順序(**先に余裕を作ってから機能を積む**)。
- 実装は `wasm_runtime.cpp` の局所変更。**Linux ホストは 192KB の静的のままでよい**(理由を results に書く)。
- **実測して記録**: プール消費 / `free_int` / `largest_int` / `free_psram` / `app_tick` の avg・max /
  jitter 統計。**sequencer が起動すること**(いちばん大きい `.wasm`)を確認する。
- **回帰 6 本(実機)をここで 1 回通す**(音源に手を入れる前の基準を取るため)。
- **しきい値の更新**(`scripts/device-regress.conf`)はゲート 6 の対象なので、値を提示して承認を得る。

#### ステップ 2: 内蔵音源ポート — Linux 先行

- `shared/seq_core.h` / `.c`: `seqcore_hooks_t` にフックを追加、`port_dispatch()` で `SYNTH` を処理。
- `hosts/linux/hostapi_sdl.c`: `Voice` を N 本に、ノイズ + エンベロープを追加、
  `audio_callback` でミキシング。`hostapi_seq.c` にフックを配線。
- `shared/hostapi_defs.h`: 契約コメント(ステップ 0-e の決定どおり)。
- 検証アプリ(ステップ 0-g)で **4 音同時**を鳴らす。

#### ステップ 3: 内蔵音源ポート — 実機

- `src/components/audio/`: ブロックミキサへの作り替え(ステップ 0-b の決定どおり)。
- `src/components/seq/seq.cpp`: フックの配線。
- **CLICK ポートの挙動が変わらないこと**を metronome で確認する(`.wasm` は再ビルドしない)。

#### ステップ 4: 検証

- **Linux**: 検証アプリで 4 音同時、クリックが従来どおり鳴る、mp3player が壊れていない。
  **画面キャプチャ**(`docs/workflow.md` §3.6)と、可能なら**出力 WAV の波形**。
- **実機**:
  - **4 音同時が鳴る**(カメラ録画。`docs/workflow.md` §3.3)。
  - **クリック(メトロノーム)の音とタイミングが従来どおり**。**7B-fix の二重クリックが再発しない**
    (1 拍ごとに数分回して耳と録音で確認する)。
  - **mp3player が従来どおり再生でき、停止後に内蔵音源が鳴る**(レート切替をまたぐ)。
  - **V4 の再測(ゲート 5)**: `./scripts/midi-clock-probe.sh --task phase21 --label V4 --duration 330 --bpm 120`。
    **Phase 13 / 17 の値と並べて表に出す。**
  - **メモリの 4 値**(`free_int` / `largest_int` / `free_psram` / `largest_psram`)とプール消費。
- **回帰 6 本**: 実機(`--task phase21-regress`)と Linux(`docs/workflow.md` §3.7)。**同時に走らせない。**
  **Linux のプール消費(highmark)を既存 5 本について前フェーズと突き合わせる**
  (Host API の追加が既存アプリに影響していないことの裏づけ。Phase 20 と同じ見方)。

#### ステップ 5: 文書化

- `docs/results/phase21.md`(雛形は下記)。
- `docs/architecture.md`: **§9 の表(WAMR プールの配置)と「WAMR プールの大きさ」節**、
  **§7 のポート抽象(SYNTH を「将来」から「実装済み」へ)**、**§10 の移行ステップ 6**、
  **§11 に設計判断の記録**(ミキサ方式、発音タイミングの丸め、Note Off の扱い)。
- `docs/hostapi.md`: SYNTH ポートの節(語彙・契約・同時発音数・溢れの規則)、§7 の表。
- `shared/hostapi_defs.h`: 契約コメント(実装と同時に書く)。
- `docs/apps/sequencer/spec.md`: §1.2 / §3.3 の拡張の受け皿、§6 に H10(内蔵音源)を足すか。
- `docs/design/ui-conventions.md`: **本フェーズでは変更不要の見込み**(画面を作らないため)。
  **ただし 21a で「横スワイプ」の解禁が要る**(現在 §2 で「v1 では使わない。将来のページ送り用に予約」)
  ので、**その旨を残課題に書く**。
- `docs/workflow.md` / `docs/lessons.md`: **フェーズの締めで要否を必ず確認する**(CLAUDE.md)。
  音の確認手順(カメラ / WAV)が定型化したら workflow へ。
- `docs/status.md`、`wasm-apps/README.md`(検証アプリを足したなら)。
- `docs/roadmap.md` は**次フェーズ(21a)の指示書を書くときに**更新する。

### 含まない

- **ドラムマシンの画面・パターンのデータモデル・Session との紐付け**(Phase 21a 以降)。
- **パターンの永続化**(MBBK v2)。
- **サンプル(WAV)の読み込みと再生**(「後でサンプル」の後半)。
- **音色の定義 API**(`tone_define` 相当の SYNTH 版)。Phase 21 は固定 4 音。
- **USB_MIDI ポート**の実装(予約のまま)。
- **L0 キュー深さの変更**(256 のまま。`architecture.md` §9 の 8KB 案は未検証のまま置く)。
- **`PEND_MAX` の拡大**、**描画スロットの上限引き上げ**(どちらも 21a)。
- **既存 5 本の `.wasm` の再ビルド**、**U-17(既存アプリへの対話規約の横展開)**。
- カウントイン(Q6)、U-19 / U-20(名前と PC の編集、Song 一覧の編集)。

## 進め方

1. セッション開始時に `CLAUDE.md`・`docs/workflow.md`(通読)・`docs/lessons.md`・本書の参照先を読み、
   `docs/workflow.md` §3.0 の環境確認を行う。
2. **ステップ 0 報告 → 承認 → ステップ 1〜5。** 各ステップの結果は `docs/results/phase21.md` に記録する。
3. **ステップ 1(メモリ)とステップ 2〜3(音源)は独立しているので、
   ステップ 1 を先に完結させて回帰を 1 回通す**(問題が起きたときの切り分けのため)。
4. 確認は Linux → 実機の順。シェル操作はすべて `scripts/hpane.sh` 経由。
5. 仕様・本書に無い判断をした場合は、results の「仕様からの逸脱」に必ず記録する。
6. **オーディオ出力は Clock Authority のレートマスターである**(P4)。
   触る前に `docs/architecture.md` §3 を読み直し、**変更のたびに V4 を測れる状態を保つこと。**

## 完了条件

- [ ] ステップ 0 の設計メモ(a〜h)が承認されている
- [ ] **WAMR プールが PSRAM 上にあり**、sequencer(最大の `.wasm`)が起動する
- [ ] **internal RAM が取り戻せており**(`free_int` / `largest_int` の実測)、
      **回帰しきい値が新しい基準値に合わせて更新**されている
- [ ] **`app_tick` の avg / max が悪化していない**(19b の 1,207µs と並べて記録)
- [ ] **`HOSTAPI_PORT_SYNTH` が両ホストで実装され、Kick / Snare / Hi-Hat / Crash の
      4 音が同時に鳴る**(実機はカメラ録画、Linux はキャプチャまたは WAV)
- [ ] **アプリからの叩き方が `seq_write` だけで済んでいる**(Host API の関数追加が無い、
      または追加が承認されている)
- [ ] **既存 5 本の `.wasm` が再ビルドされておらず**、挙動も変わっていない
- [ ] **クリック(メトロノーム)の音とタイミングが従来どおり**で、**二重クリックが再発しない**
- [ ] **mp3player が従来どおり動く**(レート切替をまたいで内蔵音源も鳴る)
- [ ] **V4 の再測が Phase 13 / 17 の目標を満たす**(欠落 0 / 100.00% / 単峰 / 20832.8µs)
- [ ] **回帰 6 本が実機・Linux の両方で PASS**
- [ ] `architecture.md`(§7 / §9 / §10 / §11)・`hostapi.md`・`hostapi_defs.h`・spec・
      `lessons.md`・`status.md` が更新されている
- [ ] **21a への申し送り**(横スワイプの解禁、`PEND_MAX`、イベント数の見積もり、
      検証アプリを残すか)が results に書かれている

## `docs/results/phase21.md` の雛形

```markdown
# Phase 21 実施記録 — ドラムマシンの土台(WAMR プールの PSRAM 化 + 内蔵音源ポート)

## ステップ 0: 設計メモ(a〜h)
## ステップ 1: WAMR プールの PSRAM 化(実測とプール / heap の 4 値)
## ステップ 2: 内蔵音源ポート(Linux)
## ステップ 3: 内蔵音源ポート(実機ミキサ)
## Host API と seqcore_hooks_t の変更
## 検証(4 音同時 / クリック / mp3 / V4 再測)
## 回帰(6 本)
## 仕様からの逸脱・architecture.md / hostapi.md への反映
## Phase 21a への申し送り
## 残課題
```

## 追記(スコープ変更)

- (日付: 内容)
