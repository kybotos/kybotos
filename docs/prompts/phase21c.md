# Phase 21c: ミキサーの MUTE と、アプリ側の音量 UI の整理

- 契約日: 2026-09-23
- 参照: `docs/results/phase21b.md`(直前の到達点。マスター設定とミキサー)、
  `docs/results/phase21a.md`(所感 D-15)、`docs/results/phase21.md`(内蔵音源)、
  `docs/design/ui-conventions.md` §3.7(装置の設定)、`docs/hostapi.md` §5(SYNTH)/ §7(audio)、
  `shared/master_ui.h` / `master_ui.c`、`shared/hostapi_defs.h`、
  `src/components/wasm_runtime/hostapi.cpp`(`mui_*`)、`src/components/audio/audio.cpp`、
  `hosts/linux/hostapi_sdl.c`、`wasm-apps/sequencer/`、`wasm-apps/metronome/`、`wasm-apps/mp3player/`
- 結果報告先: `docs/results/phase21c.md`

## 目的

**「音量と鳴らす / 鳴らさないは Settings で決める」に一本化する。**

Phase 21b でマスター設定(Master / MP3 / Drums / Click のミキサー)を作ったので、
**アプリが個別に持っていた音量・ON/OFF の UI は役目を終えた**。本フェーズで:

1. **ミキサーの各チャネルに MUTE を付ける**(ON/OFF をここで一律に決める)。
2. **アプリ側の重複した UI を外す**(sequencer のメトロノームボタン、metronome と mp3player の
   `Vol:` / `V−` / `V+`)。
3. **Click を「機能的なクリック」に限定し、既定は MUTE にする**(Sequencer の Click 既定 OFF)。
   そのために **metronome アプリの音を CLICK ポートから内蔵音源(SYNTH)へ移す**。

**すべて「マスター設定に移したから要らなくなった」という 1 つの理屈で説明できる範囲**に留める。
操作規約の統一(PLAY / PAUSE / STOP、パンくず、戻る操作、スクロール)は **Phase 21d**。

## 決定済みのスコープ(2026-09-22〜23 のユーザー回答)

- **Settings のミキサーの各要素に MUTE ボタンを設け、ON / OFF をここで一律に設定する。**
- **これに伴い、sequencer のメイン画面のメトロノーム ON/OFF ボタンは無くす。**
- **Sequencer の Click は既定 OFF。**
- **metronome アプリの `Vol:` / `V−` / `V+`、mp3player の `Vol:` / `V−` / `V+` を削除する。**
- **PLAY / PAUSE / STOP の統一は 21c に含めない**(21d。操作規約の統一として、
  パンくず・戻る操作・スクロールと一緒にやる。片方だけ入れるとちぐはぐになるため)。
- **Click は「機能的なクリック」として扱う**(ユーザー:「Metronome アプリについても音色を変えたい
  という要望はありそう(元々は振り子のリアルなメトロノーム音を想定していた)ので、
  Click はあくまで機能的な Click という扱いにして良い」)。
- **ミキサーの「Drums」は「Synth」に改名する**(チャネル = ポートの原則。
  SYNTH ポートは内蔵音源で、ドラムはそこで鳴っている楽器の 1 つにすぎない)。
- **metronome アプリの音は SYNTH ポートの GM2 / GS「Metronome Click(33)/ Metronome Bell(34)」で鳴らす。**
  **音色は最低限でよい**(ウッドブロック系の短い共鳴音)。**振り子らしさの作り込みは音色フェーズ。**

## 前提(再調査不要。ただし着手時にソースで確認すること)

- **P1: ミキサーは `shared/master_ui.c` にある。**
  `masterui_item_t` = `MASTERUI_MASTER` / `MP3` / `DRUM` / `CLICK`、ラベル `"Master"` / `"MP3"` /
  `"Drums"` / `"Click"`、既定 50 / 35 / 100 / 100。**実効音量 = マスター × チャネル。**
  1 行 = **ラベル(x 12)+ `−`(x 90, w 40)+ 値(x 142)+ `+`(x 185, w 40)+ バー(x 235, w 75)**、
  行ピッチ 34、1 行目 y 34、帯の高さ 172。**描画は両ホストで別**(実機 `hostapi.cpp` の `mui_build` /
  `mui_update_value`、Linux `hostapi_sdl.c` の `draw_master_overlay`)。**当たり判定は共有コード。**
  **MUTE を足すと横幅が足りない**(320px にラベル・`−`・値・`+`・バーが並んでいる)ので、
  **並べ方をステップ 0 で決める**。
- **P2: チャネルのゲインの掛け場所。**
  実機: `Mp3Player::set_gain(mp3, drum, click)` → `gain_mp3_`(`write_fn` で MP3 に掛ける)、
  `s_gain_drum` / `s_gain_click`(`voice_start_drum` / `voice_start_tone` で発音時に掛ける)。
  Linux: `s_gain_mp3`(`Mix_VolumeMusic`)/ `s_gain_drum` / `s_gain_click`(ボイス生成時)。
  **発音中の音には効かず、次の発音から効く**(既存契約)。**MUTE も同じ扱いでよい**か、
  鳴っている音を即座に止めるかはステップ 0 で決める。
- **P3: sequencer のメトロノームボタン。**
  `wasm-apps/sequencer/src/lib.rs` の **`CLICK_ON`**(既定 true)、ステータス行の **`T_CLICK`**
  (`STA_CLICK_X = 252`、記号 `SYM_METRO` = U+F001)、当たり判定 `STA_CLICK_HIT_X = 234`〜
  `STA_PLAY_HIT_X = 276`(`status_cell_at` の cell 3)、`on_status_cell` の `cell == 3` でトグル、
  `write_clicks` が `CLICK_ON` を見て積むかを決める。**Phase 21a で記号と当たり判定のずれを直した
  箇所**なので、消すときに ▶ / ■ の当たり判定(`STA_PLAY_HIT_X`)も見直すこと。
- **P4: metronome アプリの音。**
  `wasm-apps/metronome/src/lib.rs` は **`seq_write(port=CLICK, status=OP_TONE)`** で予約し、
  **小節頭は `ACCENT_SLOT`(1、`hostapi_tone_define` で 1568Hz)**、他は slot 0(既定クリック 1000Hz)。
  **Click を既定 MUTE にすると、metronome アプリが無音になる** → **SYNTH へ移すのが本フェーズの前提**。
  音量 UI は **`VOLUME = 98`(キャッシュ)/ `Vol:` 表示 / `FINE_LABELS` の `V−` / `V+`**
  (FINE 行は `-1` / `+1` / `V−` / `V+` の 4 つ)/ `hostapi_audio_set_volume` の呼び出し。
- **P5: mp3player の音量 UI。** `BTN_LABELS = [PLAY, PAUS, STOP, V−, V+]`、`VOLUME = 98`
  (キャッシュ)、`vol:` 表示、`V−` / `V+` で ±10 して `hostapi_audio_set_volume`。
  **PLAY / PAUS / STOP は 21c では触らない**(21d)。
- **P6: SYNTH ポートの語彙。** **Note On だけを見る / チャネルは無視 / 未知の note は何もしない**
  (`shared/hostapi_defs.h`)。v1 は **36 / 38 / 42 / 49**。**33 / 34 を足すのは非破壊**
  (今まで無視されていた note が鳴るようになるだけ)。**音色の式は両ホストで同一**にすること
  (実機 `audio.cpp` の `voice_start_drum` / `voice_render`、Linux `hostapi_sdl.c` の同名関数)。
- **P7: 既存アプリの `.wasm` を再ビルドする。** **metronome と mp3player は Phase 18a 以降
  「回帰の基準」として再ビルドしてこなかった。** 本フェーズは**意図的にこの約束を外す**
  (U-17 / D-18「既存アプリの UI refine」に踏み込む)。**基準値(Linux の highmark、
  `device-regress.conf` のアプリごとの期待値)を取り直す**必要がある。
- **P8: 設定は電源を切ると消える**(U-24)。**Click の既定 MUTE は、起動のたびに
  Sequencer の click が鳴らない状態から始まる**ことを意味する(ユーザーの意図どおり)。
- **P9: 検証の道具。** Linux は **`MIDIBOX_WAV_OUT` で WAV に落として機械判定**、UI の自動操作は
  **絶対座標 + 原点の較正**(`mousemove --window 0 0` → `getmouselocation`。
  `docs/lessons.md` Phase 21b)、実機は耳。手順は `docs/workflow.md` §3.6 / §3.8。

## ゲート(必須)

1. **ステップ 0(設計メモ)の報告 → 承認**を経てから実装に入る。
2. **SYNTH の語彙を増やす(note 33 / 34)のは契約の追加**なので、`shared/hostapi_defs.h` と
   `docs/hostapi.md` の文面を設計メモで示す。**Host API の関数は増やさない。**
3. **metronome / mp3player の `.wasm` の再ビルドは本フェーズで承認済み**(P7)。
   **それ以外の既存 3 本(touch_demo / midi_loopback / seq_smoke)は再ビルドしない。**
4. **実機 WAMR プールの余裕が 2KB を切ったら報告して止まる**(現在 sequencer で残り 18,456 B)。
5. `scripts/` / `docs/workflow.md` / `CLAUDE.md` の変更は提案 → 承認
   (`device-regress.conf` の基準値の更新は本フェーズの作業に含めてよい)。
6. **一時コードは入れた直後にも `git diff` で確認する。**

## スコープ

### 含む

#### ステップ 0: 設計メモ(実装なし・承認ゲート)

`docs/results/phase21c.md` のステップ 0 節に、次をすべて書く。

- **a. MUTE の意味と置き場**
  - **MUTE はチャネルの値(0..100)とは別のフラグ**にする(**アンミュートで元の値に戻る**)。
    **実効音量 = マスター × チャネル × (MUTE ? 0 : 1)**。
  - **Master にも MUTE を付けるか**(全体の消音)。推奨: 付ける(4 行とも同じ形にする)。
  - **既定**: Master / MP3 / Synth は MUTE OFF、**Click は MUTE ON**。
  - **鳴っている音への効き方**(P2): 次の発音から / 即座に止める。**推奨: 次の発音から**
    (既存の音量と同じ契約。ボイスの途中で切るとクリックノイズになる)。
    ただし **MP3 は連続音なので即座に効く**(`write_fn` で毎回掛けているため)。
- **b. 行の並べ方(横幅が足りない。P1)**
  - 案を出して選ぶ: (i) **MUTE をラベルの位置に重ねる**(ラベルをタップで MUTE、色で状態)、
    (ii) **バーを細くして MUTE ボタンを入れる**、(iii) **バーをやめて MUTE ボタンを入れる**。
  - **推奨は (i)**: **ラベルそのものを MUTE ボタンにする**(`ui-conventions.md` §1「状態表示が
    そのまま操作面になる」)。**ミュート中はラベルと値を灰色**にする(§4 の状態の色:
    ON = 通常 / OFF = 灰)。**専用のボタンが増えない**ので横幅の問題も消える。
  - **当たり判定を共有コードに足す**(両ホストで同じ)。
- **c. sequencer のメトロノームボタンを外す**
  - `CLICK_ON` を**廃止**し、**click は常に積む**(鳴らすかは Settings の Click MUTE が決める)。
  - `T_CLICK` の**スロットをどうするか**(空文字で残す / ▶ / ■ を左へ寄せる)。
    **スロットは座標キーで解放されない**ので、使わなくなったら空文字にするのが既定。
  - **▶ / ■ の当たり判定**(`STA_PLAY_HIT_X`)を**広げてよいか**(クリックの領域が空くため)。
- **d. metronome アプリを SYNTH へ移す**
  - `seq_write(port=SYNTH, status=0x99, data1=33 or 34, data2=velocity)`。
    **小節頭 = 34(Bell)、他 = 33(Click)。** `hostapi_tone_define` の呼び出しは不要になる。
  - **音色(最低限)**: ウッドブロック系の短い共鳴音。**中〜高域**(800Hz〜2kHz 程度)に置く
    (**内蔵スピーカーは低域が出ない**。所感 D-1)。**Bell は Click より高く明るく**。
    **両ホストで同じ式**にすること(P6)。
  - **`shared/hostapi_defs.h` の SYNTH の契約**に 33 / 34 を足す文面、
    `HOSTAPI_SYNTH_NOTE_METRO_CLICK` / `_BELL` の定数。
- **e. metronome / mp3player から音量 UI を外す**
  - **metronome**: `Vol:` 表示、`V−` / `V+`、`VOLUME` のキャッシュ、`hostapi_audio_set_volume` の呼び出し。
    **FINE 行は `-1` / `+1` だけになる**ので、**空いた場所をどうするか**(詰める / 空けたまま)を決める。
    **BPM・拍子・START の操作は変えない**(21d)。
  - **mp3player**: `vol:` 表示、`V−` / `V+`、`VOLUME` のキャッシュ。
    **`BTN_LABELS` は PLAY / PAUS / STOP の 3 つになる**。**並べ方は最小限の手直し**に留める(21d)。
- **f. ミキサーの改名**: 「Drums」→「**Synth**」(`k_labels`、`MASTERUI_DRUM` を
  `MASTERUI_SYNTH` に改名するかも決める)。
- **g. 回帰の基準値の取り直し**(P7)
  - **metronome / mp3player の Linux highmark**、`device-regress.conf` の**アプリごとの期待値**。
  - **既存 3 本(touch_demo / midi_loopback / seq_smoke)は不変**であることを確認する。
- **h. 検証計画**(下記ステップ 4)。

#### ステップ 1: ミキサーの MUTE(両ホスト)

- `shared/master_ui.c` / `.h` に MUTE の状態・既定値・当たり判定を足す。
- 両ホストの描画とゲインの掛け算に MUTE を反映する。
- 「Drums」→「Synth」。

#### ステップ 2: SYNTH に note 33 / 34(両ホスト)

- `voice_start_drum`(実機・Linux)に 2 音を足す。**式は両ホストで同一。**
- `shared/hostapi_defs.h` / `docs/hostapi.md` の契約を実装と同時に直す。

#### ステップ 3: アプリの手直し

- **sequencer**: `CLICK_ON` とメトロノームボタンを外す。
- **metronome**: SYNTH へ移す、音量 UI を外す。
- **mp3player**: 音量 UI を外す。
- `.wasm` を再ビルドしてコミットする(`wasm-apps/README.md` の手順)。

#### ステップ 4: 検証

- **Linux**:
  - キャプチャで (1) ミキサーの 4 行に MUTE が出る、(2) ミュートで灰色になり、アンミュートで
    元の値に戻る、(3) **Click は起動直後からミュート**、(4) sequencer のステータス行に
    メトロノームボタンが無い、(5) metronome / mp3player に `Vol:` / `V−` / `V+` が無い。
  - **WAV**: (6) **metronome アプリが Click のミュートに関係なく鳴る**、
    (7) **Sequencer の click は Click をアンミュートしたときだけ鳴る**、
    (8) **小節頭(34)と他の拍(33)が聞き分けられる**(帯域とピークで差を出す)、
    (9) **Synth をミュートするとドラムと metronome アプリが両方消える**。
- **実機**: ビルド・フラッシュ・**ユーザーに聴いてもらう**(metronome アプリの新しい音、
  ミュートの手触り)。
- **回帰 6 本**: 実機(`--task phase21c-regress`)と Linux。**アプリを再ビルドするので回帰は必要。**
  **metronome / mp3player は基準値を取り直し、既存 3 本は前フェーズと同値**であること。
  **metronome のクロック精度(V4)は、音の出し先を変えただけなので変わらない見込み**だが、
  **気になれば 1 回測る**(START のタップが要る)。

#### ステップ 5: 文書化

- `docs/results/phase21c.md`、`shared/hostapi_defs.h`、`docs/hostapi.md` §5 / §7、
  `docs/design/ui-conventions.md` §3.7(MUTE の形)、`wasm-apps/README.md`(3 本の説明)、
  `docs/lessons.md`、`docs/status.md`。
- `docs/roadmap.md` は次フェーズの指示書を書くときに更新する。

### 含まない

- **PLAY / PAUSE / STOP の統一、パンくず、戻る操作、スクロール、metronome の BPM / 拍子の
  シャトル化、色の規約の統一**(**Phase 21d**)。
- **振り子らしいメトロノーム音の作り込み**、**内蔵スピーカー向けの Kick**(所感 D-1)、
  **音色ごとのバランス**(D-3)(**音色フェーズ**)。
- **設定の保存**(U-24)、**SYNTH が MIDI チャネルを見る拡張**(パート単位のミキサー)。
- touch_demo / midi_loopback / seq_smoke の変更。

## 進め方

1. セッション開始時に `CLAUDE.md`・`docs/workflow.md`(通読)・`docs/lessons.md`・本書の参照先を読み、
   `docs/workflow.md` §3.0 の環境確認を行う。
2. **ステップ 0 報告 → 承認 → ステップ 1〜5。** 各ステップの結果は `docs/results/phase21c.md` に記録する。
3. 確認は Linux → 実機の順。シェル操作はすべて `scripts/hpane.sh` 経由。
4. 仕様・本書に無い判断をした場合は、results の「仕様からの逸脱」に必ず記録する。
5. **音は耳で決める**(Phase 21a の D-1 の教訓: 出力デバイスの特性は WAV では測れない)。
   WAV は「鳴っているか / 区別がつくか」の機械判定に使う。

## 完了条件

- [ ] ステップ 0 の設計メモ(a〜h)が承認されている
- [ ] **ミキサーの 4 行に MUTE**があり、**アンミュートで元の値に戻る**(両ホスト同じ操作)
- [ ] **Click は既定でミュート**、**Sequencer の click は Click をアンミュートしたときだけ鳴る**
- [ ] **sequencer のメイン画面にメトロノームボタンが無い**
- [ ] **metronome アプリは SYNTH の 33 / 34 で鳴り、Click のミュートに影響されない**
- [ ] **metronome / mp3player に `Vol:` / `V−` / `V+` が無い**
- [ ] ミキサーのラベルが **Synth**
- [ ] **Host API の関数を増やしていない**(SYNTH の note が 2 つ増えただけ)
- [ ] **touch_demo / midi_loopback / seq_smoke の `.wasm` を再ビルドしていない**
- [ ] **回帰 6 本が実機・Linux の両方で PASS**(metronome / mp3player は基準値を取り直し)
- [ ] 実機で**ユーザーが metronome アプリの新しい音を聴いて確認**している
- [ ] hostapi_defs.h・hostapi.md・ui-conventions・README・lessons・status が更新されている

## `docs/results/phase21c.md` の雛形

```markdown
# Phase 21c 実施記録 — ミキサーの MUTE と、アプリ側の音量 UI の整理

## ステップ 0: 設計メモ(a〜h)
## ステップ 1: ミキサーの MUTE
## ステップ 2: SYNTH に note 33 / 34
## ステップ 3: アプリの手直し(sequencer / metronome / mp3player)
## 回帰の基準値の取り直し
## 検証(Linux の画面と WAV / 実機)
## 回帰(6 本)
## 仕様からの逸脱
## 21d(操作規約の統一)と音色フェーズへの申し送り
## 残課題
```

## 追記(スコープ変更)

- (日付: 内容)
