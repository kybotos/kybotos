# Phase 23: 内蔵音源のボイスを共通の C に寄せる — 実施記録

指示書: `docs/prompts/phase23.md`

## ステップ 0: 設計メモ(2026-10-04)

> 状態: **承認済み**(2026-10-04、ユーザー。a〜e をすべてこのとおりに承認)。

### 0-0. 調べたこと

**P1(2 本のボイスは同じ)を数値で確かめた。** `scripts/synth-voice-compare.sh` を新しく作った。

- 何をするか: git の版から、Linux(`hostapi_sdl.c`)と実機(`audio.cpp`)のボイスのコードを目印の行で抜き出し、x86 で同じ入力を与えて鳴らす。出力の int32 の列のハッシュ(FNV-1a)を比べる。
- 入力: 5 つのシナリオ × 4 つのゲイン(マスター / Synth / Click)× 3 つのブロック長(240 / 100 / 1)で、各 2 秒。
  - シナリオ: 4 音を同時に鳴らす、Wood の Click / Bell と未知の note、tone と Kick、12 音を同時に鳴らす(ボイスの奪取)、強さを変えた 64 打。
- 結果(`2bf5156`、`captures/phase23/voice-compare-head.txt`): **60 ケースすべてで一致**。
  - うち 15 ケースはマスター音量 0(無音)なので、意味のある比較は 45 ケース。
  - 実機のコードは x86 の g++ でビルドしている。**ESP32-S3 の FPU と libm での出力とは比べていない**。言えるのは「同じコードである」ことまで。

**ボイスの外側の違い**(切り出しても残る、ホストの部分):

| 項目 | 実機(`audio.cpp`) | Linux(`hostapi_sdl.c`) |
|---|---|---|
| マスター音量 | 発音の開始時に引数で渡す(`volume_.load()`) | ファイルの静的変数 `s_master_vol` |
| ポートのゲイン | 静的変数 `s_gain_synth` / `s_gain_click`(`set_gain` が書く) | 同じ名前の静的変数(master_ui のコールバックが書く) |
| 発音要求 | FreeRTOS のキュー(深さ 16)→ ブロックの頭で全部開始 | ロック下の配列(32 件)→ ブロックの頭で全部開始 |
| ブロック | 常に 240 フレーム | SDL のコールバックを 240 ずつに切る(端数あり) |
| リセット | `synth_reset` / `flush_silence` は `kind = VK_IDLE` だけ(`s_voice_seq` は残す) | `memset` で全部 0、`s_voice_seq = 0` |
| 「鳴っているか」 | `any`(鳴っているボイスがあれば I2S にミックスを書く。無ければ 0 を書く) | 使っていない(常にミックスを書く) |

- リセットの違いは、音には影響しない。`s_voice_seq` は「鳴っているボイスの中で最も古いもの」を選ぶ比較にしか使わず、リセットの後は全ボイスが空いているので、どちらでも同じボイスが選ばれる。

**実際に鳴らした WAV は、同じコードでも 2 回の録音で一致しない。**

- いまのコードのまま、`synth_probe` を Linux で 2 回、10 秒ずつ録った(`captures/phase23/probe-head-{1,2}.wav`)。
- ファイルの長さが違う(1,779,756 B / 1,776,684 B)。最初の音の位置を揃えて比べても、**一致は 84.6%** で、**2.25 秒のところから**ずれた。
- 理由: 発音要求は seq のディスパッチのスレッドから積まれ、SDL のコールバックが「次に書くブロックの頭」で取り出す(案 A の丸め)。ディスパッチとコールバックの位相は OS のスケジューリングで毎回変わる。
- したがって、**指示書のゲート 3 / 完了条件 2「Linux の WAV が切り出しの前後でサンプル単位で一致する」は、判定に使えない**(→ 下の e で代わりを提案する)。

### a. API の形

`shared/synth_voice.h` / `synth_voice.c`。状態(8 ボイス、発音の通し番号)は **.c の静的変数**に持つ。seq_core / master_ui と同じ作り。

```c
#define SYNTHV_RATE 44100   /* 両ホストのミキサのレート */

/* 全ボイスを止め、発音の通し番号を 0 に戻す(アプリの破棄、transport_stop、MP3 との受け渡し) */
void synthv_reset(void);

/* CLICK ポート / tone_play の減衰サイン。level は 0..100 */
void synthv_start_tone(uint16_t freq_hz, uint16_t dur_ms, uint8_t level, int master_vol, int gain_click);

/* SYNTH ポート。未知の note は何もしない(hostapi_defs.h の契約) */
void synthv_start_drum(uint8_t note, uint8_t velocity, int master_vol, int gain_synth);

/* 鳴っている全ボイスの n フレームを acc に足す。呼んだ時点で鳴っているボイスがあれば true */
bool synthv_render(int32_t* acc, int n);
```

- **ゲインは整数のまま 2 つ渡し、いまと同じ順で float の式を計算する**(b)。
- `extern "C"` を付ける(実機の `audio.cpp` は C++)。
- libm は `expf` / `powf` / `cosf` / `sinf` を使う(いまと同じ)。
- **U-22(案 B: ブロックの途中から鳴らす)を妨げない**: `synthv_render` は任意の n を受けるので、呼び出し側がブロックを「発音の位置の手前」と「残り」に分けて 2 回呼べば、ブロック内のサンプル位置から鳴らせる。API は変えずに済む。

### b. ゲインの渡し方

- いまの式は `g = velocity / 127 × master / 100 × gain_synth / 100`(float を左から順に掛ける)。tone も `12000 × level / 100 × master / 100 × gain_click / 100`。
- 呼び出し側でマスター × ゲインを 1 つの float にまとめて渡すと、**掛ける順が変わって float の丸めが変わり、出力が 1 LSB 単位で変わりうる**。
  そこで**マスターとポートのゲインを整数のまま渡し、関数の中でいまと同じ式・同じ順で計算する**。
- **発音の開始時に焼き込む**意味(鳴っている音にはマスターの変更が効かない)は変えない。

### c. 実機も切り替えるか — 推奨: 切り替える

- 0-0 のとおり、2 本は同じコード。切り替えても、実機で走る**ソースの式**は変わらない。
- 切り替えないと、Phase 23 の目的(手作業の同期をなくす)が半分しか達成できない。
- **変わりうる点**:
  - コンパイル単位が C++(`audio.cpp`)から C(`synth_voice.c`)に変わる。
  - ESP32-S3 は FPU に積和(`madd.s`)があるので、コンパイラが掛け算と足し算を 1 命令にまとめるかどうか(FP contraction)が変わると、**最下位ビット程度の違い**が出うる。耳では分からない大きさで、契約(note の割り当て、同時発音数、奪取の規則)には関係しない。
  - 静的変数(`s_voices` 8 × 約 72 B、`s_voice_seq`)は、無名の名前空間から `synth_voice.c` の static に移るだけで、量は同じ。
- **確かめること**(ステップ 3):
  - 実機のビルド。
  - 回帰 3 本。停止時の `free_int` / `largest_int` がしきい値を割らず、値も Phase 22e から動かないこと(動いたら理由を調べる)。
  - **耳**(ユーザー): metronome(Wood)と `synth_probe`(4 音、ハイハットの連打、クリックとの重なり、12 音の奪取)。
- `audio/CMakeLists.txt` の `SRCS` に `../../../shared/synth_voice.c` を足す(`boot_sound.c` と同じ形。0-0 の P5)。

### d. 単体テスト

`hosts/linux/tests/synth_voice_test.c`(ctest に登録。`master_ui_test` と同じ形)。

1. **切り出し前との一致**: `synth-voice-compare.sh` と同じ 60 ケースを `synthv_*` で鳴らし、**切り出す前のコードのハッシュ**(0-0 の `voice-compare-head.txt` の値)と一致すること。期待値はテストの中に表で持つ。
2. **奪取の規則**: 8 本が埋まっているとき、9 音目は「同じ note の最も古いもの」を奪い、同じ note が無ければ「全体で最も古いもの」を奪う(`hostapi_defs.h` の契約)。`synthv_render` の戻り値と、奪われたボイスが鳴り直すことで見る。
3. **未知の note**(例 60)と velocity 0 は鳴らない(`synthv_render` が false、acc が 0 のまま)。
4. **`synthv_reset`** の後は鳴らない。

- 1 は、x86 の同じコンパイラで、いまの Linux のコードと同じ結果になることを確かめる。実機の FPU での一致は保証しない(c)。
- `synth-voice-compare.sh` には、切り出した後の版でも走らせられるように、`shared/synth_voice.c` があればそちらも同じ入力で鳴らして比べるモードを足す(ステップ 2)。

### e. ゲート 3 / 完了条件 2 の言い換え(提案)

0-0 のとおり、実際に鳴らした WAV は同じコードでも一致しない。そこで次に置き換える(承認後に指示書へ「追記」として足す。本文は書き換えない)。

| 元の条件 | 置き換え |
|---|---|
| Linux の WAV が切り出しの前後でサンプル単位で一致する | **(1) オフラインの一致**: 単体テスト d-1 で、切り出し前のコードと**60 ケースのハッシュが一致**する(決まった入力・決まったブロック長なので、サンプル単位の一致を確かめられる)。**(2) 実機に近い経路での確認**: Linux の `synth_probe` の WAV を切り出しの前後で録り、**ピーク・帯域エネルギー・オンセットの数**が揃っていること(workflow §3.8。サンプル単位では比べない) |

### f. 進め方(承認後)

| ステップ | 内容 |
|---|---|
| 1 | 基準: 0-0 で済み(`voice-compare-head.txt` = オフラインの期待値、`probe-head-{1,2}.wav` = 実機に近い経路の基準)。指示書に e の追記を足す |
| 2 | `shared/synth_voice.{h,c}` を作る(式と定数は `hostapi_sdl.c` から移す)。Linux を切り替える(`voice_*` と `Voice` を削除し、`synthv_*` を呼ぶ)。`synth_voice_test` と ctest、`synth-voice-compare.sh` の追加モード、Linux の回帰 3 本、`synth_probe` の WAV の統計 |
| 3 | 実機を切り替える(c)。ビルド、回帰 3 本、メモリの値、耳での確認(ユーザー) |
| 4 | 文書: roadmap、architecture(ボイスの置き場所に触れていれば)、コメントの「同じにすること」の削除、workflow / lessons への反映の要否 |

### 承認していただきたいこと

1. API の形(a)と、**ゲインを整数 2 つで渡していまの式の順を保つ**こと(b)。
2. **実機も切り替える**(c)。
3. **ゲート 3 / 完了条件 2 を e のとおり言い換える**(指示書に追記を足す)。

## ステップ 1: 基準(2026-10-04)

ステップ 0 で済ませた(指示書の追記のとおり)。

- オフラインの期待値: `captures/phase23/voice-compare-head.txt`(`2bf5156`、60 ケース。Linux と実機のコードで同じ)。
- 実機に近い経路の基準: `synth_probe` を Linux で 2 回、10 秒ずつ録った WAV(`captures/phase23/probe-head-{1,2}.wav`)。

## ステップ 2: `shared/synth_voice.{h,c}` と Linux の切り替え(2026-10-04)

**変更**

- `shared/synth_voice.h` / `synth_voice.c` を新しく作った。API はステップ 0 の a のとおり。
  式と定数は `hostapi_sdl.c` から**そのまま**移した。サンプルレートの名前だけ `CLICK_RATE` → `SYNTHV_RATE`(値は 44100 のまま)。
  `M_PI` が無い環境のために、同じ値の定義を足した。
- `hosts/linux/hostapi_sdl.c`: `VoiceKind` / `Voice` / `s_voices` / `s_voice_seq` と `voice_*` の 6 関数を削除した(−203 行)。
  ミキサからは `synthv_start_drum(note, vel, s_master_vol, s_gain_synth)` / `synthv_start_tone(..., s_master_vol, s_gain_click)` / `synthv_render` / `synthv_reset` を呼ぶ。
- `hosts/linux/CMakeLists.txt`: `kybotos_host` に `shared/synth_voice.c` を足した。単体テスト `synth_voice_test` を ctest に登録した。
- `hosts/linux/tests/synth_voice_test.c`(ステップ 0 の d):
  1. 60 ケースのハッシュが切り出す前と一致すること(期待値はテストの中の表)。
  2. 奪取の規則。ノイズの種は発音の通し番号から作るので、**未知の note で通し番号だけ進め**、奪取を起こさずに同じ番号のボイスを組み立てて、出力のハッシュを比べる。
     (a) 同じ note が無ければ、全体で最も古いものを奪う。(b) 同じ note があれば、その最も古いものを奪う。比較が意味を持つこと(番号がずれれば出力が変わる)も確かめる。
  3. 未知の note は鳴らない。リセットの後は鳴らない。tone は `dur_ms` で鳴り終わる。
- `scripts/synth-voice-compare.sh`: 版に `shared/synth_voice.c` があれば、それも同じ入力で鳴らす。目印が見つからない側(切り替えた後のホスト)は外す。
- `scripts/wav_summary.py`(新規): WAV の最初のオンセットから 8 秒について、ピーク、rms、オンセットの数、帯域エネルギー(goertzel 6 点)を 1 行に出す。

**確認**

| 項目 | 結果 |
|---|---|
| Linux のビルド | 警告は WAMR の既存のもの(`invokeNative_em64.s.o: missing .note.GNU-stack`)だけ |
| ctest | **3/3 PASS**(seq_core_test / master_ui_test / **synth_voice_test**) |
| テストの感度 | scratch で `synth_voice.c` の写しの Synth のゲインを `velocity/127 × (master/100 × gain/100)` に変える(**掛ける順だけ**)と、**7 件が FAIL**(64 打 × マスター 37 / Synth 55 / Click 80 などのケース)。ステップ 0 の b(ゲインを整数 2 つで渡し、順を保つ)が必要だったことの裏づけ |
| 比較スクリプト(古い版) | `synth-voice-compare.sh 2bf5156`: 60 ケース一致、ハッシュは `voice-compare-head.txt` と同じ(書き換えの後も同じ結果を出す) |
| Linux の回帰 3 本 | **PASS**(metronome highmark 33,576 / mp3player 27,208 / hostapi_check 34,728。いずれも Phase 22〜22e と同じ) |
| `synth_probe` の WAV(前後 2 回ずつ) | 下の表。**ピーク・rms・オンセットの数が 4 本とも同じ**。帯域エネルギーの違いは、切り出し前の 2 回どうしの違い(最大 7.7dB。ブロックの位相で goertzel の値が動く)の範囲に入っている |

```
wav                 first    sec   peak      rms onsets     60Hz    110Hz    190Hz   1200Hz   2000Hz   6000Hz
probe-head-1.wav     1280   8.00  16708    427.5     22    64.17    66.12    73.22    42.42    39.82    47.12
probe-head-2.wav     1024   8.00  16708    427.5     22    68.70    58.44    74.38    42.39    44.75    48.28
probe-new-1.wav       768   8.00  16708    427.5     22    65.54    67.00    75.01    42.03    40.56    45.20
probe-new-2.wav      1024   8.00  16708    427.5     22    68.70    58.42    74.38    42.41    45.56    47.74
```

(`python3 scripts/wav_summary.py captures/phase23/probe-*.wav`。`first` は最初の音までのサンプル数で、録るたびに変わる。
最初の音の位置が同じ 1024 になった head-2 と new-2 は、帯域エネルギーもほぼ同じ。)

**途中のつまずき**: 比較スクリプトを書き換える Python をシェルのヒアドキュメントで渡したら、Python の中に `EOF` だけの行があってそこで切れ、
残りの行をシェルが実行した(`/main.c: Permission denied` で止まった)。作業ツリーに変化が無いことを `git status` で確かめ、Python をファイルにして実行し直した。

## ステップ 3: 実機の切り替え(2026-10-04)

**変更**

- `src/components/audio/audio.cpp`: `VoiceKind` / `Voice` / `s_voices` / `s_voice_seq` と `voice_*` の 6 関数を削除した。
  ミキサタスクからは `synthv_start_drum(note, vel, vol, s_gain_synth)` / `synthv_start_tone(freq, dur, level, vol, s_gain_click)` / `synthv_render` を呼ぶ。
  `kMixRate` は `SYNTHV_RATE` から取る。
- `src/components/audio/CMakeLists.txt`: `SRCS` に `shared/synth_voice.c` を足した(`boot_sound.c` と同じ形)。
- **リセットはミキサタスクに頼む形にした**(ステップ 0 で扱っていなかった点)。
  - `Synth_Reset`(= `Mp3Player::synth_reset`)は、アプリの起動 / 破棄のときに **WASM のタスク**から呼ばれる(`hostapi.cpp` の `hostapi_audio_reset`)。
  - これまでは別のタスクから `kind = VK_IDLE` を書くだけだったが、`synthv_reset` は構造体ごと 0 にする。そこで、起動音と同じく **atomic のフラグ `s_synth_reset_req` を立てるだけ**にした。
  - ミキサタスクが**次のブロックの頭で、発音要求を取り出す前に** `synthv_reset` する(最大 1 ブロック = 5.4ms 後。出力の DMA リングは約 33ms なので、聞こえ方は変わらない)。
    発音要求のキューは、これまでどおりその場で空にする。
  - MP3 との受け渡しの `flush_silence` はミキサタスクの中なので、そのまま `synthv_reset` を呼び、溜まっていた要求のフラグも下ろす。
  - Linux は、`host_sdl_audio_reset` がオーディオのロックの下で `synthv_reset` を呼ぶので、このままでよい。
- **`Voice::kind` を 1 B で持つようにした**(`shared/synth_voice.c`)。
  - 最初の実機の回帰で、開始時の `free_int` が Phase 22e の 150,240 から **150,200(−40 B)** に下がった。
  - 理由は、C の enum が 4 B であること(実機の C++ は `enum VoiceKind : uint8_t` の 1 B だった)。8 ボイスで +32 B になる。
  - `uint8_t kind` にして作り直したら **150,232(−8 B)** になった。残る 8 B は `s_synth_reset_req` と境界合わせ。
  - 音の計算には関わらないので、ctest(60 ケースの一致を含む)はそのまま PASS。

**確認**

| 項目 | 結果 |
|---|---|
| 実機のビルド(`KYBOTOS_DEV_APPS=ON`) | 警告 0。`kybotos.bin` 0x12f150。`src/dependencies.lock` は変わっていない |
| 実機の回帰 3 本(`captures/phase23-device2/report.md`) | **PASS**。開始時の free_int **150,232**(22e 比 −8 B)、largest_int 98,304、metronome の反復 3 回 +0、mp3player −472 / hostapi_check −176(`EXPECT_DELTA` どおり)、WARN / ERROR 0 件 |
| 実機の WAMR プール | metronome の highmark **25,744**(Phase 22e と同じ) |
| Linux(`kind` を変えた後) | ctest 3/3 PASS、回帰 3 本 PASS(highmark 33,576 / 27,208 / 34,728、ステップ 2 と同じ) |
| 比較スクリプト(`a97a9b4`) | `synth-voice-compare.sh`: 実機のコード(切り替え前)と共通の C で 60 ケース一致 |
| **耳**(ユーザー、`captures/phase23-ear/`。カメラの音 −18.5 dB) | **metronome**(120bpm 4/4、小節頭の Bell と Click): 「**同じに聞こえる**」。**synth_probe**(4 音同時、16 分のハイハット、クリックとの重なり、12 音の奪取、小節 8〜12 の MP3 の後にドラムが戻る): 「**同じに聞こえる**」 |
| 実機の普段使いへの復帰 | `KYBOTOS_DEV_APPS=OFF` でビルド(0x12b1c0)・書き込み。SD の hostapi_check / midi_loopback / synth_probe をシリアルの `rm` で消した |

**観察(Phase 23 の変更とは関係しない)**: 耳での確認で、`synth_probe` を止めたら free_int が 256 B 減った(`captures/phase23-ear/monitor.log`)。
ボイスはヒープを使わないので、MP3 の経路を疑って、リセット直後に `synth_probe` を 3 回続けて回した(MP3 が止まった後の 28 秒で止める)。
結果は **−36 / +0 / +0**(`captures/phase23-probe-heap/monitor.log`)で、U-23 の「起動後に初めて MP3 を鳴らしたときの一度きりの確保」と同じ。
−256 B のときは **MP3 が鳴っている最中(小節 12 の直前)にアプリを止めた**ので、別の一度きりの経路を通ったとみている(U-23 と同じ種類。切り分けはしていない)。

**非公開の app-sequencer の回帰(sequencer を足した 4 本)**: ステップ 3 のコミットの後に回した(本来はコミットの前に回す順序だった)。

| 項目 | 結果 |
|---|---|
| Linux(`app-sequencer/scripts/regress-linux.sh`) | **4 本 PASS**。sequencer の highmark 221,440(repo の分割のときと同じ)、他の 3 本は上と同じ |
| 実機(sequencer 入りのファーム、`KYBOTOS_DEV_APPS=1`) | **4 本 PASS**(`captures/phase23-seq-device2/report.md`)。開始時の free_int 150,232、sequencer の反復 3 回 +0、WARN / ERROR 0 件 |

- 1 回目は「hostapi_check が SD に無い」で止まった。**`build-fw.sh flash` に `KYBOTOS_DEV_APPS=1` を付け忘れた**ため。`flash` も `idf.py` でビルドし直すので、
  付けないと OFF の構成で作り直して焼く(スクリプトの冒頭のコメントどおり、build と flash の両方に付ける)。付けて焼き直したら通った。
- 終わったあと、実機は公開の普段使い(`KYBOTOS_DEV_APPS=OFF`)のファームに戻し、検査用アプリを消した。

## ステップ 4: 文書(2026-10-04)

- `docs/architecture.md` §11-12 に、ボイスの置き場所(`shared/synth_voice.c`)と、ミキサに残るものを書いた。
- `docs/lessons.md` に「ボイスを共通の C に寄せる(Phase 23)」の節を足した(実際に鳴らした WAV は同じコードでも一致しない、float の掛ける順、C と C++ の enum の大きさ、別のタスクからのリセット)。
- `docs/workflow.md` は変えない。§3.8 の「数値で判定できる」の表に、`scripts/wav_summary.py` を手順として足すほどの汎用性はまだ無い(今回の比較だけで使った)と判断した。
- `docs/roadmap.md` / `docs/status.md` を更新した。
- コメントの「式と定数は〜と同じにすること」は、両ホストから消えた(ボイスのコードごと削除したため)。

## 完了条件の確認

| 完了条件 | 結果 |
|---|---|
| 1. ボイスの実装が `shared/synth_voice.c` の 1 本だけになっている | ✅(Linux はステップ 2、実機はステップ 3) |
| 2. 切り出しの前後で出力が一致する(追記のとおり言い換えた) | ✅ オフライン: 60 ケースのハッシュが一致(ctest)。実機に近い経路: `synth_probe` の WAV のピーク・rms・オンセットの数が一致 |
| 3. 回帰 3 本 PASS(Linux・実機)、Host API / ABI・`.wasm` は不変 | ✅ |
| 4. 記録 | ✅ この文書 |
