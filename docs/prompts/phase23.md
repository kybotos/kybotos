# Phase 23: 内蔵音源のボイスを共通の C に寄せる

- 契約日: 2026-10-04
- 参照: `docs/results/phase21.md`(内蔵音源ポートとミキサ)、`docs/results/phase21c.md`(メトロノームの音色 Wood)、
  `src/components/audio/audio.cpp`(実機のミキサとボイス)、`hosts/linux/hostapi_sdl.c`(Linux のミキサとボイス)、
  `shared/hostapi_defs.h`(`HOSTAPI_SYNTH_*` の契約)、`hosts/linux/tests/`(Linux の C の単体テスト)
- 結果報告先: `docs/results/phase23.md`

## 目的

内蔵音源(SYNTH ポート)と CLICK / tone のボイスは、**実機の `audio.cpp` と Linux の `hostapi_sdl.c` に同じものが 2 本ある**。
コメントの「式と定数は Linux の hostapi_sdl.c と同じにすること」/「実機の audio.cpp と同じにすること」で、**手で同期している**。

- 音色を足す、変える(U-27)たびに、2 か所を同じに直す必要がある。食い違っても、ビルドも回帰も気づかない。
- ボイスはホストに依存しない計算(再帰振動子のサイン、xorshift32 のノイズ、指数減衰)だけでできている。
  seq_core(`shared/seq_core.c`)や master_ui(`shared/master_ui.c`)と同じく、`shared/` に置けるはず。
- 今後ホストが増えたときに、3 本目の写しを作らずに済む。

このフェーズでは、ボイスを **`shared/synth_voice.c` / `synth_voice.h`** に切り出し、両ホストがそれを使うようにする。**音は 1 サンプルも変えない。**

## 前提(2026-10-04 に確かめたこと。着手時にソースで再確認する)

- **P1: 2 本の中身は同じ**。`VoiceKind`(TONE / KICK / SNARE / HAT / CRASH / WOOD)、`Voice` の構造体、`voice_set_sine` / `voice_alloc`(空き → 同じ note の最古 → 全体の最古)/
  `voice_start_tone` / `voice_start_drum` / `voice_noise` / `voice_render` の式と定数は、行単位で一致している。
  - 違うのは**ゲインの渡し方**だけ。実機はマスター音量を引数で渡し、ポートのゲインはファイルの静的変数。Linux はどちらもファイルの静的変数。
  - サンプルレートの名前(`kMixRate` / `CLICK_RATE`)も違うが、値はどちらも 44100。
- **P2: 使う libm は `expf` / `powf` / `cosf` / `sinf`**(発音の開始時と、Kick のブロックごとの係数だけ。サンプルごとには呼ばない)。
- **P3: ミキサの部分(ブロックの長さ、発音要求の受け渡し、MP3 との排他、起動音、DMA のリング)はホストに残す**。切り出すのはボイスの生成と描き出しだけ。
- **P4: Linux は `KYBOTOS_WAV_OUT` でミキサの出力を WAV に書ける**(`hostapi_sdl.c`)。これで切り出しの前後をサンプル単位で比べられる。
- **P5: 実機の `audio` コンポーネントは、すでに `shared/boot_sound.c` を `SRCS` に入れている**(`src/components/audio/CMakeLists.txt`)。`shared/` の C を足す道はある。

## ゲート

1. **ステップ 0 の設計メモ(API の形、ゲインの渡し方、実機も切り替えるか)をユーザーが承認するまで実装しない。**
2. **Host API / ABI と `.wasm` は変えない。** `HOSTAPI_SYNTH_*` の契約(note の割り当て、同時発音 8、奪取の規則)も変えない。
3. **音を変えない**: Linux の WAV で、切り出しの前後が**サンプル単位で一致**すること。
4. 回帰 3 本(Linux。実機を切り替える場合は実機も)PASS。実機を切り替える場合は、停止時の `free_int` / `largest_int` がしきい値を割らないこと。
5. 公開の repo なので、非公開の repo の中身は書かない。

## スコープ

### 含む

- ステップ 0: 設計メモ(`docs/results/phase23.md`)。決めること:
  - **a. API の形**: たとえば `synthv_reset()` / `synthv_start_tone(freq_hz, dur_ms, level, gain)` / `synthv_start_drum(note, velocity, gain)` / `synthv_render(int32_t* acc, int n)`(全ボイスを acc に足し、鳴っているボイスがあるかを返す)。
    ボイスの配列を静的に持つか、呼び出し側が状態の構造体を渡すか。
  - **b. ゲイン**: マスター × ポートのゲインを、呼び出し側が掛けて 1 つの値で渡すか。**発音時に焼き込む**いまの意味(発音中の音にはマスターの変更が効かない)は変えない。
  - **c. 実機も切り替えるか**: 推奨は**切り替える**(手作業の同期をなくすのが目的なので)。
    `audio.cpp` は C++ なので、ヘッダに `extern "C"` を付ける。ミキサのタスクのスタックや、静的確保の量が変わらないことを確かめる。
  - **d. 単体テスト**: `hosts/linux/tests/synth_voice_test.c`。決めた入力(note、velocity、ブロックの長さ)に対する出力のチェックサム、奪取の規則、未知の note で何も鳴らないこと。
- ステップ 1: 切り出す前に、Linux の WAV の基準を録る。内蔵音源の全 note と tone を鳴らす経路で録る(`synth_probe` か metronome。どちらにするかはステップ 0 で決める)。
- ステップ 2: `shared/synth_voice.{h,c}` を作り、Linux を切り替える。WAV が基準と一致することと、単体テストを確かめる。
- ステップ 3(c で切り替えると決めた場合): 実機を切り替え、実機の回帰とメモリの値を確かめる。メトロノームの音とドラムを耳で確かめる(ユーザー)。
- ステップ 4: 文書(`docs/results/phase23.md`、roadmap、architecture の該当箇所、コメントの「同じにすること」の削除)。

### 含まない

- 音色の変更・追加(U-27)。
- U-22 の対策(発音の前倒し、ブロック内のサンプルオフセット)。ただし、`synthv_render` の形が、将来「ブロックの途中から鳴らし始める」ことを妨げないようにはしておく(ステップ 0 で一言触れる)。
- ミキサの作り替え、MP3 との排他(U-21)。

## 完了条件

1. ボイスの実装が `shared/synth_voice.c` の 1 本だけになっている(実機を切り替えない場合は、その理由と、残る二重実装を roadmap の課題に書く)。
2. Linux の WAV が切り出しの前後でサンプル単位で一致する。単体テストが ctest で通る。
3. 回帰 3 本 PASS(Linux。実機を切り替えた場合は実機も)。Host API / ABI・`.wasm` は不変。
4. 記録 `docs/results/phase23.md`。

## 追記の置き場所

スコープを変えるときは本文を書き換えず、この下に「追記 (日付)」節を足す。

## 追記 (2026-10-04): ゲート 3 と完了条件 2 の判定方法

ステップ 0 で、**実際に鳴らした Linux の WAV は、同じコードでも 2 回の録音で一致しない**ことが分かった
(発音要求のスレッドとオーディオのコールバックの位相が毎回変わる。`docs/results/phase23.md` 0-0)。
ユーザーの承認を得て、ゲート 3「Linux の WAV で、切り出しの前後がサンプル単位で一致すること」と
完了条件 2「Linux の WAV が切り出しの前後でサンプル単位で一致する」を、次の 2 つで判定する。

1. **オフラインの一致**: `hosts/linux/tests/synth_voice_test.c` で、`scripts/synth-voice-compare.sh` と同じ 60 ケース
   (5 つのシナリオ × 4 つのゲイン × 3 つのブロック長)を `synthv_*` で鳴らし、**切り出す前のコードのハッシュと一致する**こと。
2. **実機に近い経路での確認**: Linux の `synth_probe` の WAV を切り出しの前後で録り、**ピーク・帯域エネルギー・オンセットの数**が揃っていること
   (workflow §3.8。サンプル単位では比べない)。

あわせて、ステップ 0 で次も決めた: API は `synthv_reset` / `synthv_start_tone` / `synthv_start_drum` / `synthv_render`、
ゲインはマスターとポートの整数 2 つで渡していまの式の順を保つ、**実機も切り替える**(ステップ 3 を行う)。
