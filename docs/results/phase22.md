# Phase 22 実施記録: 回帰アプリの統合と、タップの自動化

指示書: `docs/prompts/phase22.md`

## ステップ 0: 設計メモ(2026-09-27)

**2026-09-27 に承認された。** 末尾の「決めてほしいこと」への回答は「ユーザーの回答」節。**c の SD の扱いは推奨案ではなく `rm` コマンド**になったので、以降の節はその回答で読み替える。

### 調べて分かったこと(指示書の前提に加えて)

- **Linux の回帰では seq_smoke の自己検査が最後まで走っていない。** `linux-regress.sh` はどのアプリも 5 秒で ESC を送るが、
  seq_smoke の一巡は約 60 秒かかる。**Linux で seq_smoke の合否を見たことは、手で回したとき以外に無い。**
- **この clone の Linux には MP3 が無い。** `hosts/linux/sdcard/` は `.gitignore` の対象で、`./sdcard/music` が無い
  (repo を分けたあとに clone したため)。mp3player は「no mp3 files in music dir」になる。Linux で PLAY のシナリオを回すには、
  回帰の前に MP3 を置く必要がある(実機はファームが `test.mp3` などを SD に置く)。
- **HW キーの注入口はすでにある。** 実機は `wasmrt::app_request_key_back()` / `app_request_force_home()`(電源キーのタスクが呼ぶ)、
  Linux は main ループの `key_back_req` フラグ。**どちらも `app: key back -> handled|stop` のログをすでに出している**
  (U-18 の「短押しがログを出さない」は、アプリの実行中については解消済み)。
- **画面の文字はどちらのホストも保持している。** 実機は `s_texts[80]`(LVGL のラベル + 座標 + 色、`hostapi.cpp`)、
  Linux は `s_texts[80]`(文字列 + 座標 + 色、`hostapi_sdl.c`)。**読み出すだけで判定に使える。**
- **Linux のタッチは `host_sdl_push_touch` / `host_sdl_push_touch_move` に集まる**(マウスのイベントもここを通り、
  masterui の関所もここにある)。**ここへ注入すれば、マウスと同じ経路を通る。**
- **`audio_set_volume` には読み出しの関数が無い。** 呼ぶと装置のマスター音量が変わり、元の値に戻せない(Phase 21b の契約)。

### a. カバレッジ表(2026-09-27 時点、`.wasm` の import / export から機械抽出)

Host API は 30 関数 + 任意 export 2 つ。抽出は `.wasm` の import / export セクションを直接読むスクリプトで行った
(**ステップ 4 で `scripts/wasm_imports.py` としてコミットする**。workflow §2.3)。

| 関数 | touch_demo | mp3player | metronome | midi_loopback | seq_smoke | synth_probe | 回帰 5 本で |
|---|---|---|---|---|---|---|---|
| `draw_text` | X | X | X | X | X | X |  |
| `draw_text_rgb` | . | . | . | . | . | . | **未検査** |
| `fill_rect` | X | X | X | X | X | X |  |
| `poll_event` | X | X | X | X | X | . | (呼ぶだけ。タップが無いので中身は空) |
| `audio_play` | . | X | . | . | . | X | (呼ばれない。PLAY を押さないため) |
| `audio_ctrl` | . | X | . | . | . | X | (同上) |
| `audio_set_volume` | . | . | . | . | . | . | **未検査** |
| `audio_get_state` | . | X | . | . | . | . |  |
| `fs_list` | . | X | . | . | . | . |  |
| `fs_read` | . | . | . | . | . | . | **未検査** |
| `fs_write` | . | . | . | . | . | . | **未検査** |
| `play_click` | X | . | . | . | . | . | (呼ばれない。タップしないため) |
| `now_ms` | . | . | X | . | . | . |  |
| `tone_define` | . | . | . | . | X | . |  |
| `tone_play` | . | . | . | . | . | . | **未検査** |
| `midi_send` | . | . | . | . | X | . |  |
| `midi_recv` | . | . | . | X | X | . |  |
| `transport_start` / `stop` | . | . | X | X | X | X | (metronome は START を押さないので呼ばれない) |
| `transport_continue` | . | . | . | . | X | . |  |
| `transport_locate` / `get_position` | . | . | X | . | X | X |  |
| `tempomap_set_tempo` / `set_meter` | . | . | X | X | X | X |  |
| `tempomap_set_loop` / `clear` | . | . | . | . | X | (clear のみ) |  |
| `seq_write` / `seq_filled_until` | . | . | X | . | X | X |  |
| `seq_flush_after` / `time_us_to_tick` | . | . | . | . | X | . |  |
| `app_key`(export) | . | . | . | . | . | . | **未検査** |
| `app_exit`(export) | . | . | . | . | . | X | **未検査** |

**import していても、タップが無いので実際には呼ばれていないもの**が多い(括弧書き)。現状の回帰で中身まで確かめているのは、
実質 **seq_smoke の音楽時間軸 API**(しかも実機だけ)と、全アプリの**起動・停止と heap** である。

ポート / イベント種別: `HOSTAPI_PORT_SYNTH` は metronome(START を押したときだけ)、`PORT_CLICK` / `OP_TONE` / `OP_STOP` / `PORT_DIN_OUT` は seq_smoke。
**`HOSTAPI_EV_TOUCH_MOVE` はどの回帰でも発生しない。**

### b. 検査アプリ `hostapi_check`

- **名前は `hostapi_check`**、置き場所は `wasm-apps/dev/hostapi_check/`(c)。**依存 0・no_std**(seq_smoke と同じ。appui は使わない)。
- **2 部に分ける。**

**第 1 部: タップなしで一巡する(起動と同時に走る)**

| # | 検査 | 中身 |
|---|---|---|
| 1 | 音楽時間軸 | **seq_smoke の 12 項目をそのまま移す**(tmp / lop / loc / stp / u2s / con / u2t / flu / clr / exh / rst / sta) |
| 2 | fs | `fs_write` → `fs_read` で一致、**切り詰め読み**(buf_len < 大きさ)、**不正なパス**(`.x` / `a/b` / `..` / 64 文字)と無いファイルで -1。名前は `hostapi_check.dat`(データルートは sequencer の保存先と共有なので、他と重ならない名前にする) |
| 3 | 描画 | `draw_text_rgb` で色付きの行を描き、**同じ座標に描き直してスロットが増えない**こと(判定は `texts` の件数と色。第 2 部のシナリオ側で見る) |
| 4 | トーン | `tone_define` / `tone_play` / `play_click` の戻り値(正常 0、範囲外のスロット -1)。**Click は既定 MUTE なので無音** |
| 5 | audio の異常系 | 無いファイルの `audio_play` → -1 と `ERROR`、停止中の `PAUSE` → -1。**MP3 は鳴らさない**(下記) |
| 6 | MIDI | `midi_send` が 0、`midi_recv` が 0 以上(ループバックの配線は要求しない) |
| 7 | 時刻 | `now_ms` が単調増加 |

**第 2 部: 注入したタップ / キーで確かめる(シナリオが操作する)**

| # | 検査 | 中身 |
|---|---|---|
| 8 | タッチ | 画面に的を出し、シナリオが `tap` → **DOWN / UP が 1 回ずつ、座標が一致**。`drag` → **MOVE が 1 件以上、8px 以上の間隔**。最後に触った座標を表示する(**touch_demo の座標表示の代わり**) |
| 9 | 戻るキー | `app_key` を export し、`key back` で呼ばれたら表示を変えて **1(処理した)を返す**(アプリは止まらない。止まると回帰の `stop` が空振りするため) |

- **結果の出し方**: 画面の最下行に **`RESULT PASS`** か **`RESULT FAIL <項目名 …>`**。第 1 部の間は `RESULT ...`(走行中)。
  回帰は `texts` でこの行を待つ。**seq_smoke の CC#119 / #120 の送出はやめる**(ループバックの配線をしないと読めず、読んでいる仕組みも無い)。
- **所要時間の目標: 全体で 30 秒以内**(seq_smoke は約 60 秒)。一番長いのは **V1(100 小節でマップ上限 32 の 3 倍を超える検査、約 19 秒)**。
  **小節数は減らさず、テンポを上げて縮める**(検査の意味は「件数が上限を超えても枯渇しない」なので、件数を保てばよい)。
  V1_LOOKAHEAD(`app_tick` 100ms より先まで予約する量)も合わせて見直す。stage 0〜6 の待ち(小節数)も詰める。
- **含めないもの**:
  - **`audio_set_volume`**: 読み出しの関数が無く、呼ぶと装置のマスター音量が戻せない。**ミキサーの UI(masterui)経由の同じ経路が
    毎回の手動確認で動いている**ので、回帰には入れない(カバレッジ表に「意図して外した」と書く)。
  - **MP3 の再生**: mp3player のシナリオに任せる(U-23 の差分を 1 か所に閉じ込めるため)。
  - **`app_exit`**: 呼ばれたことをアプリ自身が画面に出せない(停止の途中で呼ばれる)。ホストのログに出るかをステップ 3 で見て、出るならログで確かめる。
  - **SYNTH ポート**: metronome のシナリオ(START)で鳴る。

### c. 置き場所と、既定のファームから外す方法

- **`wasm-apps/dev/<app>/` へ移す**: `hostapi_check`(新規)、`midi_loopback`、`synth_probe`。
  **touch_demo と seq_smoke は削除**(seq_smoke は hostapi_check が両ホストで PASS してから)。
  - Linux のランチャー(`scan_apps`)は `wasm-apps` の直下の各ディレクトリの `.wasm` だけを見るので、**移すだけで一覧から消える**。
  - `.gitignore` に `wasm-apps/dev/*/target/` を足す。
- **実機: CMake のキャッシュ変数 `KYBOTOS_DEV_APPS`(BOOL、既定 OFF)**。ON のときだけ `wasm-apps/dev/*` の 3 本を埋め込み表に足す。
  `KYBOTOS_EXTRA_APPS` と同じ仕組み(`CMakeLists.txt` の `_apps`)に載せる。
- **回帰に使うファームのビルド**: **開発者の `src/build` を一度 `-DKYBOTOS_DEV_APPS=ON` で構成する**(キャッシュに残る)。
  CI と README の手順は OFF のまま(= 配布するファーム)。
  - 差分は**埋め込み表とフラッシュ上の `.wasm` だけ**(`.wasm` は rodata。RAM の基準値には効かない。ステップ 5 で OFF / ON の停止時 heap を比べて確かめる)。
  - **`device-regress.sh` は最初の `ls` で conf の `APPS` がすべて SD にあるかを確かめ、無ければ「DEV_APPS が OFF のファーム」と分かる
    メッセージで止める**(いまは `run err` で 1 本ずつ落ちる)。
  - CI に **ON のビルドを 1 本足す**(dev の `.wasm` が見つからない等の構成ミスを拾う)。
- **SD に残った `.wasm`**(P7)。**推奨: 「ファームが置いたアプリの一覧」を SD に持つ。**
  - ファームは seed のたびに `/sdcard/apps/.seeded`(名前の一覧)を書く。**一覧にあって、今のファームの表に無い名前は消す。**
    ユーザーが自分で置いたファイルは一覧に入らないので消えない。
  - 一覧をまだ書いていない古いファームが置いたもの(`touch_demo` / `seq_smoke` / `midi_loopback` / `synth_probe`、
    それに Phase 12〜14 で消した `hello` / `demo` / `bars` / `bench` / `clicktest`)は、**一度だけ「既知の旧アプリ」として扱って消す**。
  - **注意**: `KYBOTOS_EXTRA_APPS` で入れた外のアプリも一覧に入るので、**外のアプリを入れないファームを焼くと SD から消える**
    (次に外のアプリ入りのファームを焼けばまた置かれる。アプリのデータ(`/sdcard/data`)は消さない)。ファームと SD の中身が
    常に一致する、という挙動になる。
  - 代案: (i) シリアルに `rm <app>` を足して手で消す、(iii) 手で消す手順を README に書くだけ。どちらもユーザーの手が要る。

### d. 実機のタップ注入と、画面の文字の読み出し

**注入**(`touch` コンポーネントに注入の状態を持たせる)

- `touch.hpp` に `touch::inject_press(x, y)` / `inject_move(x, y)` / `inject_release()` を足す。**`indev_read_cb` は、注入中なら
  実タッチを読まずに注入した点を返す**(`data->point` = 論理座標、`state` = PRESSED / RELEASED)。
  状態は 1 組の atomic(押下中フラグ + 座標)で、LVGL タスクとシリアルのタスクの間でロックは取らない。
- 押下 → 保持 → 離すの時間は**シリアルのタスクが `vTaskDelay` で刻む**(LVGL の読み取り周期に合わせた状態機械は作らない)。
  `tap` は 80ms 押して離す(LVGL の読み取り周期の数回ぶん)。**コマンドは 1 つずつ終わってから応答する**ので、シナリオは順に並べるだけでよい。
- **経路**: `indev_read_cb` → LVGL → `screen_input_event_cb` → **masterui の関所** → `push_event` → アプリ。**指と同じ**。
  メニュー画面ではランチャーのボタンとスクリーンセーバーにも効く(無操作時間も戻る)。
- **確かめること(ステップ 1)**: `indev_read_cb` が返す座標は `map_basic_to_display` で回転を済ませた値。
  **LVGL が表示の回転をさらに掛けないか**を、`tap` した座標と hostapi_check(または `TOUCH_LOG_TAP`)の座標の一致で確かめる。

**シリアルのコマンド**(応答は `KBCMD` 行。座標はアプリの論理座標 320×240)

| コマンド | 動作 | 応答 |
|---|---|---|
| `tap X Y` | 80ms 押して離す | `tap done` |
| `hold X Y MS` | MS 押して離す | `hold done` |
| `drag X Y DX DY MS` | 押す → MS 待つ → 8 段で (DX, DY) 動かす(1 段 40ms)→ 離す(`ui-linux.sh drag` と同じ形) | `drag done` |
| `key back` / `key home` | 電源キーの短押し / 長押しと同じ(`app_request_key_back` / `app_request_force_home`) | `key ok` / `key idle`(アプリが無い) |
| `texts` | アプリの画面の文字を 1 行 1 スロットで出す | `text X Y RRGGBB <文字>` × N、最後に `texts done N`(アプリが無ければ `texts idle`) |

- `texts` は LVGL のロックを取って `s_texts` を読む(`lv_label_get_text`)。**非 ASCII のバイト(記号の私用領域など)は `\xNN` で出す**
  (ログを ASCII に保つ)。空文字のスロットも出す(スロットの消費が見えるように)。
- **`CONFIG_KYBOTOS_SERIAL_CMD` の下に置く**(既存のコンソールと一緒に切れる)。
- **`kLineMax` 96 は足りる**(最長の `drag` で 30 文字程度)。

### e. Linux のタップと文字の読み出し

- **ホストにコマンドの入口を持たせる**: 環境変数 **`KYBOTOS_CMD_FIFO=<path>`** があれば、その FIFO を非ブロッキングで開き、
  **main ループの 1 周ごとに行を読む**。環境変数が無ければ何もしない(従来どおり)。stdin にしないのは、端末から手で起動したときに干渉しないため。
- **語彙は実機と同じ**(`tap` / `hold` / `drag` / `key back` / `texts` / `ping`)に加えて **`stop`**(ESC と同じ終了)。
  時間のかかる `hold` / `drag` は **main ループの中の小さな状態機械**で進める(ループを止めない。`SDL_GetTicks` で次の段の時刻を持つ)。
- **注入先は `host_sdl_push_touch` / `host_sdl_push_touch_move`**(マウスと同じ。masterui の関所も通る)。SDL のイベントは作らない
  (**xdotool で困った「ボタンマスクが立たない」「原点の較正」「ユーザーのマウスと干渉する」がすべて無くなる**)。
- **応答は stdout に `CMD: ...` の行**で出し、**行ごとに `fflush`**(回帰はログファイルを読むので、バッファに溜まると待ちがずれる)。
- **`linux-regress.sh` は xdotool を使わなくなる**(終了も FIFO の `stop`)。**U-11(`timeout` で止める案)は不要になる**のでクローズする。
- **`ui-linux.sh` は手動の確認と撮影用に残す**(`tap` / `hold` / `drag` / `key` は、FIFO が有効ならそちらへ流すように書き換える。
  `shot` はそのまま xwd)。
- **MP3 の用意**: `linux-regress.sh` が、`hosts/linux/sdcard/music` が無ければ `src/components/wasm_runtime/assets/*.mp3` をコピーする
  (実機のファームが SD に置くのと同じ 3 ファイル)。

**workflow §1-8 の新しい文面(案)**

> 8. **UI の操作は、実機・Linux ともホストのコマンドの入口から注入する**(実機 = シリアルコンソールの `tap` / `hold` / `drag` / `key`、
>    Linux = `KYBOTOS_CMD_FIFO`)。**判定は画面の文字(`texts`)とログで行う**(画像の比較はしない)。
>    xdotool のクリック(`scripts/ui-linux.sh`)は**手動の確認と撮影のときだけ**使い、回帰には使わない
>    (合成クリックはボタンマスクが立たない、ドラッグは原点の較正が要りユーザーのマウスと干渉する。`docs/lessons.md` Phase 21b)。
>    **音と見た目の最終確認は人間**(§3.3 のカメラ + 人間の操作)。
>    Linux ホストの画面キャプチャは `scripts/screen-still.sh` / `screen-rec.sh`(Phase 18。ウィンドウ単位。x11grab は使わない)。

### f. 回帰スクリプトのシナリオ

- **conf に `SCENARIO` を足す**(**スキーマの追加**。無くてもよい連想配列なので、外の conf はそのまま動く):
  ```bash
  declare -A SCENARIO=(
      [metronome]="expect START; tap 281 202; expect STOP; wait 3; tap 125 202; expect BPM: 125; tap 281 202; expect START"
      [mp3player]="expect state: STOPPED; tap 40 220; expect state: PLAYING; wait 3; tap 164 220; expect state: STOPPED"
      [hostapi_check]="expect@40 RESULT; tap 160 120; drag 60 120 80 0 0; key back; expect@5 RESULT PASS"
  )
  ```
  - 手順は `;` 区切り: `wait S` / `tap` / `hold` / `drag` / `key back` / **`expect[@秒] <文字>`**(`texts` を 0.5 秒ごとに取り、
    **どれかのスロットが <文字> を含むまで待つ**。既定 5 秒)。
  - **シナリオのあるアプリは、`HOLD_SEC` の代わりにシナリオを実行**してから `stop` する。**シナリオの無いアプリは従来どおり**。
  - 座標とラベルは**アプリのレイアウトに依存する**ので、conf の該当行にその旨を書く(workflow §2.3)。上の座標は
    `BTN_XS` / `BTN_Y` から計算した値で、**ステップ 4 で実機と Linux の両方で合わせる**。hostapi_check の座標は b で決める。
- **実行部は 1 つのファイル(`scripts/regress-scenario.sh`)に置き、`device-regress.sh` と `linux-regress.sh` の両方が source する。**
  違いは「コマンドを送る関数」と「応答を待つ関数」だけ(実機 = `hpane.sh send esp32-monitor` + monitor.log、Linux = FIFO + アプリのログ)。
- **report.md に「シナリオ」列を足す**(PASS / どの手順で落ちたか)。
- **反復回数**: metronome 3 回、**mp3player 3 回(ただし U-23 の差分に注意。下記)**、hostapi_check 1 回。
- **U-23(MP3 を再生すると internal が 36〜44 B 減る)**: シナリオで PLAY を押すと 1 回ごとの差分に出る。
  **3 回の反復で積み上がるか(= 本当のリーク)か、1 回目だけか**をステップ 4 で測り、それに合わせて
  `EXPECT_DELTA[mp3player]` と反復判定の扱いを決める(積み上がるなら、U-23 の温度感を上げて roadmap に書く)。
- **U-18 はついでに入れる**: `stop` の応答が `stop idle` なら「保持中に停止した」として別の FAIL 理由を出す(スクリプトを触るので、ここで直すのが安い)。
- **Linux の回帰も `HOLD_OVERRIDE` / 反復を conf から読むようにする**(いまは一律 5 秒・1 回)。hostapi_check の第 1 部が走りきるのを待つため。

### g. 削除・移動・参照の掃除

| 対象 | 変更 |
|---|---|
| `wasm-apps/touch_demo/` | 削除 |
| `wasm-apps/seq_smoke/` | 削除(hostapi_check が両ホストで PASS した後) |
| `wasm-apps/midi_loopback/` / `synth_probe/` | `wasm-apps/dev/` へ移す(`git mv`)。**`.wasm` は再ビルドしない**(中身は同じ) |
| `wasm-apps/dev/hostapi_check/` | 新規 |
| `src/components/wasm_runtime/CMakeLists.txt` | `KYBOTOS_BUILTIN_APPS` を metronome / mp3player にし、`KYBOTOS_DEV_APPS` を足す |
| `src/components/wasm_runtime/launcher.cpp` | `.seeded` の一覧と、旧アプリの削除(c) |
| `src/main/serial_cmd.cpp`、`src/components/touch/` | d のコマンドと注入 |
| `hosts/linux/main.c`(+ `hostapi_sdl.c`) | e の FIFO と注入、`texts` |
| `scripts/device-regress.conf` | `APPS="metronome mp3player hostapi_check"`、`APP_WASM[hostapi_check]`、`SCENARIO`、`HOLD_OVERRIDE` / `REPEAT_OVERRIDE` の整理 |
| `scripts/device-regress.sh` / `linux-regress.sh` / `ui-linux.sh`、新規 `regress-scenario.sh` / `wasm_imports.py` | f / e |
| `.gitignore` | `wasm-apps/dev/*/target/` |
| `.github/workflows/build.yml` | ON のファームのビルドを 1 本 |
| 文書 | `CLAUDE.md`(回帰対象の段落)、`docs/workflow.md` §1-8 / §3.3 / §3.4 / §3.7、`README.md` / `README.ja.md`(サンプルアプリの段落)、`wasm-apps/README.md`、`hosts/linux/README.md`(`touch_demo` の起動例)、`docs/lessons.md`、`docs/roadmap.md`(U-11 / U-18 / U-23 / U-25) |

- `docs/architecture.md` / `docs/hostapi.md` の touch_demo / seq_smoke / midi_loopback の記述は**経緯の記録**なので書き換えない
  (現状と食い違う箇所だけ「Phase 22 で …」と注記する)。`docs/results/` / `docs/prompts/` は書き換えない。
- `scripts/midi-clock-probe.sh` は midi_loopback の集計項目に触れているだけで、パスに依存していないことを確認済み(変更なし)。

### h. 検証計画

| ステップ | 確かめること |
|---|---|
| 1 | 実機で metronome を `run` → `tap` START → `texts` に `STOP` → `tap` → `START`。`drag` で上端からマスター設定が開く。`key back` でアプリが止まる(`app: key back -> stop`)。**注入した座標がアプリに同じ値で届く**(回転の確認) |
| 2 | Linux で同じ手順が同じ結果になる。FIFO が無いときは従来どおり動く |
| 3 | hostapi_check が実機・Linux で `RESULT PASS`。**わざと 1 項目を壊した一時ビルドで `RESULT FAIL <その項目>`**(一時コードは戻す)。highmark とプール消費(`device-pool.sh`)を記録 |
| 4 | シナリオつきの回帰が実機・Linux で PASS。**シナリオの無いアプリを外の conf の形で 1 本足しても従来どおり回る**(midi_loopback で代用)。U-23 の積み上がりを測る |
| 5 | **統合前(`3bea232`)と統合後の所要時間**を、実機・Linux それぞれ同じ条件で測る。metronome / mp3player の highmark が不変。**OFF のファームで、ランチャーに metronome / mp3player だけが出る**(古い `.wasm` が SD から消える)を静止画で。音はカメラ録画で人間が確認 |

統合前の所要時間は、ステップ 1 に入る前に **`3bea232` のファームと scripts で `device-regress.sh` / `linux-regress.sh` を 1 回ずつ回して測る**
(スクリプトの開始から終了までの壁時計。ボードのリセットとモニタの起動を含む)。

### 決めてほしいこと

1. **SD に残った `.wasm` の扱い**(c): 推奨は **`.seeded` の一覧で、ファームの表から外れたものを消す**
   (外のアプリも、入れないファームを焼くと消える)。代案は `rm` コマンドか、手で消す手順だけ。
2. **回帰に使うファーム**(c): 推奨は **`src/build` を一度 `KYBOTOS_DEV_APPS=ON` で構成して使う**(CI と README は OFF)。
   代案は、ON を別のビルドディレクトリにして、回帰のたびに `--build-dir` を付ける。
3. **workflow §1-8 の新しい文面**(e の案)でよいか。
4. **U-18 と U-11 をこのフェーズで片付けてよいか**(U-18 は `stop idle` を別の理由で出す、U-11 は FIFO の `stop` で不要になるのでクローズ)。

### ユーザーの回答(2026-09-27)

1. **SD に残った `.wasm` は、シリアルに `rm <app>` を足して手で消す**(`.seeded` の一覧は作らない。ファームは SD のアプリを消さない、という今の挙動を保つ)。
   `rm` は `/sdcard/apps/<app>.wasm` だけを対象にし(`/` や `..` を含む名前は拒否)、**実行中のアプリは消さない**。応答は `rm ok <path>` / `rm err <理由>`。
   これに伴い g の表の `launcher.cpp` の行は無くなり、`serial_cmd.cpp` に `rm` が加わる。README に「古いファームから更新したら `rm` で消す」手順を書く。
2. **回帰に使うファームは推奨どおり**: `src/build` を一度 `-DKYBOTOS_DEV_APPS=ON` で構成して使う。CI と README は OFF。
3. **workflow §1-8 は e の案の文面でよい。**
4. **U-18 と U-11 はこのフェーズで片付ける。**
