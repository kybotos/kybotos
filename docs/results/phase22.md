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

## 統合前の基準(2026-09-27)

ステップ 1 より前のコード(`3bea232` と同じ。間のコミットは文書だけ)でファームをビルド・フラッシュし、回帰を 1 回ずつ回した。
所要時間は**スクリプトの開始から終了までの壁時計**(実機はボードのリセットとモニタの起動を含む)。

| 回帰 | 所要時間 | 結果 | 備考 |
|---|---|---|---|
| 実機 `device-regress.sh` | **147 秒** | PASS(5 本、4 本 × 3 回 + seq_smoke 1 回、差分すべて +0、警告 0) | 停止時 free_int 150,312 / largest_int 102,400 / free_psram 8,136,668 |
| Linux `linux-regress.sh` | **27 秒** | PASS(5 本 × 1 回、各 5 秒保持) | highmark: touch_demo 15,944 / mp3player 19,112 / metronome 22,952 / midi_loopback 29,120 / seq_smoke 28,968 |

**Linux の 27 秒は seq_smoke の自己検査を最後まで待っていない**(5 秒で止める)ので、統合後と同じ条件の比較にはならない。
統合後の Linux は hostapi_check の完了を待つぶん長くなりうる(ステップ 5 で両方を並べて評価する)。

## ステップ 1: 実機のタップ注入と `texts`(2026-09-27)

### 実装

| ファイル | 変更 |
|---|---|
| `src/components/touch/touch.hpp` / `touch.cpp` | `touch_inject::press(x, y)` / `release()`。**`indev_read_cb` の先頭で、注入中なら実タッチを読まずに注入した点を返す**。状態は atomic 3 つ(注入中 / 押下中 / 座標)。release は「LVGL が RELEASED を一度読んだら注入を終える」 |
| `src/components/wasm_runtime/hostapi.hpp` / `hostapi.cpp` | `hostapi_dump_texts(emit)`: LVGL のロック下で文字スロットを列挙(アプリ画面が無ければ -1) |
| `src/main/serial_cmd.cpp` | `tap X Y`(80ms)/ `hold X Y MS` / `drag X Y DX DY MS`(8 段 × 40ms)/ `key back\|home` / `texts` / `rm <app>`。押す時間はコンソールのタスクが `vTaskDelay` で刻み、**終わってから応答する** |
| `src/main/Kconfig.projbuild` | `KYBOTOS_SERIAL_CMD` の help にコマンドを追記 |

### 確認(`captures/phase22-step1/monitor.log`)

- **座標**: metronome の START(`BTN_XS[3]` = 246 + 35, `BTN_Y` = 176 + 26 → `tap 281 202`)で `START` → `STOP ` → `START`、
  BPM+(`tap 125 202`)で `BPM: 120` → `BPM: 125`。**表示の回転はパネル側(`swap_xy` / `mirror_x`)で行っていて LVGL の回転は 0**
  なので、**注入した論理座標がそのままアプリの座標になる**(厳密な一致はステップ 3 の hostapi_check で確かめる)。
- **`texts`**: `text <x> <y> <rrggbb> <文字>` × 8 行 + `texts done 8`。アプリが無いときは `texts idle`。
- **masterui の関所を通る**: `drag 160 0 0 80 0`(上端から下へ 80px)で **Settings の帯が開いた**(カメラの静止画
  `captures/phase22-step1/cam_still_143905.png`)。× を `tap 300 18` で閉じたあと、START のタップがアプリに届いた。
- **`key back`**: `KBCMD: key ok back` → `app: key back -> stop` → 停止、**free_int 差分 +0**。アプリが無いときは `key idle`。
- **`rm`**: `rm ../x` → `rm err bad name`、`rm nosuch` → `rm err no such app ...`、`rm synth_probe` → `rm ok`(`ls` が 7 → 6 本、
  `run synth_probe` → `no such app`)。synth_probe は今のファームに埋め込まれているので次の起動でまた置かれる。
- **警告**: 起動直後の I2C の nack 以外に W / E 行なし。

## ステップ 2: Linux のコマンドの入口(2026-09-27)

### 実装

| ファイル | 変更 |
|---|---|
| `hosts/linux/cmd_fifo.h` / `cmd_fifo.c`(新規) | `KYBOTOS_CMD_FIFO=<path>` があれば FIFO を `O_NONBLOCK` で開き、main ループの 1 周ごとに行を読む。語彙は実機と同じ(`ping` / `tap` / `hold` / `drag` / `key back` / `texts`)+ **`stop`**。応答は stdout の `CMD: ...`。**入口を使うときは stdout を行バッファにする**(`app started` などもすぐログファイルに出る)。`tap` / `hold` / `drag` は**段の予定表**(時刻・種類・座標)を積み、ループの 1 周ごとに期限の来た段を実行する(ループを止めない)。終わるまで次の行は読まない |
| `hosts/linux/hostapi_sdl.h` / `hostapi_sdl.c` | `host_sdl_dump_texts(emit)` |
| `hosts/linux/main.c` | 起動時に `cmd_fifo_open()`、ループで `cmd_fifo_poll()`(`KEY_BACK` → `key_back_req`、`STOP` → ESC と同じ停止)、終了時に `cmd_fifo_close()` |
| `hosts/linux/CMakeLists.txt` | `cmd_fifo.c` を追加 |

- **注入先は `host_sdl_push_touch` / `host_sdl_push_touch_move`**(マウスと同じ入口。masterui の関所も通る)。SDL のイベントは作らない。
- **Linux のループは 100ms 周期**(`APP_TICK_MS`)なので、段の時刻の分解能も 100ms になる(80ms の `tap` は次の周で離す)。
  アプリはキューで受けるので、DOWN と UP が同じ `app_tick` で届いても判定は変わらない。
- **メニュー画面(アプリが動いていない)では、注入系は `idle` を返す**(Linux の回帰は単発実行なので要らない。実機はメニューにも効く)。

### 確認(`captures/phase22-step2/`)

ステップ 1 と同じ手順を FIFO から流し、**実機と同じ結果**になった:
`texts` の 8 行が実機と**1 文字も違わず一致**、`tap 281 202` で `START` ↔ `STOP`、`tap 125 202` で `BPM: 125`、
`drag 160 0 0 80 0` で **Settings が開いた**(`screen_still_144158.png`)、× で閉じて START のタップがアプリに届く、
`key back` → `key back -> stop` → `app stopped`、`stop` → `app stopped`、プロセスの残留なし、警告 0。
**FIFO を指定しなければ従来どおり**(`cmd fifo` の行も `CMD:` の行も出ない)。

## ステップ 3: 検査アプリ `hostapi_check`(2026-09-27)

### 実装

| ファイル | 変更 |
|---|---|
| `wasm-apps/dev/hostapi_check/`(新規) | 検査アプリ(依存 0・no_std、8,175 B)。第 1 部 18 項目 + 第 2 部 3 項目、合否は最下行の `RESULT PASS` / `RESULT WAIT <残り>` / `RESULT FAIL <項目>`。検査の一覧と意図して外したものはソース冒頭のコメント |
| `src/components/wasm_runtime/CMakeLists.txt` | `option(KYBOTOS_DEV_APPS … OFF)`。ON のときだけ `wasm-apps/dev/` のアプリを埋め込む(この時点では hostapi_check だけ。移動はステップ 4) |
| `src/components/wasm_runtime/hostapi.cpp` / `hosts/linux/hostapi_sdl.c` | **拒否したパスをログに出す**(`fs_read: rejected path '<名前>' (<長さ> bytes)`。印字できない文字は `?`、64 文字で切る)。回帰の許容パターンを検査アプリの名前(`hcheck_`)だけに当てるため。Host API の挙動は変えていない |
| `.gitignore` | `wasm-apps/dev/*/target/` |

`src/build` は決定 2 のとおり **`-DKYBOTOS_DEV_APPS=ON` で構成した**(`CMakeCache.txt` に `KYBOTOS_DEV_APPS:BOOL=ON`)。

### 時間を縮めた方法と、そこで踏んだこと

seq_smoke の 12 項目は、**検査の中身(小節の数え方・件数)を変えずにテンポだけを上げた**: 基本 120 → 240bpm、速いテンポ 180 → 360bpm、
V1(100 小節)は 150,000 / 160,000µs → **75,000 / 80,000µs**(約 800bpm)、最初の待ちを 2 → 1 小節、OP_STOP を 3 → 2 小節目。
**第 1 部は Linux で約 8 秒**(seq_smoke は約 60 秒)。

- **800bpm では V1 の先読みの予約が間に合わなかった。** 最初の `app_tick`(開始の 100ms 後)の時点で song はもう 1,280 tick 進んでいて、
  小節 1 の頭(960)を過ぎている。**過去の tick にはテンポを書けない(-1)**ので小節 1 が前のテンポのまま進み、照合 83 回がすべて不一致になった
  (診断行 `v1 n83 miss83 at1`、一時の表示で `upq 75000 at off 343` を確認)。**V1 を始める前(STOPPED 中)に先読みの範囲まで予約する**ように直した。
  seq_smoke は 150,000µs で 100ms = 640 tick < 960 だったので踏んでいなかった(**元の seq_smoke は Linux でも 12 項目合格**を確認済み。
  これまでの Linux の回帰は 5 秒で止めていたので、一度も最後まで見ていなかった)。
- 診断行 `v1 n<照合回数> miss<不一致> at<最初に外れた小節> rx<MIDI 受信バイト>` は、失敗したときの手がかりとして残した。

### 確認

| | Linux | 実機 |
|---|---|---|
| 第 1 部 | 18 項目合格、約 8 秒 | 18 項目合格(V1 の照合 84 回・不一致 0) |
| 第 2 部 | `tap 265 165` → tch、`drag 20 205 120 0 0` → mov(MOVE 4 件)、`key back` → key(`key back -> handled`) | 同じ(**タップの座標が完全に一致** = 注入した論理座標がそのままアプリに届く。ステップ 1 の宿題) |
| 結果 | `RESULT PASS` | `RESULT PASS` |
| わざと壊す | `tone_play(8)` の期待値を反転した一時ビルドで **`RESULT FAIL ton`** | — |
| WAMR プール | highmark 33,976(Linux、256KB) | **highmark 24,752 / total 180,032**(起動後に最初に読んだアプリなので値は正しい) |
| 色付きの行 | `text 12 166 40c0ff rgb 40c0ff` | 同じ |

### 見つけたこと: 起動後に初めて書き込み用にファイルを開くと、internal が 176 B 減る(一度だけ)

実機で hostapi_check の 1 回目だけ **free_int が −176 B**、2 回目以降は +0 だった。**ボードをリセットすると再現**する(データファイルが SD に残っていても)。

- 検査を外した一時ビルドで二分探索: トーンと audio を外しても −176、**fs を外すと +0**、**既存ファイルの読み込みだけなら +0**。
- ファームの一時コード(SD の準備の直後に、`fopen("wb")` / `fwrite` / `fclose` / `remove` / `rename` の前後の free_int を 2 周ぶん出す):
  ```
  round 0: fopen -264 fwrite -132 fclose 220 remove 0 rename 0 (total -176)
  round 1: fopen  -88 fwrite -132 fclose 220 remove 0 rename 0 (total 0)
  ```
  **起動後 1 回目の書き込み用の `fopen` だけが 176 B 多く取り、返さない。** 2 回目からは同じ操作で 0。**リークではなく一度きりの確保**
  (何が取っているか — newlib / VFS / FATFS のどれか — は未特定)。起動時にこの一時コードが先に書いたビルドでは、hostapi_check は +0 になった。
- **回帰への影響**: 実機の回帰はモニタの起動でボードをリセットするので、**起動後に最初に `fs_write` を呼ぶアプリの 1 回目**に必ず −176 が出る。
  metronome / mp3player は書かないので、**回帰では hostapi_check の 1 回目に決まって出る**。扱いはステップ 4 で決める。
- 一時コードは撤去し、`git diff` で残っていないことを確認した。roadmap に課題として足す(U-30)。

### 許容パターンに足すもの(ステップ 4 で conf に入れる)

実機のログ(`captures/phase22-step3/monitor-1.log`)に出た、検査アプリが意図して起こす W / E 行:

```
W WASM/API: fs_read: rejected path '.hcheck_dot' (11 bytes)        … fs_read / fs_write × 不正な名前 4 つ
E AUDIO/MP3: Failed to open MP3 file: /sdcard/music/hcheck_missing.mp3
W WASM/API: audio_play: failed: /sdcard/music/hcheck_missing.mp3
```

## ステップ 4: 置き場所の変更と、回帰のシナリオ(2026-09-27、途中)

### 実装

| 対象 | 変更 |
|---|---|
| `wasm-apps/touch_demo/`、`wasm-apps/seq_smoke/` | 削除(seq_smoke は hostapi_check が両ホストで PASS した後。ゲート 7) |
| `wasm-apps/midi_loopback/`、`synth_probe/` | `wasm-apps/dev/` へ `git mv`(`.wasm` は再ビルドしていない) |
| `src/components/wasm_runtime/CMakeLists.txt` | 既定の表 = mp3player / metronome、`KYBOTOS_DEV_APP_LIST` = hostapi_check / midi_loopback / synth_probe |
| `scripts/regress-scenario.sh`(新規) | シナリオの実行部。`wait` / `tap` / `hold` / `drag` / `key back` / `expect[@秒] <文字列>`。応答のログと接頭辞と送る関数を呼ぶ側が決める |
| `scripts/device-regress.conf` | `APPS="metronome mp3player hostapi_check"`、`APP_WASM[hostapi_check]`、**`SCENARIO`(スキーマの追加。ステップ 0 f で承認済み)**、`REPEAT_OVERRIDE[hostapi_check]=1`、`EXPECT_DELTA[hostapi_check]=-176`、許容パターン 3 つ(`hcheck_` に限る)、`HOLD_OVERRIDE` の seq_smoke を削除 |
| `scripts/device-regress.sh` | シナリオの実行と「シナリオ」列、**最初の `ls` で APPS が SD にあるか確かめる**(無ければ DEV_APPS の案内を出して止まる)、**U-18**: `stop` の応答が `stop idle` なら「保持中に停止した」 |
| `scripts/linux-regress.sh` | **FIFO 方式に書き直し**(xdotool を使わない。終了も `stop`)、シナリオ、`HOLD_OVERRIDE` / `HOLD_SEC`、MP3 が無ければ assets からコピー、アプリごとの所要秒 |
| `scripts/ui-linux.sh` | `KYBOTOS_CMD_FIFO` が FIFO なら tap / hold / drag / key をそちらへ(Escape → `stop`、BackSpace → `key back`)。無ければ従来どおり xdotool |
| `scripts/wasm_imports.py`(新規) | アプリ × Host API のカバレッジ表(ステップ 0 a の表を出したもの) |
| `hosts/linux/main.c` | **`cmd_fifo_open()` を main の先頭へ**(下記) |
| `.github/workflows/build.yml` | `-B build-dev -DKYBOTOS_DEV_APPS=ON` のビルドを 1 本追加。`.gitignore` に `src/build-dev/` |

### 途中で直したこと

- **`ls` の判定**: モニタのログの行末は `\r` なので、`wasm$` では一致しなかった(3 本とも「SD に無い」と誤判定)。`[[:space:]]*$` にした。
- **Linux で `app started` が 15 秒見えなかった**: `setvbuf`(行バッファ)を `cmd_fifo_open()` の中で呼んでいたが、それが **SDL の初期化などで stdout に書いた後**だったので効いておらず、
  `app started` は次の応答の `fflush` まで出ていなかった(回帰は待ちの上限 15 秒で先へ進んでいた)。**stdout に何か書く前(main の先頭)で開く**ようにした。
  1 本あたり 19 秒 → 4 秒。ステップ 3 で「第 1 部は Linux で約 8 秒」と書いたのはこの 15 秒の後から数えていたためで、**実際は約 18 秒**(実機とほぼ同じ)。

### 確認

**Linux(`captures/phase22-step4-linux/`)**: 3 本とも PASS、**29 秒**(metronome 4 秒 / mp3player 4 秒 / hostapi_check 21 秒)。
**highmark は metronome 22,952 / mp3player 19,112 で統合前と同じ**(ゲート 3)。hostapi_check 34,728。

**外の conf の形**(`. device-regress.conf` → `APPS="$APPS midi_loopback"` + `APP_WASM`、シナリオ無し)で midi_loopback を足しても従来どおり回った
(`scenario=-`、7 秒、highmark 29,120 は統合前と同じ)。

**`ui-linux.sh` の FIFO 経路**: `tap 281 202` → STOP、`hold 125 202 700` → BPM 130(長押しの連打が効く)、`key Escape` → `stop ok`。

**実機(`captures/phase22-step4-device/report.md`)**: **53 秒**(統合前 147 秒)。シナリオはすべて合格、許容外の警告 0 件。**判定は FAIL**:

| アプリ | int 差分 | largest_int | シナリオ | 判定 |
|---|---|---|---|---|
| metronome #1〜#3 | +0 | 98,304 | PASS(8 手順) | PASS |
| **mp3player #1** | **−36** | 98,304 | PASS(6 手順) | **FAIL(期待 0)** |
| mp3player #2 / #3 | +0 | 98,304 | PASS | PASS(反復 3 回の終了値も一致) |
| hostapi_check | −176(期待どおり) | 98,304 | PASS(6 手順) | PASS |

- **U-23 の −36 B は、積み上がるリークではなく「起動後に初めて MP3 を再生したときの一度きりの確保」だった**(2・3 回目は +0、終了値も 3 回一致)。
  hostapi_check の −176 B(起動後に初めて書き込み用にファイルを開いたとき)と同じ種類。今の conf の書式(`EXPECT_DELTA` はすべての回に同じ値を要求)では
  「1 回目だけ減ってよい」を表せない。→ **ユーザー判断待ち**。
- **`largest_int` が統合前の 102,400 から 98,304 に下がり、`MIN_LARGEST_INT`(98,304)ちょうど**。free_int は 150,312 → 150,304(−8 B)しか変わっていないので、
  **静的領域のわずかな増減でブロックの境界がずれた**(U-29 の版の違いのときと同じ種類)と見ている。判定は `≥` なので通る。→ **ユーザーに報告**。

### ユーザーの判断(2026-09-27)と、その後

- **一度きりの確保は、conf の書式を変えずに固定値で扱う**: `EXPECT_DELTA[mp3player]=-36` / `[hostapi_check]=-176`、
  どちらも `REPEAT_OVERRIDE=1`(2 回目以降は +0 になり、同じ期待値を全回に当てられないため)。U-23 の記録には 36〜44 B と揺れた値があるので、
  −36 以外で落ちたら、まず値が揺れたのか漏れが増えたのかを見る(conf のコメントに書いた)。
- **`MIN_LARGEST_INT` は据え置き**(98,304 ちょうどで `≥` なので通る)。

**実機(`captures/phase22-step4-device/report.md`)**: **PASS、45 秒**。metronome × 3 +0(反復一致)、mp3player −36、hostapi_check −176、シナリオすべて合格、許容外の警告 0 件。

**外の conf の形で実機**(`captures/phase22-extconf-device/`): 上の 3 本 + midi_loopback × 3(シナリオ無し、+0、反復一致)で **PASS**。
1 回目は midi_loopback の 3 回目で `free_psram` / `largest_psram` が空になり「反復で free_psram が変化」で落ちた。ログの行は完全だったので、
**tee が書き途中の行にスクリプトが一致した**(`app: stopped free_int=` まで書かれた時点)。以前からあり得た競合で、**行末の `]` まで待つ**ように直した。

## ステップ 5: 回帰と所要時間(2026-09-27)

### 所要時間(スクリプトの開始から終了までの壁時計)

| 回帰 | 統合前(`3bea232`) | 統合後 | 備考 |
|---|---|---|---|
| **実機** `device-regress.sh` | **147 秒**(5 本、4 本 × 3 回 + seq_smoke 60 秒) | **45 秒**(metronome × 3 + mp3player × 1 + hostapi_check × 1) | 約 3 分の 1。**しかも今回はタップと表示の確認を含む** |
| **Linux** `linux-regress.sh` | 27 秒(5 本 × 5 秒保持) | **29 秒**(metronome 4 秒 / mp3player 4 秒 / hostapi_check 21 秒) | 統合前は seq_smoke の自己検査を最後まで待っていなかった(5 秒で ESC)ので、同じ条件の比較ではない |

### 既存アプリへの影響が無いこと(ゲート 3 / 完了条件 4)

- **metronome / mp3player の `.wasm` は変えていない**(git の差分なし)。**Linux の highmark は metronome 22,952 / mp3player 19,112 で統合前と同じ**。
  midi_loopback(`wasm-apps/dev/` へ移しただけ)も 29,120 で同じ。
- 実機の停止時の値: free_int 150,312 → **150,304(−8 B)**、largest_int 102,400 → **98,304**(`MIN_LARGEST_INT` ちょうど。ユーザー判断で据え置き。
  ステップ 1 のファームから同じ値なので、シリアルコンソールと注入の追加による静的領域のわずかな増減でブロックの境界がずれたと見ている)。
- **既定(OFF)のファームと ON のファームで、停止時の値は同じ**(OFF で metronome を起動・停止: free_int 150,304 / largest_int 98,304)。
  埋め込む `.wasm` はフラッシュ上の rodata なので RAM に効かない、という設計メモ c の見立てどおり。

### 既定のファームのランチャー(完了条件 3)

`src/build`(ON)を崩さないよう、`captures/phase22-off-build` に OFF でビルドしてフラッシュした。起動直後の `ls` は 8 本
(古いファームが置いた seq_smoke / touch_demo / midi_loopback / synth_probe / hostapi_check が残っている。P7 のとおり)。
**`rm` で 5 本を消し**(sequencer はユーザーの非公開アプリなので残した)、ランチャーは **Settings / sequencer / metronome / mp3player** になった
(`captures/phase22-step5/cam_still_154250.png`)。**実機は OFF のファームのまま残した**(普段使いの状態)。回帰を回すときは `src/build`(ON)を焼き直す。

**運用上の注意**: ON のファームを焼くと検査用・診断用アプリが SD に置かれ、OFF に戻しても残る。**回帰の後に OFF に戻したら `rm` で消す**(決定 1 の結果)。
手間が気になるようなら、設計メモ c の「`.seeded` の一覧」を後で入れられる(roadmap に残す)。

### 音(人間の確認用)

ON のファームで、カメラ録画(`captures/phase22-step5/cam_rec_153847.mp4`、h264 / aac、20 秒)しながらシリアルから
metronome の START → 5 秒 → STOP、mp3player の PLAY → 6 秒 → STOP を流した。1 秒ごとのピークでは **metronome の区間(4〜9 秒)に拍のピーク
(−39〜−41 dBFS、暗騒音 −48〜−52)**が出ている。MP3 の区間(14〜18 秒)はピークが小さい(ミキサーの既定が Master 50 × MP3 35)。
**最終確認はユーザーの耳**(回帰の合否には入れない)。

## ステップ 6: 文書(2026-09-27)

| 文書 | 変更 |
|---|---|
| `docs/workflow.md` | **§1-8 をステップ 0 e の文面に改訂**(ユーザー承認済み)。§3.3(操作はシリアルから注入してよい、判断は人間)、§3.4(対象 3 本、DEV_APPS のビルド、`ls` の事前確認、`rm`、シナリオの書式、コマンドの表、一度きりの確保、`stop idle`、書き途中の行)、§3.6(FIFO つきで起動して `ui-linux.sh` で操作)、§3.7(FIFO 方式に書き直し)、例示のアプリ名 |
| `CLAUDE.md` | 回帰対象の段落を 3 本に(経緯を残す) |
| `README.md` / `README.ja.md` | 既定のファームのアプリ、`KYBOTOS_DEV_APPS`、古いファームから更新したら `rm`、回帰の説明 |
| `wasm-apps/README.md` | アプリ一覧(dev/ の 3 本、touch_demo / seq_smoke を削除)、dev/ のビルドと既定のファーム |
| `hosts/linux/README.md` | 起動例(touch_demo → metronome)、`dev/` の一覧、`KYBOTOS_CMD_FIFO` |
| `docs/lessons.md` | 「回帰の統合と UI の注入(Phase 22)」7 項目 |
| `docs/roadmap.md` | Phase 22 を done、U-11 / U-18 クローズ、U-23 更新、U-25 追記、**U-30 / U-31 を追加**、変更履歴 |
| `docs/status.md` | 2026-09-27 時点 |

`docs/architecture.md` / `docs/hostapi.md` の touch_demo / seq_smoke / midi_loopback の記述は、移行計画や API の経緯として書かれたもの
(当時の回帰条件)なので書き換えていない。

## 完了条件の確認

| # | 条件 | 結果 |
|---|---|---|
| 1 | 回帰が 3 本になり、実機・Linux とも 1 コマンドで、タップを含めて人手なしで PASS | ✅ `device-regress.sh`(45 秒)/ `linux-regress.sh`(29 秒) |
| 2 | 検査アプリの合否を機械判定、わざと壊すと FAIL | ✅ `expect RESULT PASS`、壊した一時ビルドで `RESULT FAIL ton` |
| 3 | 既定のファームのランチャーには実用アプリだけ | ✅ OFF のファーム + `rm` で Settings / sequencer / metronome / mp3player(sequencer は外から入れたアプリ) |
| 4 | Host API / ABI 不変、metronome / mp3player の highmark 不変 | ✅(変えたのはデバッグ用の入口と、拒否したパスのログの文言だけ) |
| 5 | 所要時間を統合前後で実測 | ✅ ステップ 5 |
| 6 | 外のアプリを `--conf` で足す仕組みが従来どおり動く | ✅ 実機・Linux とも midi_loopback を外の conf の形で足して PASS |
| 7 | workflow.md §1-8 を含む文書の更新 | ✅ ステップ 6 |

### 申し送り

- **U-30 / U-23**: 一度きりの確保の中身は未特定。回帰の値(−176 / −36)が変わったら見る。
- **U-31**: ON ↔ OFF を行き来すると検査用アプリが SD に残る(`rm` で消す)。
- **`MIN_LARGEST_INT` ちょうど**: 次に静的領域が増えて下回ったら、free_int の差を見てから判断する。
- **音の最終確認**(`captures/phase22-step5/cam_rec_153847.mp4`)はユーザーに依頼する。

### 追記: 非公開の app-sequencer の追従(2026-09-27)

外のアプリの conf は kybotos の conf を source するので、対象が 3 本になったことにはそのまま追従する。ただし app-sequencer は
**自分のビルドディレクトリで sequencer 入りのファームを作る**ので、そのファームに hostapi_check が入っていないと、回帰は最初の `ls` の確認で止まる。
app-sequencer のビルドスクリプトに ON / OFF の指定(回帰用は ON、普段使いは OFF)を足し、**sequencer を含む 4 本で実機・Linux とも PASS** した
(sequencer × 3 は +0・反復一致、警告 0)。実機はそのあと OFF のファームに戻し、SD の検査用アプリを `rm` で消した
(ランチャーは sequencer / metronome / mp3player)。

### 追記: 音の録り直し(2026-09-27)

ステップ 5 の録画(`cam_rec_153847.mp4`)は**ユーザーが再生しても聞こえなかった**。原因は、`cam-rec.sh` が pulse の `default` から録っていて、
**既定の入力がヘッドセット(Jabra EVOLVE 20)のマイクになっていた**こと(StreamCam のマイクではない)。平均 −53.8 / 最大 −37.6 dBFS で、
ステップ 5 に「metronome の区間に拍のピーク」と書いたのは暗騒音の中の小さな差で、**聴ける音は録れていなかった**(記録の読み違い)。

ユーザー判断で **`cam-rec.sh` に `CAM_AUDIO_SOURCE` を足し、既定を StreamCam のソース名にした**(workflow §3.3 に追記)。
OFF のファームのまま録り直した `captures/phase22-step5b/cam_rec_155831.mp4` は**平均 −15.9 / 最大 0 dBFS**で、1 秒ごとの RMS は
metronome の区間(3〜8 秒)−19〜−22 dBFS、無音の区間(9〜11 秒)−48〜−50、MP3 の区間(12〜18 秒)−5〜−22 dBFS。
