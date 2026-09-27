# Phase 22: 回帰アプリの統合と、タップの自動化

- 契約日: 2026-09-27
- 参照: `docs/results/phase12.md`(回帰アプリの絞り込みとカバレッジ表)、`docs/results/phase14.md`(5 本になった経緯)、
  `docs/results/repo-split.md`(6 本 → 5 本)、`docs/workflow.md` §1-8 / §3.3 / §3.4 / §3.6 / §3.7、
  `scripts/device-regress.sh` / `device-regress.conf` / `linux-regress.sh` / `ui-linux.sh` / `wasm_funcs.py`、
  `src/main/serial_cmd.cpp`(シリアルコンソール)、`src/components/touch/touch.cpp`(`indev_read_cb`)、
  `src/components/wasm_runtime/hostapi.cpp`(`screen_input_event_cb` / 文字のスロット)、
  `src/components/wasm_runtime/launcher.cpp`(`launcher_prepare_sd` / `seed_file`)、
  `src/components/wasm_runtime/CMakeLists.txt`(埋め込むアプリの表)、`hosts/linux/main.c`(`scan_apps`)、
  `hosts/linux/hostapi_sdl.c`、`shared/hostapi_defs.h`、`wasm-apps/*`
- 結果報告先: `docs/results/phase22.md`

## 目的

**回帰を「実用アプリの起動と操作」+「Host API をひととおり叩く検査アプリ 1 本」にまとめ、実機・Linux の両方でタップまで自動で回す。**

いまの回帰は 5 本(touch_demo / mp3player / metronome / midi_loopback / seq_smoke)で、実用アプリは metronome と
mp3player の 2 本だけ。残りは検証用・診断用で、**実機のランチャーに並んで実用アプリが埋もれる**(回帰に入れていない
synth_probe も埋め込まれているので並ぶ)。また:

- **seq_smoke の自己検査の合否は画面の「PASS chk」でしか分からず、`device-regress.sh` は見ていない**
  (判定しているのは heap と警告だけ)。
- **Host API のうち、どの回帰アプリも使っていない関数がある**(`draw_text_rgb`、`fs_read` / `fs_write`、
  `tone_play`、任意 export の `app_key` など。ステップ 0 で機械抽出して確定させる)。
- **アプリ内の操作(metronome の START / STOP、mp3player の PLAY など)は自動化されていない**。
  実機は人間のタップ + カメラ(§3.3)、Linux は xdotool のクリックを「使わない」ことになっている(§1-8)。

本フェーズで:

1. **回帰アプリを 3 本にする**: **metronome / mp3player / 新しい検査アプリ**(仮名 `hostapi_check`)。
2. **検査用・診断用のアプリを既定のファームに入れない**(ランチャーに出さない)。
3. **タップを自動化する**: 実機はシリアルコンソールから、Linux も同じ語彙で。**画面の文字を読み出して判定する。**
4. **回帰スクリプトが、アプリごとの操作シナリオ(タップ → 表示の確認)を回す。**

## 決定済みのスコープ(2026-09-27 のユーザー回答)

- **回帰の対象は、実用アプリ(metronome / mp3player)の起動と操作 + Host API をある程度網羅する検査アプリ。**
  検査アプリは新しく作ってよい。**目的は回帰時間の短縮と、実機で実用アプリを目立たせること。**
- **検査用・診断用のアプリは、既定のファームに埋め込まない**(案 A)。ビルドのオプションを付けたときだけ埋め込む。
  ランチャーに「出さない印」を付ける方式(案 B)は採らない。
- **touch_demo は削除する**(タッチ座標の可視化が要るなら検査アプリに持たせる)。
- **Linux もタップを自動化する。** これは `docs/workflow.md` §1-8(「Linux の UI クリック自動化は使わない」)の
  見直しにあたり、**見直すこと自体はユーザー承認済み**。§1-8 の新しい文面はステップ 0 で示して承認を得る。
- 本フェーズは **Platform トラックの最初のフェーズ**(Sequencer トラックは 21f で終了。`docs/roadmap.md`)。

## 前提(再調査不要。ただし着手時にソースで確認すること)

- **P1: 回帰の設定。** `scripts/device-regress.conf` の `APPS` / `HOLD_SEC` / `HOLD_OVERRIDE`(seq_smoke 60 秒)/
  `REPEAT_RUNS`(3)/ `REPEAT_OVERRIDE`(seq_smoke 1)/ `ALLOW_PATTERNS` / `EXPECT_DELTA` / しきい値。
  **この repo の外のアプリは、この conf を source して `APPS` に足した conf を `--conf` で渡す**(§3.4 / §3.7)。
  **外の conf が壊れないこと**(`APPS` に足す形がそのまま動くこと)を本フェーズでも守る。
  **conf のスキーマ変更は承認ゲート**(`docs/roadmap.md` 冒頭)。
- **P2: 回帰にかかる時間。** 実機は 4 本 × 3 回(保持 6 秒 + 起動・停止)+ seq_smoke 60 秒 × 1 回。
  **最も長いのは seq_smoke の 60 秒**。統合後の所要時間を**統合前と同じ条件で測って比べる**。
- **P3: シリアルコンソール**(`src/main/serial_cmd.cpp`)は `ping` / `ls` / `run <app>` / `stop` / `heap` だけ。
  応答はタグ `KBCMD` のログ行。`CONFIG_KYBOTOS_SERIAL_CMD=y`(既定)。
- **P4: 実機のタッチの経路。** `touch.cpp` の **`indev_read_cb`**(生座標 → 較正 → 論理座標)→ LVGL →
  `hostapi.cpp` の **`screen_input_event_cb`**(PRESSED / PRESSING / RELEASED)→ **masterui の関所**
  (上端からの下スワイプ)→ `push_event` → アプリの `poll_event`。メニュー画面は `menu_input_event_cb`。
  **注入は `indev_read_cb` の段(論理座標)で行えば、指と同じ経路をすべて通る。**
- **P5: HW キー。** 電源キーの短押し → `app_key`(任意 export。無い / 0 を返すなら既定動作 = 停止)
  (`wasm_runtime.cpp`)。**短押しはログを出さない**(roadmap U-18)。
- **P6: 画面の文字。** 実機は `draw_text` / `draw_text_rgb` を**座標キーの retained スロット**(LVGL のラベル)に持つ。
  Linux も同様のスロットを持つ(`hostapi_sdl.c`)。**ホストが今表示している文字を読み出せば、Host API を変えずに
  アプリの状態を判定できる。**
- **P7: SD への配置。** `launcher_prepare_sd` は埋め込んだアプリを `/sdcard/apps` へ `seed_file` で置く
  (内容が同じなら書かない)。**消す処理は無い**ので、**埋め込みから外したアプリの `.wasm` は SD に残り、ランチャーに出続ける。**
- **P8: Linux のランチャー**(`hosts/linux/main.c` の `scan_apps`)は、既定で `../../wasm-apps` の**直下の各ディレクトリの
  `.wasm`** を並べる。単発実行(引数に `.wasm`)なら場所を問わない。
- **P9: Linux の UI 操作。** `scripts/ui-linux.sh`(`tap` / `hold` / `drag` / `key` / `shot`)は xdotool。
  **合成クリック(`click --window`)は座標がそのまま届く**が、**ドラッグは実ポインタを動かすので原点の較正が要り、
  ユーザーのマウス操作と干渉しうる**(`docs/lessons.md` Phase 21b)。**SDL のボタンマスクが立たない**問題もある(同)。
- **P10: 既知の差分。** **mp3player で MP3 を実際に再生すると internal が 36〜44 B 減る**(U-23、原因未特定)。
  いまの回帰は再生しないので出ない。**タップで PLAY を押すと出るようになる。**
- **P11: 既定 MUTE。** Click は既定 MUTE(Phase 21c)。CLICK ポートの音は鳴らないが、**呼び出しの成否は判定できる**。

## ゲート(必須)

1. **ステップ 0(設計メモ)の報告 → 承認**を経てから実装に入る。
2. **Host API / ABI は変えない。** 変えるのはホストのデバッグ用の入口(シリアルコンソール、Linux のコマンド入口)と、
   検査用・診断用アプリの置き場所だけ。必要になったら止まって報告する。
3. **metronome / mp3player の `.wasm` は再ビルドしない**(操作はタップで外から与える。アプリのコードは変えない)。
   **highmark が変わらないこと**を、既存アプリへの影響が無い裏づけにする。
4. **conf のスキーマ変更、`scripts/` / `docs/workflow.md` / `CLAUDE.md` の変更は提案 → 承認**
   (設計メモで示し、承認後は本フェーズの作業に含めてよい)。
5. **実機 WAMR プールの余裕が 2KB を切ったら報告して止まる**(検査アプリは Host API を広く使うので、`.wasm` が大きくなりうる)。
6. **一時コードは入れた直後にも `git diff` で確認する。**
7. **削除(touch_demo / seq_smoke など)は、代わりの検査が PASS してから行う。**

## スコープ

### 含む

#### ステップ 0: 設計メモ(実装なし・承認ゲート)

`docs/results/phase22.md` のステップ 0 節に、次をすべて書く。

- **a. カバレッジ表の更新**
  - `scripts/wasm_funcs.py` 等で**全アプリの import を機械抽出**し、Phase 12 の表を**現在の Host API**で作り直す
    (任意 export の `app_key` と、`HOSTAPI_PORT_*` / `OP_*` の種類も行にする)。
  - **どの回帰アプリも触れていない関数・ポート・イベント種別**を列挙する。
- **b. 検査アプリの中身**
  - **名前**(仮名 `hostapi_check`)と置き場所。
  - **検査項目**: seq_smoke の 8 項目(と Phase 17 の追加分)を引き継ぎ、a で見つかった穴を埋める。
    **タップなしで一巡する部分**と、**注入したタップ / キーで確かめる部分**(`poll_event` の DOWN / MOVE / UP と座標、
    `app_key`)に分ける。
  - **所要時間の目標**(seq_smoke の 60 秒より短く。案: 30 秒以内)と、そのために省く / 縮める検査。
  - **結果の出し方**: **画面の文字**(例: `PASS 0x…` / `FAIL <項目>`)を正とし、**文字の読み出し(下記 d)で判定する**。
    seq_smoke の CC#119 / #120 の外部出力を残すかも決める。
  - **音を伴う検査**: Click は既定 MUTE(P11)。MP3(`audio_play` など)を検査に含めるなら U-23 の差分(P10)が出る。
    **含めるか、mp3player のシナリオに任せるか**を決める。
  - **MIDI の受信**(`midi_recv`)をループバックの配線なしでどう検査するか(配線ありのときだけ見る / 受信 0 件を許す)。
- **c. 検査用・診断用アプリの置き場所と、既定のファームから外す方法**
  - 対象: **検査アプリ、midi_loopback、synth_probe**。**touch_demo と seq_smoke は削除**
    (seq_smoke は検査アプリに吸収する)。
  - **推奨: `wasm-apps/dev/<app>/` へ移す。** Linux のランチャーは `wasm-apps` 直下のディレクトリしか見ない(P8)ので、
    それだけで Linux の一覧から外れる。実機は CMake のオプション(仮名 `KYBOTOS_DEV_APPS`、既定 OFF)で
    ON のときだけ `wasm-apps/dev/*` を埋め込む。
  - **回帰に使うファームのビルド方法**(オプション ON のビルドディレクトリをどこにするか。`src/build` か別か)。
    **配布用(既定 OFF)とテスト用(ON)で、アプリの表以外が同じ**であることをどう担保するか。
  - **SD に残った `.wasm` の扱い**(P7)。案: (i) シリアルに `rm <app>` を足す、(ii) 「以前埋め込んでいた名前」の表を持って
    seed のときに消す、(iii) 手で消す手順を README に書く。**ユーザーが自分で置いたアプリや、外のアプリ
    (`KYBOTOS_EXTRA_APPS`)を消さないこと。**
  - README / `wasm-apps/README.md` / CI(`.github/workflows/`)への影響。
- **d. タップの注入と、画面の文字の読み出し(実機)**
  - シリアルコンソールに足すコマンドと応答の形(`KBCMD` 行):
    `tap X Y` / `hold X Y MS` / `drag X Y DX DY MS` / `key`(`app_key` を呼ぶのと同じ経路)/ `texts`(表示中の文字を座標つきで列挙)。
    **座標はアプリの論理座標(320×240)**で、`ui-linux.sh` と同じにする。
  - **注入点は `indev_read_cb`**(P4)。注入中は実タッチより注入を優先する。**押下 → 保持 → 離す**を LVGL の読み取り周期に
    合わせてどう進めるか(タイマ / 状態機械)。**masterui の関所**(上端 3px の保留)を通ること。
  - **`key` の注入**: 電源キーのタスクと同じ入口を使う。あわせて**短押しをログに出す**か(U-18)。
  - **`texts` の出力**: スロットの座標・色・文字。**空文字のスロット**の扱い。**非 ASCII(記号)**の出し方。
  - **Kconfig**: `CONFIG_KYBOTOS_SERIAL_CMD` の下に置く(配布用で切れるように)。
- **e. Linux のタップと文字の読み出し**
  - **推奨: xdotool ではなく、ホスト自身にコマンドの入口を持たせる**(例: 環境変数 `KYBOTOS_CMD_FIFO` で指定した FIFO、
    または stdin)。語彙は d と同じ(`tap` / `hold` / `drag` / `key` / `texts` / `stop`)。**SDL のイベントとしてではなく、
    実機と同じく入力の段で注入する**(P9 のボタンマスクや原点の較正の問題を避け、ユーザーのマウスとも干渉しない)。
  - `stop` を入口に持たせれば、**回帰の終了に xdotool の ESC が要らなくなる**(U-11 の代わりになるかも評価する)。
  - **`ui-linux.sh` をどうするか**(入口を使うように書き換える / 手動の確認用に残す)。
  - **§1-8 の新しい文面**(「注入は実機・Linux ともコマンドの入口から行い、判定は `texts` とログで行う。xdotool のクリックは
    手動の確認とキャプチャ用に限る」など)。
- **f. 回帰スクリプトのシナリオ**
  - conf に**アプリごとの操作シナリオ**を書く形(例: `SCENARIO[metronome]="wait 1; tap X Y; expect STOP; wait 3; tap X Y; expect START"`)。
    **シナリオの無いアプリは従来どおり「起動 → 保持 → 停止」**(外の conf が壊れない。P1)。
  - **`expect`** は `texts` の結果に対する部分一致(座標の指定も可にするか)。失敗時にどの手順で落ちたかを report.md に出す。
  - **実機と Linux で同じシナリオを使う**(座標は論理座標で共通)。
  - 各アプリのシナリオ案:
    - **metronome**: START → 数秒 → STOP、BPM ± のタップで表示が変わること。
    - **mp3player**: 一覧の表示 → PLAY → 状態表示 → STOP。**U-23 の差分(P10)を `EXPECT_DELTA` にどう書くか**。
    - **検査アプリ**: 自己検査の完了を `expect PASS` で待つ + タップ / キーの検査。
  - **反復回数**(`REPEAT_RUNS`)を 3 本それぞれどうするか。
  - U-18(`stop idle` を「保持中に停止した」として出す)を**ついでに入れるか**。
- **g. 削除と移動の一覧**
  - 削除: `wasm-apps/touch_demo/`、`wasm-apps/seq_smoke/`(吸収後)。
  - 移動: midi_loopback / synth_probe → `wasm-apps/dev/`(c の方式による)。
  - 参照の掃除: `CMakeLists.txt`、`device-regress.conf`、`scripts/midi-clock-probe.sh` など
    (`grep -rn "touch_demo\|seq_smoke\|midi_loopback\|synth_probe"` で洗う。**`docs/results/` と `docs/prompts/` の記録は書き換えない**)。
- **h. 検証計画**(下記ステップ 5)と、**統合前の所要時間の測り方**。

#### ステップ 1: 実機のタップ注入と `texts`

d のとおり実装し、**metronome を手で `run` → `tap` → `texts` で START / STOP の切り替えが読める**ことを確かめる。
あわせて **masterui**(上端からの `drag` でマスター設定が開く)と **`key`**(アプリが止まる / `app_key` が呼ばれる)も確かめる。

#### ステップ 2: Linux のコマンドの入口

e のとおり実装し、ステップ 1 と**同じ手順が同じ結果になる**ことを確かめる。

#### ステップ 3: 検査アプリ

b のとおり作る。**実機と Linux の両方で PASS**し、**わざと 1 項目を壊した一時ビルドで FAIL が出る**ことも確かめる(一時コードは戻す)。
highmark と実機のプール消費(`scripts/device-pool.sh`)を記録する。

#### ステップ 4: 置き場所の変更と、回帰スクリプトのシナリオ

c・f・g のとおり。**touch_demo / seq_smoke の削除は、ステップ 3 の検査アプリが両ホストで PASS してから。**
外の conf の形(`APPS="$APPS <app>"`)で**シナリオの無いアプリを 1 本足して回し、従来どおり動く**ことを確かめる
(この repo のアプリで代用してよい)。

#### ステップ 5: 回帰と所要時間

- **統合前**(ステップ 1 より前のコミット)と**統合後**の回帰を、実機・Linux それぞれ同じ条件で回し、所要時間を比べる。
- **metronome / mp3player の highmark が統合前と同じ**であること(ゲート 3)。
- **既定 OFF のファームでランチャーに metronome / mp3player(と外のアプリ)だけが出る**ことを、実機の静止画で確かめる
  (SD に残った古い `.wasm` の扱いは c の方式で)。
- **音と画面の最終確認は人間**: シナリオで metronome を鳴らしているあいだの音、mp3player の再生音を
  カメラ録画(§3.3)で残す(回帰の合否には入れない)。

#### ステップ 6: 文書

`docs/workflow.md`(§1-8、§3.3、§3.4、§3.7、必要なら新しい節)、`CLAUDE.md`(回帰対象の記述)、
`README.md` / `README.ja.md` / `wasm-apps/README.md` / `hosts/linux/README.md`、`docs/lessons.md`、`docs/roadmap.md`
(U-11 / U-18 / U-23 / U-25 の扱い)。フェーズの締めで workflow.md / lessons.md への反映の要否を確認する。

### 含まない

- **metronome / mp3player のコードの変更**(操作規約の統一は U-26)。
- **Host API の追加・変更**。
- **画面の画像比較**(判定は文字とログで行う。静止画は目視の記録用)。
- **音の自動判定を回帰に入れること**(§3.8 の WAV 判定は従来どおり個別に使う)。
- **U-23 の原因の切り分け**(差分を許容値として明記するところまで)。

## 完了条件

1. 回帰の対象が **metronome / mp3player / 検査アプリの 3 本**になり、**実機・Linux とも 1 コマンドで、タップを含めて人手なしで PASS する**。
2. **検査アプリの合否が回帰スクリプトで機械的に判定される**(画面の目視に頼らない)。わざと壊すと FAIL になる。
3. **既定のファームのランチャーには実用アプリだけが出る**(検査用・診断用はオプションを付けたビルドだけ)。
4. **Host API / ABI は不変**、**metronome / mp3player の highmark は不変**。
5. **回帰の所要時間**を統合前後で実測し、記録している。
6. **この repo の外のアプリを `--conf` で足す仕組みが従来どおり動く。**
7. workflow.md §1-8 を含む文書が新しいやり方に更新されている。

## 追記の置き場所

スコープを変えるときは、本文を書き換えず、この下に「追記 (日付)」節を足す。
