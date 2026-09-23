# Phase 21c 実施記録 — ミキサーの MUTE と、アプリ側の音量 UI の整理

- 指示書: `docs/prompts/phase21c.md`
- 前提の到達点: `docs/results/phase21b.md`(マスター設定とミキサー)、`docs/results/phase21a.md`(所感 D-15 / D-18)

---

## ステップ 0: 設計メモ(a〜h)

読んだ実体: `shared/master_ui.h` / `.c`、`shared/hostapi_defs.h`(SYNTH / CLICK の契約)、
`src/components/wasm_runtime/hostapi.cpp`(`mui_*` / `mui_set_level_cb` / `native_hostapi_audio_set_volume`)、
`src/components/audio/audio.cpp`(`voice_start_drum` / `voice_start_tone` / `set_gain` / `write_fn`)、
`hosts/linux/hostapi_sdl.c`(同名関数 / `draw_master_overlay` / `masterui_set_level_cb`)、
`wasm-apps/sequencer/src/lib.rs` / `metronome/src/lib.rs` / `mp3player/src/lib.rs`、
`docs/design/ui-conventions.md` §1 / §3.7 / §4、`docs/hostapi.md` §5 / §7、`scripts/device-regress.conf`。

環境確認(`docs/workflow.md` §3.0)は実施済み(`ensure` の冪等性・`run` の echo)。

### 0-0. 調査で分かったこと(前提 P1〜P9 の確認と、指示書に無かった事実)

| # | 事実 | 出所 |
|---|---|---|
| F1 | P1〜P6 は指示書の記述どおり(座標・既定値・`CLICK_ON` / `T_CLICK` / `STA_*_HIT_X`・metronome の `ACCENT_SLOT` と `FINE_LABELS`・mp3player の `BTN_LABELS`) | 各ソース |
| F2 | **ゲインの通り道はホストの `set_level` フック 1 本だけ。** 実機は `mui_set_level_cb` が `masterui_level()` を読み直して `Volume_adjustment` / `Set_Port_Gain` に渡し、Linux は `masterui_set_level_cb(item, v)` の `v` をそのまま `s_*` に入れる | `hostapi.cpp` / `hostapi_sdl.c` |
| F3 | **実機の MP3 はマスター・MP3 ゲインとも `write_fn` で毎ブロック掛けている**(`volume_ × gain_mp3_`)ので**即座に効く**。ドラムとクリックは**発音時に焼き込む**(次の発音から) | `audio.cpp` 570 行付近 |
| F4 | **`hostapi_audio_set_volume` は `master_ui` を通らない**(両ホストとも直接 `s_master_vol` / `Volume_adjustment` を書く)。オーバーレイの Master 表示とずれるうえ、**Master の MUTE を素通りして鳴らしてしまう**。**呼んでいるのは metronome と mp3player だけ**で、本フェーズで両方から消える | `hostapi.cpp` 685 / `hostapi_sdl.c` 624 |
| F5 | **Click の MUTE は CLICK ポートだけでなく `hostapi_tone_play` / `hostapi_play_click` にも効く**(両ホストとも同じ `voice_start_tone` を通り `s_gain_click` が掛かる)。**既定 MUTE にすると、次のアプリも起動直後は無音になる**: **touch_demo のボタンタップ音**、seq_smoke のクリック、synth_probe の (3)。**sequencer の click と metronome 以外への波及は指示書に書かれていなかった** | `voice_start_tone`、各アプリの grep |
| F6 | metronome / mp3player は**ソースと `.wasm` が同じコミット**(metronome `65d328b` / mp3player `cc547e1`)。ソースは `.wasm` と一致している | `git log` |
| F7 | `device-regress.conf` には**アプリごとの期待値が無い**(`EXPECT_DELTA` / `EXPECT_DELTA_PSRAM` は空、しきい値は全アプリ共通)。**metronome / mp3player 固有の値を書き換える箇所は現状無い** | `device-regress.conf` |
| F8 | Linux の highmark の基準(Phase 20 / 21 / 21a / 21b で同値): touch_demo 15,944 / mp3player 19,888 / metronome 23,864 / midi_loopback 29,120 / seq_smoke 28,968 / sequencer 153,720 | `phase21a.md` T7 |

### a. MUTE の意味と置き場

- **MUTE はチャネルの値(0..100)とは別のフラグ**にする。**アンミュートで元の値に戻る。**
  **実効音量 = マスター × チャネル × (MUTE ? 0 : 1)**(マスター自身の MUTE も同じ形)。
- **Master にも MUTE を付ける**(4 行とも同じ形にする。指示書の推奨どおり)。
- **既定**: Master / MP3 / Synth は MUTE OFF、**Click は MUTE ON**。
- **置き場は `shared/master_ui.c`**。`s_muted[MASTERUI_ITEMS]` と既定値の表を足し、
  **フックへは「実効値」(`muted ? 0 : level`)を渡す**。
  - **ホストのゲイン計算は 1 行も変えずに済む**(F2。ホストが受け取る値が 0 になるだけ)。
  - 実機の `mui_set_level_cb` は `masterui_level()` を読み直しているので、
    **`masterui_effective(item)`(実効値を返す)を足してそちらを読む**ように直す。
  - 追加する API(ホスト内部。Host API ではない): `masterui_is_muted(item)`、`masterui_effective(item)`。
- **鳴っている音への効き方: 次の発音から**(既存の音量と同じ契約。ボイスの途中で切ると
  クリックノイズになる)。**MP3 は即座に効く**(F3。`write_fn` で毎回掛けているため)。
  Synth の最長は Crash の 800ms なので、**ミュートしてから最大 0.8 秒は鳴り残る**。
- **MUTE 中に `−` / `+` を押した場合**: **値は変わるが MUTE は解けない**(値と MUTE は別のフラグ、
  という原則を崩さない)。値は灰色のまま動く。
- **`hostapi_audio_set_volume` を `master_ui` 経由にする**(F4)。
  `masterui_set_level(MASTERUI_MASTER, v)` を足して両ホストの native から呼ぶ。
  **シグネチャ・Host API の関数は不変**。これで**アプリが呼んでも Master の MUTE は守られ、
  オーバーレイの表示とも一致する**。呼ぶアプリは本フェーズで無くなるが、ABI には残るので
  穴を塞いでおく(**小さい変更なので本フェーズで直すことを推奨。承認を仰ぐ点 5**)。

### b. 行の並べ方

| 案 | 内容 | 評価 |
|---|---|---|
| **(i)(推奨)** | **ラベルそのものを MUTE ボタンにする** | **専用ボタンが増えず横幅の問題が消える。** `ui-conventions.md` §1「状態表示がそのまま操作面になる」に合う |
| (ii) | バーを細くして MUTE ボタンを入れる | バー 75px → 約 30px。バーの意味が薄れる |
| (iii) | バーをやめて MUTE ボタンを入れる | バーは 21b で「使って要否を判断する」とした。判断材料なしに消すことになる |

**(i) の具体**:

```
 Settings                              X
 [Master ]  [ - ]  50  [ + ]  ▓▓▓▓░░░░      ← MUTE OFF: ラベル緑 / 値緑 / バー緑
 [MP3    ]  [ - ]  35  [ + ]  ▓▓░░░░░░
 [Synth  ]  [ - ] 100  [ + ]  ▓▓▓▓▓▓▓▓
 [Click  ]  [ - ] 100  [ + ]  ▓▓▓▓▓▓▓▓      ← MUTE ON: ラベル・値・バーを灰色
```

- **ラベルの下に `−` / `+` と同じ色の箱**を敷く(x 8〜84、行の高さ 30)。**押せることが見える**ように
  するため(帯の中で押せるものはすべて箱、に揃える)。
- **状態は文字色**(`ui-conventions.md` §4「状態の色」: **ON = 緑 / OFF = 灰**)。
  **MUTE OFF(鳴る)= ラベル緑、MUTE ON(鳴らない)= ラベル・値・バーの塗りを灰色。**
  背景色は使わない(選択カーソルの語彙と衝突させない。§4 の方針)。
- **タップで即トグル**(DOWN で切り替え。`−` / `+` が DOWN で 1 ステップ動くのと揃える)。
  **長押しにはしない**(戻せる操作なので 1 タップ。§2「壊す操作は長押し」の裏返し)。
- **当たり判定は共有コード**に足す: `MASTERUI_MUTE_X = 8` / `MASTERUI_MUTE_W = 78`(〜x 86、
  `−` の x 90 との間に 4px の隙間)。**描画座標と同じ定数で判定する**(21a の教訓: 記号と判定を別の
  定数にするとずれる)。
- Master を MUTE しても**他の行の表示は変えない**(それぞれの行は自分の MUTE だけを表す)。

### c. sequencer のメトロノームボタンを外す

- **`CLICK_ON` を廃止し、click は常に積む**(鳴らすかは Settings の Click MUTE が決める)。
  **既定で Click は MUTE なので、起動直後の Sequencer は click が鳴らない**(D-15 / P8 のとおり)。
- **`T_CLICK` は定数ごと消す**(空文字で残すのではなく)。
  - **理由**: 描画スロットは**描いた座標で初めて確保される**。一度も描かなければスロットを消費しない。
    空文字で描き続けるとスロットを 1 つ持ち続ける。`SC_NAME` で `T_CLICK` に空文字を書いている
    1 行も消す。`T_ROW0` 以降は 1 つずつ詰める(`TEXTS` は 29 → 28)。
- **▶ / ■ の当たり判定を広げる**: `STA_PLAY_HIT_X` 276 → **234**(旧クリックの左端)。
  **記号の位置(x 292)は動かさない。** 当たり判定は x 234〜320(86px、`RPT` の 84px と同程度)。
  **演奏中の主操作なので的は大きい方がよい。** x 130〜234 は従来どおりどのセルにも属さない。
  - `status_cell_at` の cell 3 を消し、`on_status_cell` の `cell == 3` の分岐も消す。
  - **21d でステータス行を作り直すときに再検討**してよい(本フェーズでは「空いた分を ▶ に渡す」だけ)。
- sequencer の CLICK ポートの使い方(`ACCENT_SLOT` = 1568Hz、slot 0 = 1000Hz)は**変えない**
  (Click =「機能的なクリック」の定義どおり)。

### d. metronome アプリを SYNTH へ移す

- **`seq_write(port=SYNTH, status=0x99, data1=34 or 33, data2=velocity)`**。
  **小節頭 = 34(Metronome Bell)、他 = 33(Metronome Click)。** velocity は**両方 127**
  (違いは音色の式で付ける。アプリ側で強弱を付けるとバランスの調整箇所が 2 つに割れる)。
  **`hostapi_tone_define` の呼び出し・`ACCENT_SLOT`・`PORT_CLICK`・`OP_TONE` は削除。**
- **音量は Synth チャネルに乗る**(マスター × Synth)。**Click の MUTE には影響されない。**
  **MP3 再生中は鳴らない**(SYNTH の既存契約。metronome は MP3 を使わないので実害なし)。
- **音色(最低限。ウッドブロック系の短い共鳴音)**:
  **新しい描画コードは足さず、既存の Snare の描画経路(減衰サイン + 減衰ノイズ)を再利用する**。
  ノイズを数 ms で消えるように速く減衰させると、**「叩いた瞬間のアタック + 短い共鳴」**になる。
  `voice_start_drum` の `switch` に 2 つの `case` を足すだけで、`voice_render` は変えない
  (`VoiceKind` に `VK_WOOD` を足し、`voice_render` の `VK_SNARE` の分岐に並べる)。

  | note | 役 | サイン | 長さ | サイン振幅 | ノイズ振幅 / 減衰 |
  |---|---|---|---|---|---|
  | **33 Click** | 通常拍 | **1,200Hz** | **60ms** | 8,000 × g | 3,000 × g / **4ms** で消える |
  | **34 Bell** | 小節頭 | **2,000Hz** | **150ms** | 8,000 × g | 3,000 × g / **4ms** で消える |

  - **中〜高域**(内蔵スピーカーは低域が出ない。所感 D-1)。**Bell は Click より高く、長く鳴る**
    (= 明るく目立つ)。数値は**実機の耳で決める**前提の初期値。
  - **振幅はこれまでのクリック(12,000 × level、30ms)と同じくらいの大きさ**に置いた
    (ノイズとサインの和でピーク ~11,000)。metronome アプリは単音なので 4 音同時のクリップの
    懸念は無い。
  - **式は両ホストで同一**(P6)。定数はコメントで「実機 `audio.cpp` と同じ値にすること」と縛る。
- **`shared/hostapi_defs.h` の差分(案)**:

```
- * - **note 番号は GM ドラム準拠。** v1 が鳴らすのは次の 4 つで、**未知の番号は
+ * - **note 番号は GM ドラム準拠。** 鳴らすのは次の 6 つで、**未知の番号は
  *   何もしない**(ログも出さない。L0 のディスパッチャから呼ばれるため):
  *     36 = Bass Drum / 38 = Acoustic Snare / 42 = Closed Hi-Hat / 49 = Crash Cymbal
+ *     33 = Metronome Click / 34 = Metronome Bell(Phase 21c。GM2 / GS の番号)
+ *   33 / 34 は**メトロノームの音**(ウッドブロック系の短い共鳴音。34 のほうが高く長い)。
+ *   **追加は非破壊**(今まで無視されていた note が鳴るようになるだけ)。
  ...
+/* Phase 21c で追加(メトロノーム) */
+    HOSTAPI_SYNTH_NOTE_METRO_CLICK = 33,
+    HOSTAPI_SYNTH_NOTE_METRO_BELL  = 34,
```

  **Host API の関数は増えない。** `docs/hostapi.md` §5 の SYNTH 表の「note 番号」の行と、
  §6 要件 1(高精度メトロノーム)に「Phase 21c で SYNTH へ移した」旨を足す。

### e. metronome / mp3player から音量 UI を外す

- **metronome**:
  - 削除: `Vol:` 表示、`V−` / `V+`、`VOLUME` / `VOLUME_MIN/MAX/STEP`、`apply_volume_delta`、
    `hostapi_audio_set_volume` の extern と呼び出し。
  - **FINE 行は `-1` / `+1` だけ**。**位置はそのまま**(`-1` は `BPM−` の真上、`+1` は `BPM+` の真上。
    **意味の対応が縦に揃っている**ので詰めない)。**右の 2 枠は描かない**(背景色のまま空ける)。
  - ステータス行は `BPM: 120   beats/bar: 4`。
  - **BPM・拍子・START の操作は変えない**(21d)。
- **mp3player**:
  - 削除: `vol:` 表示、`V−` / `V+`、`VOLUME`、`hostapi_audio_set_volume` の extern と呼び出し。
  - **`BTN_LABELS` = PLAY / PAUS / STOP**。**位置はそのまま**(x 12 / 74 / 136)、右の 2 枠は描かない。
    **並べ直しは 21d**(PLAY / PAUSE / STOP の統一と一緒に)。
- **21b の残課題 B-2(キャッシュ `VOLUME = 98` と表示のずれ)はこれで解消**する。

### f. ミキサーの改名

- 表示「Drums」→「**Synth**」。
- **`MASTERUI_DRUM` → `MASTERUI_SYNTH`、`MASTERUI_DEF_DRUM` → `MASTERUI_DEF_SYNTH` に改名する**
  (チャネル = ポートの原則を名前にも通す)。ホスト内部の `s_gain_drum` も `s_gain_synth` に揃える。
  `Set_Port_Gain(mp3, drum, click)` の引数名も `synth` に。**いずれもホスト内部で、ABI には現れない。**

### g. 回帰の基準値の取り直し

- **metronome / mp3player の Linux highmark を取り直す**(現 23,864 / 19,888)。
  **まず「ソースを変えずに再ビルド」した `.wasm` が既存とバイト一致するかを見る**
  (ツールチェーンの差とコード変更の差を分けるため。一致しなければその差分も記録する)。
- **既存 3 本(touch_demo 15,944 / midi_loopback 29,120 / seq_smoke 28,968)は不変**であることを確認。
  sequencer は変わる(`CLICK_ON` 削除で小さくなる見込み)ので新しい値を記録する。
- **`device-regress.conf` は書き換える箇所が無い見込み**(F7。アプリごとの期待値を持っていない)。
  実機回帰で新しい 2 本の `free_int` 差分 +0 と、下限しきい値の内側であることを確認するだけ。
  **実機の WAMR プール余裕は sequencer で確認**(ゲート 4: 2KB を切ったら止まる。減る方向の変更のみ)。

### h. 検証計画

| # | 内容 | どこで | 合格 |
|---|---|---|---|
| V1 | ミキサーの 4 行にラベル箱(MUTE)が出る | Linux キャプチャ | 4 行とも箱。ラベルが **Master / MP3 / Synth / Click** |
| V2 | ラベルのタップで MUTE ↔ 解除、**アンミュートで元の値に戻る** | Linux キャプチャ | 灰 ↔ 緑。値の数字は変わらない |
| V3 | **Click は起動直後からミュート** | Linux キャプチャ | Click 行が灰 |
| V4 | sequencer のステータス行にメトロノーム記号が無い / ▶ が x 240 付近でも反応する | Linux キャプチャ | 記号なし。x 240 のクリックで再生が始まる |
| V5 | metronome / mp3player に `Vol:` / `V−` / `V+` が無い | Linux キャプチャ | 無い |
| V6 | **metronome が Click のミュートに関係なく鳴る** | Linux WAV | 既定(Click MUTE)で拍ごとにオンセット。1,200 / 2,000Hz 帯のエネルギー |
| V7 | **Sequencer の click は Click をアンミュートしたときだけ鳴る** | Linux WAV | 既定: 1,000 / 1,568Hz 帯がドラム以外で立たない → アンミュート後は拍ごとに立つ |
| V8 | **小節頭(34)と他の拍(33)が区別できる** | Linux WAV | 2,000Hz 帯は小節頭だけ、1,200Hz 帯は他の拍で優勢。長さ(包絡線)も違う |
| V9 | **Synth をミュートするとドラムと metronome が両方消える** | Linux WAV | 両方でオンセットが消える(鳴り残りは最大 0.8 秒) |
| V10 | Master の MUTE で全体が消え、アンミュートで戻る | Linux WAV | ピーク 0 → 元のピーク |
| V11 | 実機で metronome の新しい音・ミュートの手触り | **実機 + ユーザーの耳** | ユーザー判断(数値は耳で直す) |
| V12 | 回帰 6 本 | 実機(`--task phase21c-regress`)+ Linux | ALL PASS。既存 3 本の highmark 同値、metronome / mp3player / sequencer は新基準を記録 |

- **UI 操作の自動化**: オーバーレイは**上端からのドラッグ**で開く(実ポインタのドラッグ =
  **絶対座標 + 原点の較正**。`docs/lessons.md` Phase 21b)。ラベル箱・▶ は**合成クリック**
  (`click --window`、論理座標 × 2)。すべて**キャプチャで届き先を確かめながら**行う(§3.6)。
- **V6〜V10 の WAV**: `MIDIBOX_WAV_OUT` で書き出し、Python 標準ライブラリで goertzel / 包絡線(§3.8)。
  **MUTE をかけた時刻は WAV 上の位置と突き合わせる**(操作の前後で区間を切る)。
- **metronome のクロック精度(V4 / MIDI Clock)は測らない予定**(音の出し先を変えただけで、
  クロックは L1 がグリッドから生成するため)。気になる兆候があれば 1 回測る。

### 承認を仰ぎたい点(まとめ)

1. **MUTE は値と別のフラグ、Master を含む 4 行すべてに付ける。既定は Click だけ MUTE。**
   **効き方は「次の発音から」**(MP3 だけは即座)。**MUTE 中の `−` / `+` は値だけ動かす。**
2. **行の形は (i): ラベルを箱にして MUTE ボタンにする。** 状態は文字色(緑 = 鳴る / 灰 = MUTE)、
   MUTE 中は値とバーも灰。**タップで即トグル。**
3. **sequencer: `CLICK_ON` と `T_CLICK` を削除し、▶ / ■ の当たり判定を x 234 まで広げる。**
4. **metronome: SYNTH の 34(小節頭)/ 33 で鳴らす。音色は Snare の描画経路を再利用した
   「減衰サイン + 4ms のノイズ」、Click 1,200Hz / 60ms、Bell 2,000Hz / 150ms を初期値に、耳で直す。**
   **FINE 行・mp3player のボタンは位置を変えずに消した枠を空ける。**
5. **`hostapi_audio_set_volume` を `master_ui` 経由にする**(F4。シグネチャ不変・Host API 不変。
   Master の MUTE を素通りしないように)。
6. **`MASTERUI_DRUM` → `MASTERUI_SYNTH` などの識別子の改名**(ホスト内部のみ)。
7. **F5 の波及を受け入れてよいか**: Click の既定 MUTE で **touch_demo のボタンタップ音、seq_smoke /
   synth_probe のクリックも起動直後は鳴らない**(`hostapi_tone_play` も CLICK と同じゲインを通るため)。
   **推奨: 受け入れる**(Click =「機能的なクリック」の定義どおりで、Settings で戻せる。
   seq_smoke の合否は MIDI の CC で判定しているので回帰は影響を受けない)。
   **既存 3 本の `.wasm` は変えない**(ゲート 3)。

### 承認(2026-09-23)

- **1〜6 は設計メモのとおり承認。**
- **7 は受け入れる。** ユーザー:「元々、これらのアプリが Click に相当するサウンドを使っていることに
  起因しているという認識なので、これは別途のアクション(シンセの音源に切り替える等)にすべき」。
  → **roadmap ② に U-25 として起票した**(21c では既存 3 本を再ビルドしない)。

## ステップ 1: ミキサーの MUTE

- **`shared/master_ui.c` / `.h`**: `s_muted[]` と既定の表(**Click だけ `MASTERUI_DEF_MUTE_CLICK = true`**)、
  `masterui_is_muted` / `masterui_effective` / `masterui_set_level` を追加。
  **フックへは実効値(MUTE 中は 0)を渡す**ので、**ホストのゲイン計算は変えていない**。
  **ラベルの箱(`MASTERUI_MUTE_X = 8` / `MASTERUI_MUTE_W = 78`)の DOWN で即トグル**。
  描画と当たり判定は同じ定数。
- **改名**: `MASTERUI_DRUM` → `MASTERUI_SYNTH`、`MASTERUI_DEF_DRUM` → `MASTERUI_DEF_SYNTH`、表示「Drums」→「Synth」、
  ホスト内部の `s_gain_drum` / `gain_drum_` / `Set_Port_Gain` の引数 → `synth`。
- **描画(両ホスト)**: ラベルの下に `−` / `+` と同じ色の箱。**文字色で状態**(緑 `0x40e070` = 鳴る /
  灰 `0x707880` = MUTE)。MUTE 中は**ラベル・値・バーの塗り**を灰に。
  実機は `mui_update_value()` で色も更新する(タッチのたびに `mui_sync` から呼ばれる)。
- **`hostapi_audio_set_volume` を `masterui_set_level(MASTERUI_MASTER, v)` 経由にした**(両ホスト。0-0 F4)。
  **副次的に Linux の既存の不具合も消えた**: 旧実装は最後に `Mix_VolumeMusic(v)` を**MP3 ゲインを掛けずに**
  呼び直しており、アプリが音量を変えると MP3 だけミキサーの MP3 ゲインが外れていた。
- **ビルド**: Linux / ESP32 とも成功(警告は既存の `main.c` の `-Wformat-truncation` のみ)。
  途中で `set_gain` の置換漏れ(`s_gain_synth = drum;`)で ESP32 のビルドが 1 回落ちた。

**Linux キャプチャでの確認**(`captures/phase21c/s1_*.png`。metronome の上で上端スワイプ):

| 画像 | 内容 | 結果 |
|---|---|---|
| `s1_open` | 起動直後 | **4 行とも箱付きのラベル。Master / MP3 / Synth は緑、Click は灰(既定 MUTE)** |
| `s1_toggled` | Click のラベルをタップ → Master のラベルをタップ | **Click が緑(値 100 のまま)、Master が灰(値 50 のまま)** |
| `s1_master_back` | Master をもう一度タップ | **Master が緑 50 に戻った**(アンミュートで元の値) |

音への効き(WAV)はステップ 4 でまとめて確認する。
## ステップ 2: SYNTH に note 33 / 34

- **両ホストの `voice_start_drum` に `case 33 / 34` を足した**(`VoiceKind` に `VK_WOOD`)。
  **描画は Snare の経路(減衰サイン + 減衰ノイズ)を共有**し、`voice_render` の変更は分岐に `VK_WOOD` を並べただけ。
  - 33 Click: サイン 1,200Hz / 60ms / 8,000 × g、ノイズ 3,000 × g を 4ms で e^-5 まで減衰
  - 34 Bell: サイン 2,000Hz / 150ms / 同上
  - **定数は両ホストで同一**(コメントで相互参照)。
- 契約: `shared/hostapi_defs.h` に `HOSTAPI_SYNTH_NOTE_METRO_CLICK = 33` / `_BELL = 34` と文面
  (**非破壊**、**Synth チャネルの値と MUTE が掛かり、Click の MUTE は効かない**)、`docs/hostapi.md` §5 の表。
  **Host API の関数は増やしていない。**

## ステップ 3: アプリの手直し(sequencer / metronome / mp3player)

- **再ビルドの前に、ソースを変えずに再ビルドした `.wasm` が既存とバイト一致することを確認した**
  (metronome 3,910 / mp3player 2,942 / sequencer 43,705 B。rustc 1.95.0)。
  → 以降のサイズ差はすべてコード変更によるもの。
- **sequencer**: `CLICK_ON` / `T_CLICK` / `SYM_METRO` / `STA_CLICK_X` / `STA_CLICK_HIT_X` / cell 3 を削除。
  `write_clicks` は常に積む。`T_ROW0` 6 → 5、`TEXTS` 29 → 28。**`STA_PLAY_HIT_X` 276 → 234**。
- **metronome**: `seq_write(port=SYNTH, 0x99, 34 / 33, 127)`。`hostapi_tone_define` / `hostapi_audio_set_volume`、
  `VOLUME` 一式、`apply_volume_delta`、`V-` / `V+` を削除。FINE 行は `-1` / `+1` の 2 枠(位置は据え置き)。
- **mp3player**: `vol:`、`V-` / `V+`、`VOLUME`、`hostapi_audio_set_volume` を削除。ボタンは PLAY / PAUS / STOP の 3 枠(位置は据え置き)。

| `.wasm` | 変更前 | 変更後 |
|---|---|---|
| metronome | 3,910 | **3,641** |
| mp3player | 2,942 | **2,708** |
| sequencer | 43,705 | **43,549** |

## 回帰の基準値の取り直し

**Linux の WAMR プール消費(highmark)**:

| アプリ | 21a / 21b | **21c** | |
|---|---|---|---|
| touch_demo | 15,944 | **15,944** | 同値(`.wasm` 不変) |
| mp3player | 19,888 | **19,112** | 新基準(−776) |
| metronome | 23,864 | **22,952** | 新基準(−912) |
| midi_loopback | 29,120 | **29,120** | 同値(`.wasm` 不変) |
| seq_smoke | 28,968 | **28,968** | 同値(`.wasm` 不変) |
| sequencer | 153,720 | **153,288** | 新基準(−432) |

**実機**:

- 回帰は 6 本とも **`free_int` 151,752 / `largest_int` 102,400**(21b は 151,720 / 102,400。+32 B はホストのコード差)。
- **sequencer のプール(起動直後の初回ロードで測定)**: **highmark 110,184 / 130,880、残り 20,696 B**
  (21b は 112,424、残り 18,456 B)。**ゲート 4(2KB)に対して余裕が増えた**。
  回帰の中では sequencer が初回ロードにならず highmark が壊れた値(4294967xxx)になるので、
  リセット直後にシリアルの `run sequencer` で単独に測った(`captures/phase21c/pool.log`)。
- **`device-regress.conf` は変更なし**(アプリごとの期待値を持っておらず、しきい値 146,000 / 98,304 の内側)。

## 検証(Linux の画面と WAV / 実機)

**画面**(`captures/phase21c/*.png`):

| # | 内容 | 結果 |
|---|---|---|
| V1 | ミキサーの 4 行にラベル箱、Master / MP3 / Synth / Click | **PASS**(`s1_open`) |
| V2 | タップで MUTE ↔ 解除、アンミュートで元の値 | **PASS**(`s1_toggled` / `s1_master_back`) |
| V3 | Click は起動直後からミュート | **PASS**(`s1_open`、`m1_synth_muted`) |
| V4 | sequencer のステータス行にメトロノーム記号が無い / x 240 で ▶ | **PASS**(`q1_menu` に記号なし、`q1_playing` は x 240 のタップで再生開始 = ■) |
| V5 | metronome / mp3player に Vol / V− / V+ が無い | **PASS**(`m1_app` / `mp3_app`) |

**WAV**(`MIDIBOX_WAV_OUT`。goertzel は各オンセットの先頭 30ms):

- **metronome**(`m1.wav`、既定 = Click MUTE のまま START):
  - **V6 PASS**: 拍ごとにオンセット(500ms 間隔、120bpm)。**Click の MUTE に関係なく鳴る**。
  - **V8 PASS**: **4 拍に 1 回だけ 2,000Hz が立ち**(1,283〜1,420)、他の拍は **1,200Hz**(877〜947)。
    相手の帯は 5〜45 で、**帯域で完全に分かれる**。**長さも Bell ~125–130ms / Click ~50–55ms**(5% 減衰まで)。
    ピークは両方とも ~4,400〜5,000(Master 50 × Synth 100)。
  - **V9(metronome 側)PASS**: Synth を MUTE した区間(8.27〜13.77 秒)に**オンセット 0**、解除後に再開。
- **sequencer**(`q1.wav`、Drum Machine のパターンを再生しながら Settings を操作。時刻は `q1_times.txt`):

  | 区間 | 長さ | クリック音(1,000 / 1,568Hz)のオンセット | ドラム帯域のオンセット |
  |---|---|---|---|
  | 既定(Click MUTE) | 16.0s | **0** | 16 |
  | Click をアンミュート | 4.4s | **9** | 5 |
  | さらに Synth を MUTE | 4.4s | 8 | **0** |
  | さらに Master を MUTE | 3.5s | **0**(35.21 → 39.21 秒の 4 秒間無音。境界の 1 発を除く) | 0 |
  | Master をアンミュート | — | 再開 | 0(Synth は MUTE のまま) |

  - **V7 PASS**(click は Click をアンミュートしたときだけ)、**V9 PASS**(Synth MUTE でドラムが消える)、
    **V10 PASS**(Master MUTE で全体が消え、戻すと再開)。警告 0 件。

**実機(V11)PASS**: ユーザーが実機で metronome の新しい音(Bell / Click)、Synth の MUTE、
Sequencer での Click のアンミュートを確かめ、「**動作確認しました。OK です**」(2026-09-23)。
**音色の初期値(1,200Hz / 60ms、2,000Hz / 150ms)はそのまま確定**。

**ユーザーの質問と判断**: 「metronome アプリで Settings の Click を有効にしても鳴らないのは意図的か。
一貫性という意味では鳴らしてもよい」。
→ **意図的**(設計メモ d、指示書の完了条件)。ただし**規約と README に書いていなかった**ので追記した。
鳴らす案(CLICK にも積む)は **Click 有効時に同じ拍が二重に鳴る**ので、
**「Click = 機能的なクリック、metronome の拍の音 = 楽器(Synth)」の線引きを保つ**ことを推奨し、
**ユーザーが「今のままでよい」と判断**。`ui-conventions.md` §3.7 と `wasm-apps/README.md` に明記した。

## 回帰(6 本)

- **Linux ALL PASS**(`captures/phase21c/regress/`。6 本とも started / stopped 各 1、警告 0、残留なし)。
  highmark は上の表。**既存 3 本は前フェーズと 1 バイトも同じ。**
- **実機 ALL PASS**(`captures/phase21c-regress/report.md`、22 項目 / FAIL 0。全行 `free_int` 差分 +0、反復 3 回も同一、
  許容外の WARN/ERROR 0 件)。
## 仕様からの逸脱

| # | 指示書 / 設計メモ | 実際 | 理由 |
|---|---|---|---|
| 1 | (指示書に無し) | **`hostapi_audio_set_volume` を `master_ui` 経由にした** | 設計メモ 0-0 F4 / 承認点 5。直接書くと Master の MUTE を素通りする。**副次的に Linux の既存不具合**(アプリの音量変更で MP3 のミキサーゲインが外れる)**が消えた** |
| 2 | (指示書に無し) | **Click の既定 MUTE が touch_demo / seq_smoke / synth_probe のクリックにも効く** | 設計メモ 0-0 F5 / 承認点 7。**ユーザーが受け入れ、アプリ側を SYNTH へ移す別アクションとして roadmap U-25 に起票** |
| 3 | 設計メモ c「スロットは空文字で残すのが既定」(指示書) | **`T_CLICK` を定数ごと削除**(`TEXTS` 29 → 28) | 設計メモ c。一度も描かなければスロットを消費しない |
| 4 | 設計メモ d「ピーク ~11,000」 | 実測ピーク **~4,400〜5,000**(Master 50 / Synth 100。フル音量換算 ~9,400) | 見積もりはノイズとサインの単純和。実害なし(耳で OK) |
| 5 | `device-regress.conf` の基準値の更新 | **変更なし** | アプリごとの期待値を持っておらず、しきい値の内側だったため(F7) |

## 21d(操作規約の統一)と音色フェーズへの申し送り

- **21d**:
  - **metronome の FINE 行(右 2 枠)と mp3player のボタン行(右 2 枠)は空けたまま**。PLAY / PAUSE / STOP の統一と
    BPM / 拍子のシャトル化のときに並べ直す。
  - **sequencer のステータス行は ▶ / ■ の当たり判定を x 234 まで広げただけ**。x 130〜234 はどのセルにも属さない。
    パンくず・戻る操作と一緒に作り直すときに再検討する。
- **音色フェーズ**:
  - **metronome の音色は最低限**(Snare の描画経路を流用した「減衰サイン + 4ms のノイズ」)。
    **振り子らしいメトロノーム音**はここで作り込む(`voice_start_drum` の `case 33 / 34` を差し替えるだけで、
    アプリも契約も変わらない)。
  - 所感 D-1(内蔵スピーカーで Kick が聞こえない)/ D-3(音色ごとのバランス)は未着手のまま。
- **ミキサーの軸**: チャネル = ポートのまま。将来メロディ楽器を同時に鳴らす段では **MIDI チャネル(パート)**が
  自然な軸になる(指示書作成時の所見。SYNTH は今チャネルを無視している)。

## 残課題

| # | 内容 | 温度感 |
|---|---|---|
| C-1 | **touch_demo / seq_smoke / synth_probe のクリックが既定で鳴らない**(Click の既定 MUTE)。**roadmap U-25**(アプリを SYNTH の音源へ) | 回帰対象の再ビルドを許す回で |
| C-2 | **設定が電源で消える**(21b B-1、roadmap U-24)。**MUTE が 4 つ増えたぶん、合わせ直す手間がさらに増えた**(ただし既定が「Click だけ MUTE」なので多くの場合そのままで使える) | 21d の後 |
| C-3 | **回帰中の実機プール(highmark)は初回ロードのアプリでしか正しく取れない**(18 の既知)。sequencer の余裕は**リセット直後に `run sequencer` を単独で送って測った**。回帰スクリプトで取れるようにするかは未検討 | 記録のみ(workflow への反映はユーザー判断) |
