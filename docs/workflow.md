# MidiAppBox 標準開発ワークフロー(リファレンス)

## 位置づけ

- 本書は、herdr ペイン経由のビルド / フラッシュ / モニタ / カメラ撮影という
  **確立済みワークフローの原本**であり、以下の用途で参照する:
  1. 新しいフェーズ指示書(docs/prompts/phaseXX.md)入力時の動作確認・デバッグのベース。
     フェーズ指示書は本書を参照し、フェーズ固有の差分だけを書けばよい。
  2. ワークフロー自体の一巡チェック(§4 のモードで実行)。
- 本書は 2 層に分かれる:
  - **§1 不変条件**: やり方を変えてはならない点。変更には必ずユーザーの承認が要る。
  - **§2–3 推奨手順**: 既定の具体的なやり方。改善してよいが、**実行前に**差分と理由を
    提示して承認を得ること。承認なしに別のやり方へ置き換えない(試行錯誤で
    別解を探ることも含めて禁止)。承認されて成功したら本書と CLAUDE.md を更新する。
- **本書に書くのは MidiAppBox 全体の開発に関わることだけ**(ビルド / フラッシュ / モニタ、
  回帰、測定、画面キャプチャなど、**どのアプリの開発でも使う手順**)。
  **特定のアプリに依存する手順・レイアウト・判断は本書に書かない。** 必要になったら
  そのアプリの仕様書(`docs/apps/<app>/`)か該当フェーズの `docs/results/` に置き、
  本書からは参照するだけにする(置き場所はその都度検討する)。
  本書の例に出てくるアプリ名(metronome / seq_smoke 等)は、**手順を示すための具体例**であって
  そのアプリ固有の手順という意味ではない。
- 役割分担: **CLAUDE.md は常時従う原則の要点、本書は herdr/hpane ワークフローの具体
  (ペイン構成・コマンド形・タイムアウト・初回セットアップ・手順)の原本**。
  herdr/hpane に関する記載は CLAUDE.md には重複させず本書に一本化する。
  両者が食い違う場合は作業を進めず、食い違い自体を報告すること。
  **セッションの最初に必ず本書を通して読むこと**(CLAUDE.md の指示)。

## §1 不変条件(変更にはユーザー承認が必要)

再現性を壊す典型は「待ち方・実行形式・環境の無断変更」である。以下は理由込みで
固定されており、一見改善に見える変更(例: 番兵方式をやめて `herdr wait output` で
ログ文言を直接待つ)が過去に失敗した実績に基づく(各項の根拠は docs/lessons.md と
docs/results/)。

1. **シェル実行はすべて `scripts/hpane.sh` 経由**。直接 Bash で実行しない。
2. **完了待ちは `hpane.sh run` の番兵トークン方式のみ**。`herdr wait output` を
   ビルドログの文言に直接マッチさせない(スクロールバック誤マッチ・文言揺れ・
   高速スクロール取りこぼしの実績あり)。exit code が成否、124 はタイムアウト。
3. **pane ID を記憶・再利用しない**。必ずラベルから毎回解決する(ID は非永続)。
4. **ペインに対話状態を持たせない**。docker 内作業も毎回一発コマンド。
   cwd ドリフトに注意し、ビルドは絶対パス+成果物のタイムスタンプ/シンボル確認をセットで。
5. **Docker イメージタグはリポジトリトップ README.md 準拠**。build と flash/monitor は
   同一タグ。devcontainer CLI(`devcontainer up`/`exec`)は使わない(README と同じ
   生イメージへの `docker run`+`docker exec` では通る同一ソース・同一 pin バージョンの
   managed component が、devcontainer CLI 経由のビルドだと `-Wignored-qualifiers` が
   `-Werror` 化されてビルド失敗する現象を確認済み。根本原因未特定)。
   非対話コマンドで `idf.py` を使うときは `bash -c 'source /opt/esp-idf/export.sh && ...'`
   で明示 source(`docker run ... bash -lc '...'` はログインシェル扱いで `~/.bashrc` を
   読まないため不可)。
6. **monitor は `PYTHONUNBUFFERED=1 ... | tee <ログ>` 方式**で常駐(`send`)させ、
   `waitfor` とログファイルで読む。monitor 再起動は既定でボードをリセットする点に注意。
7. **タイムアウトは §6.2 の既定値表に従う**。超過時は勝手に次へ進まず、
   `read` でログ確認 → 報告して停止。
8. **実機のタッチ操作はユーザーに物理操作を依頼する**(プログラム注入経路なし)。
   Linux ホストの UI クリック自動化(xdotool のマウスクリック)は信頼できないため
   使わない。ランチャー操作が不要なら単発実行モードで回避する。
   Linux ホスト(SDL ウィンドウ)の画面キャプチャは `scripts/screen-still.sh` /
   `screen-rec.sh` で取れる(Phase 18。ウィンドウ ID を指定する `import -window` / `xwd -id`。
   x11grab は画面全体を読むのでこの環境では黒くなり、使わない)。キャプチャでクリックの
   届き先を確かめられるようになったので、Phase 18 では xdotool のクリック + キャプチャで
   画面遷移を確認した(承認済みの設計。本項の「使わない」の見直しは別途承認を得る)。
9. **キャプチャ出力は `captures/<タスク名>/`**(.gitignore 対象)。
   Zenn 素材として残すものは `~/ビデオ/zenn-phaseXX/` にコピー。
10. `.claude/settings.local.json` の permissions 追記が必要になったら、
    内容を提示してユーザーに依頼する(勝手に権限前提の手順へ変えない)。

## §2 基本ワークフロー(やるべきこと)

環境ごとの手順と、各手順で必ず確認する観点。順序は Linux → ESP32 を推奨
(安価な環境で先に問題を潰す)。

### 2.0 環境確認(セッション初回に一度)
- `ensure` の冪等性(再実行で同一 pane)と `run` の一発実行(echo テスト)を確認
  してから本作業に入る(具体コマンドは §3.0)。
- 環境そのものの初回セットアップ(ユーザーが一度だけ実施)は §6.3。

### 2.1 Linux ホスト
やること: ビルド → 単発実行モードで対象アプリを起動 → 動作確認 → 終了。
確認観点:
- ビルドが exit 0。
- ログに起動(`app started`)→ 単発モード表示 → 終了(`app stopped`)が一貫して出る。
- stderr に警告(`no free slot`、WARN/ERROR)が無い。
- プロセスが残留していない。

### 2.2 ESP32 実機
やること: ビルド → フラッシュ → **`scripts/device-regress.sh` で自動回帰**
(heap・警告の機械判定。§3.4)→ (音・画面の確認が要る場合)モニタ常駐+カメラ録画を
開始した上でユーザーに物理操作を依頼 → 録画停止・静止画 → ログ確認。

**heap とログの回帰は自動回帰スクリプトを既定とする**(Phase 12 作業 3)。
ユーザーに全アプリのタップを依頼するのは、音・画面・タッチ応答そのものを
確認したいときだけにする。

確認観点:
- ビルド・フラッシュが exit 0。モニタで `app_main` 到達。
- アプリ起動〜終了がシリアルログで一貫している。
- **free heap が開始時と一致**(`app: stopped (ok), free heap NNNN (at start NNNN)`、
  リークなし)。
- WARN/ERROR/`no free slot` が無い。既知の起動ノイズ(ボードリセット直後の
  `I2C transaction unexpected nack detected` 一連、Touch online 前)は無視してよい。

### 2.3 記録
- 実施結果は `docs/results/<対応するファイル>.md` に追記(冒頭に対応する指示書への
  参照)。ファイルが無ければ `docs/prompts/` の指示書名に対応させて新規作成する。
- キャプチャは `captures/<タスク名>/` に置き、`git status` に現れないことを確認。

## §3 推奨手順(既定の具体的なやり方)

以下は check-workflow / 7D / 8c で実績のあるコマンド列。`<repo>` はリポジトリの絶対パス。

### 3.0 環境確認

```bash
herdr pane list --workspace 1            # 初回のみ: JSON 構造の実物を確認
./scripts/hpane.sh ensure unix-build     # 再実行して同一 pane_id を確認
./scripts/hpane.sh run unix-build "echo hello" 10000
```

`hpane.sh` は herdr の JSON 構造をキー名に依存しない形で走査しているが、
`ensure` が正しい pane ID を返さない場合はパーサ部(VERIFY コメント箇所)を
実際の JSON に合わせて修正し、修正内容を報告すること(改修は §5 の手続き。
一巡チェックモード §4 の実行中はスクリプトを修正せず、報告して停止する)。

ビルドキャッシュ関連のエラー(例: `cmake` の
`CMakeCache.txt directory ... is different than the directory ... where
CMakeCache.txt was created`、`idf.py` の managed_components ハッシュ不一致等)が
出た場合は、該当する `build/` ディレクトリ等(いずれも .gitignore 対象の
生成物)をクリーンにしてから同じコマンドを再実行してよい。**毎回のクリーン
ビルドはしない**(このエラーが出たときだけの対処)。Linux ホストの場合の
クリーン例:

```bash
rm -rf hosts/linux/build
```

### 3.1 Linux ホスト

```bash
# ビルド(絶対パスで cwd ドリフトを回避)
./scripts/hpane.sh run unix-build \
  "cd <repo>/hosts/linux && cmake -B build && cmake --build build -j" 600000

# 単発実行モードで起動(常駐なので send。DISPLAY を付ける)
./scripts/hpane.sh send unix-build \
  "cd <repo>/hosts/linux && DISPLAY=:0 ./build/midibox_host ../../wasm-apps/metronome/metronome.wasm"
./scripts/hpane.sh waitfor unix-build "app started" 15000

# 数秒動作させたのち、ESC キー送信で終了(キー送信は信頼できる。クリックは不可)
xdotool search --name "MidiAppBox WASM host"   # → <window id>(複数ヒットすることがある)
# 複数ヒットした場合は無関係なウィンドウ(mutter-x11-frames 等の装飾ウィンドウが
# 誤って一致することがある)が混ざっていないか、対象 pid と突き合わせて確認する:
#   for w in <window id...>; do xdotool getwindowpid $w; done
#   pgrep -af midibox_host   # ここで得た pid と一致するものを選ぶ
xdotool key --window <window id> Escape

# 確認
./scripts/hpane.sh read unix-build 60          # app started → single mode → app stopped
pgrep -af midibox_host                          # 残留なしを確認(何も出ない)
```

- **ホストの出力をパイプ(`| tee` 等)に通さない。** stdout がブロックバッファされ、
  `app started` が終了時までペインに出ず、`waitfor` が空振りする。ログをファイルにも残したい
  ときは **stderr だけリダイレクト**する(`2> <file>`。`app started` / `app stopped` は stdout)。
- **`waitfor` はスクロールバックにも一致する。** 同じペインで同じアプリを続けて起動すると
  前回の `app started` を拾う。アプリ名を含めた文字列で待つか(`app started: ../../wasm-apps/<app>`)、
  `pgrep -x midibox_host` で実際に起動したことを確かめる(§3.4 と同じ趣旨)。
- 画面を撮って確認したいときは §3.6。
- **ホスト非依存のコードは Linux でテストしてから実機へ行く**(実機・Linux の両方で同じ結果になる部分を、
  安い側で先に潰す)。
  ```bash
  # shared/seq_core.c の単体テスト(偽の時計でディスパッチを決定的に進める。Phase 17 で新設)
  ./scripts/hpane.sh run unix-build \
    "cd <repo>/hosts/linux && cmake --build build -j && ctest --test-dir build --output-on-failure" 600000
  # Rust の crate(wasm-apps/seqcore 等): ホストでのテストと、no_std で通ることの確認
  ./scripts/hpane.sh run unix-build \
    "cd <repo>/wasm-apps/seqcore && cargo test --features std && \
     cargo build --release --target wasm32-unknown-unknown" 600000
  ```
  **境界の数µs で決まる挙動(「その tick のクロックを出さない」等)は実時間の試験では確かめられない。**
  偽の時計の単体テストに寄せ、実機・Linux では「同じ `.wasm` が同じ判定を出すか」を見る(Phase 17)。
  アプリの `.wasm` のビルドとコミットの手順は `wasm-apps/README.md`。

### 3.2 ESP32 実機

**`docker run --rm`(都度起動)に統一する。** 持続コンテナ + `docker exec`
方式は、`entrypoint.sh` の gosu 降格(`docker run`)を経由しない `exec`
(root 実行)と混在すると `build.ninja`/`.ninja_log` 等の所有者が割れて
`Permission denied` を起こす実績があるため使わない(詳細は docs/results/phase08a.md)。
ビルド・フラッシュ・モニタすべてこの方式で統一する。

**一時的な計測コード(プール消費・実行時間などを一度だけ見たいとき)**:

- 目印のコメント(`PHASE18-TEMP` のようにフェーズ番号入り)を付けて入れる。
- 計測が済んだら撤去し、**`git diff` を見て一時コードが残っていないことを確認**してから次のビルドに進む。
  撤去の仕方は、そのファイルの**他の変更がコミット済みかどうか**で決める:
  - **コミット済み**(一時コードだけが未コミットの差分): **`git checkout -- <file>` で戻し、
    `git diff` が 0 行であることを確認**する。
  - **未コミットの実装が入っている**: **`git checkout` を使わない。** 目印コメントの行だけを消す
    (または先に実装をコミットしてから計測する)。**Phase 18a で `git checkout -- wasm_runtime.cpp` を
    実行し、未コミットだった実装ごと消して再適用する羽目になった。**
  **Edit で 1 行だけ消すと前後の行が連結されることがある**
  (Phase 18 で `src/CMakeLists.txt` の 2 行がつながり、ビルドに使う前に `git diff` で気づいた)。
- `idf_build_set_property(COMPILE_DEFINITIONS …)` でビルド時定義を足す / 外すと**全再ビルド**に
  なる(約 2,000 ターゲット、数分)。計測のために 2 回ビルドすることを見込んで段取りする。
- 実機の自己検査(`SEQCORE_SELFTEST`)もこの形で有効化する(`shared/seq_core.h` の説明)。

`managed_components/`(gitignore 対象)を「再取得可能なキャッシュ」と即断して
中身を確認せず `rm -rf` してはならない。ハッシュ不一致で `idf.py fullclean` が
保護的に停止した場合、削除前に該当ファイルの差分を確認すること(ローカル修正が
入っていた可能性があるファイルを不用意に削除してしまった実績あり)。

```bash
# ビルド(README のタグの生イメージを都度起動。export.sh を明示 source)
./scripts/hpane.sh run esp32-build \
  "docker run --rm -v <repo>:/workspaces/MidiAppBox -w /workspaces/MidiAppBox/src \
   ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5 \
   bash -c 'source /opt/esp-idf/export.sh && idf.py build'" 1800000

# フラッシュ(--device=/dev/ttyACM0 --group-add <dialout gid> を付けた
# docker run --rm -it を都度起動。monitor 同様 -it 必須)
./scripts/hpane.sh run esp32-build \
  "docker run --rm -it -v <repo>:/workspaces/MidiAppBox -w /workspaces/MidiAppBox/src \
   --device=/dev/ttyACM0 --group-add <dialout gid> \
   ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5 \
   bash -c 'source /opt/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash'" 300000

# モニタ(常駐: send + waitfor + tee)。ログはマウント配下(ホスト側 captures/
# 等)に出す。コンテナ内一時パス(/tmp 等)は --rm で消え、herdr の
# スクロールバックも高頻度ログですぐ埋まるため、ホスト側ファイルで確認する。
./scripts/hpane.sh send esp32-monitor \
  "docker run --rm -it -v <repo>:/workspaces/MidiAppBox -w /workspaces/MidiAppBox/src \
   --device=/dev/ttyACM0 --group-add <dialout gid> \
   ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5 \
   bash -c 'source /opt/esp-idf/export.sh && PYTHONUNBUFFERED=1 idf.py -p /dev/ttyACM0 monitor | tee /workspaces/MidiAppBox/captures/<タスク名>/monitor.log'"
./scripts/hpane.sh waitfor esp32-monitor "app_main" 60000
```

長時間接続した `idf.py monitor` は `docker ps` 上 `Up` のままサイレントに
詰まる(実機からの新規出力を転送しなくなる)ことがある。実機自体は動作を
続けているため、`docker kill <container id>` で該当コンテナを落として
モニタを再起動すれば復旧する。`herdr pane send-keys <pane_id> "C-c"` は
効かないことがあるので、`docker ps` で確認して直接 kill する方が確実
(詳細は docs/results/phase08c.md)。

ユーザーが実機を触りながら現象を確認したい場合、`esp32-monitor` ペインに
直接フィルタ済みのライブログを出すと効率的
(`idf.py monitor | tee <保存用ログ> | grep --line-buffered -E "<pattern>"`)。
ホスト側ファイルを都度読み上げて報告するより、ユーザー自身がペインを見ながら
物理操作できる。

### 3.3 実機の動作検証(カメラ+人間操作)

```bash
# 録画開始(常駐: send)
./scripts/hpane.sh send camera "./scripts/cam-rec.sh captures/<タスク名>"
```

→ ユーザーに具体的な手順を提示して物理操作を依頼する
(例: 「ランチャーから metronome をタップ → START → 数秒後 STOP → 終了」)。
完了の返答を**待ってから**次へ進む。

```bash
# 録画停止(空文字送信 = Enter)
./scripts/hpane.sh send camera ""
# 静止画(必要ならユーザーに画面状態の保持を依頼)
./scripts/hpane.sh run camera "./scripts/cam-still.sh captures/<タスク名>" 30000
```

- 生成物は `ffprobe` で h264 / 正常な duration を確認。
- 動画・音声ずれは原因特定・修正済み(2026-07-20、check-workflow-routine 後の
  別タスク。詳細は scripts/cam-rec.sh 冒頭コメントと docs/results/av-sync-fix.md)。根本原因は
  ffmpeg が v4l2/pulse の 2 入力を**それぞれ自分の先頭時刻で 0 にリセット**し、
  音声が映像より系統的に約164ms 遅れて始まる相対差を破棄していたこと(結果として
  音声が早く再生される)。pulse 入力に `-itsoffset`(既定 0.16s、環境変数
  `CAM_AUDIO_DELAY` で調整可)を前置して補正、ユーザーの ffplay 判定で同期を確認。
  併せて `-thread_queue_size`/`-timestamps abs` も維持(キュー詰まり回避・両入力の
  時刻系統一)。

### 3.4 実機の自動回帰(heap・警告の機械判定)

`scripts/device-regress.sh` が、残したアプリを順に
「`run` → 起動待ち → 一定時間保持 → `stop` → 停止待ち」で回し、free heap /
largest free block / WARN・ERROR を集計して Markdown の表と合否を出す。
**ユーザーの物理操作は不要。**

```bash
# ファームウェアは CONFIG_MIDIBOX_SERIAL_CMD=y(既定)でビルド・フラッシュ済みのこと
./scripts/device-regress.sh --task <タスク名>
# → captures/<タスク名>/monitor.log と report.md。exit 0 が合格
```

- 対象アプリ・保持秒数・許容警告パターン・アプリごとの許容 heap 差分・下限しきい値
  (`MIN_FREE_INT` / `MIN_LARGEST_INT` / `MIN_FREE_PSRAM`。Phase 15 で「固定値一致」から改訂)・
  **反復回数**(`REPEAT_RUNS` / `REPEAT_OVERRIDE`)は `scripts/device-regress.conf` に外出ししてある。
- **同じアプリを N 回繰り返す**(Phase 18 で追加、既定 3 回。seq_smoke は 1 回)。
  1 回ごとの差分 +0 に加えて、**N 回の終了時の `free_int` / `free_psram` がすべて同じ**ことを
  判定する(1 回ごとの差分が 0 でも、開始値が回を追って下がる漏れを捕まえるため)。
  表には「<アプリ> 反復 N 回」の行が出る。
- **保持中にアプリが自分で止まると「停止しない」と判定される。** スクリプトは
  「自分が送った `stop` の後の停止行」を待つので、それより前に止まっていると空振りする
  (このとき `stop` の応答は `stop idle`)。**電源キーの短押しはログを出さずにアプリを止める**ので、
  1 回の FAIL で結論を出さず、`monitor.log` の時系列(`MBCMD: stop ok` の有無)を見てから再実行する
  (Phase 18 で実際に 1 回だけ出て、再実行では再現しなかった)。
- **実機の回帰と Linux ホストの回帰を同時に走らせない。** 実機が UM-ONE へ流した MIDI を
  Linux ホストが受け、ドレインしないアプリ(touch_demo 等)で `midi: RX ring buffer full` が
  大量に出る(Phase 17。構成依存の挙動で、単独で走らせれば出ない)。
- スクリプトは実行前に `docker ps` を見て、シリアルポートを掴んだままの
  `idf.py monitor` コンテナがあれば落とす(既知の教訓)。終了時も同様に片付ける
  (`--keep-monitor` で残せる)。
- 実機側の受け口は USB Serial/JTAG のコマンドコンソール
  (`ping` / `ls` / `run <app>` / `stop` / `heap`。応答はタグ `MBCMD` のログ行)。
  **タッチ・電源キーの既存操作系は変更していない。**
- アプリ内 UI 操作(metronome の START/STOP 等)は自動化していない。音・画面の
  確認は §3.3 の人間操作+カメラのまま。

**待ち方(§1-2 の趣旨の強化)**: 本スクリプトの完了待ちは、herdr のペイン出力では
なく **`tee` が書くログファイルの「今回の待ちを始めた行より後ろ」** に対して行う。
ペイン出力を対象にすると、スクロールバックに残る前回の実行の行に誤マッチする
(Phase 12 で、モニタ再起動直後の `waitfor` が前回の起動ログに一致し、まだ起動して
いないのに起動したと誤判定した実績がある)。ペインは人間が見るライブ表示として残す。

### 3.5 MIDI Clock の測定(Phase 13 で追加)

実機 MIDI OUT → UM-ONE → PC で受けたクロックを記録・集計する。集計項目は
`midi_loopback` の E1 統計と同じ(0xF8 総数・期待数・clocks÷expected・間隔の
min/mean/max・ヒストグラム・外れ値・見かけ BPM の分布・0xFA/0xFB/0xFC の件数)。

```bash
# 計測(captures/<タスク名>/<ラベル>.csv と .md を作る)
./scripts/midi-clock-probe.sh --task phase13 --label A1 --duration 330 --bpm 120

# 既存 CSV を条件を変えて集計し直す(再測定は不要)
./scripts/midi-clock-probe.sh --analyze-only --task phase13 --label A1 \
    --from 120 --to 300          # 判定窓を切る(例: テンポ変更区間を除外)
./scripts/midi-clock-probe.sh --analyze-only --task phase13 --label C \
    --segments auto              # テンポ切替を検出して区間ごとに集計
./scripts/midi-clock-probe.sh --analyze-only --task phase13 --label A23 --span 2
                                 # 2 番目の再生区間(0xFA〜0xFC)だけを対象にする
```

- 実体は `tools/midi_clock_probe/`(C の受信プローブ + Python の集計)。
  ビルドはラッパが必要なときだけ行う(成果物は .gitignore 対象)。
- 接続先は既定で名前に `UM-ONE` を含むポート。`--port <部分一致>` で変更でき、
  Linux ホスト自身の出力を測るときは `--port MidiAppBox` を使う。
  測定対象を後から起動する場合は `--wait <秒>` を付ける。
- 打刻は **ALSA のカーネル側(real-time キュー)を主**、受信ループの
  `CLOCK_MONOTONIC` を副として両方 CSV に残す。出力表の「カーネル打刻とユーザ打刻の差」が
  ツール自身の遅延の自己検証になる(mean 数十µs が正常)。
- **STOPPED をまたぐ間隔は統計から除外される**(0xFA/0xFB〜0xFC の再生区間ごとに
  集計する)。stop→continue の空白を外れ値と数えないため。
- **出力には「再生区間ごとの 0xF8 数」と「停止中(区間外)の 0xF8 数」がある**(Phase 17 で追加)。
  「Start から Stop までちょうど N 発」「Stop の後は 0 発」を見るための行で、
  境界停止(`HOSTAPI_SEQ_OP_STOP`)や曲の長さの検証はこの 2 行で判定する。
  それ以前の集計は区間外のクロックを黙って捨てていたので、古い `.md` と比べるときは注意。
- **記録器は測定対象を起動する前につないでおく。** `app_init` で即 `transport_start` する
  アプリ(seq_smoke 等)は最初の 0xFA を取り逃がしうる(`--wait` を付けても、ポートが現れてから
  接続するまでの隙間で漏れる)。区間の数え方で吸収されるが、**0xFA の件数だけは実際より 1 少なく出る**。
- **CC など データバイトまで見たいときは `aseqdump` を併走させる。** probe の CSV はイベントの種別しか
  持たない(`aseqdump -p <ポート名>`。seq_smoke の判定値 CC#119/#120 はこれで読む)。
- **受信側の σ で送信精度を判定しないこと。** UM-ONE 経由は USB の 1ms フレームを
  通るのでぼやける。送信側の σ は検証ビルドの送信打刻(`--txlog`)で見る。
  なお **MIDI DIN は 1 バイト 320µs** なので、それより短い受信間隔が出たら
  配送側でまとめて届いたアーティファクトである(P10-5 と同じ理屈)。
- **テンポが動いている区間を固定の公称値で判定しないこと。** 外れ値の判定は
  公称間隔の 1.5 倍 / 0.5 倍なので、テンポ変更中の区間は「欠落」ではなく
  当然の変化として大量に引っかかる。`--segments auto` で区間に分けてから見る。

### 3.6 Linux ホストの画面キャプチャ(Phase 18 で追加)

**x11grab は使わない。** 画面全体(ルートウィンドウ)を読むため、この環境(Wayland + XWayland /
GNOME)では常に黒くなる。**ウィンドウ ID を指定して読めば取れる**(静止画は ImageMagick の
`import -window`、録画は `xwd -id` の連続取得)。対象ウィンドウは **`midibox_host` の pid と
`xdotool getwindowpid` の一致**で選ぶ(同名のフレーム窓は mutter のもの)。この選択はスクリプトが行う。

```bash
# 静止画 1 枚 → captures/<タスク名>/<名前>.png
./scripts/screen-still.sh captures/<タスク名> <名前>

# 録画(常駐なので screen ペインへ send。Enter で停止 → mp4 にまとめて ffprobe で確認)
./scripts/hpane.sh send screen "cd <repo> && ./scripts/screen-rec.sh captures/<タスク名>"
./scripts/hpane.sh waitfor screen "Enterキーで停止" 15000
#   … ここで操作(クリック送信など)…
./scripts/hpane.sh send screen ""
./scripts/hpane.sh waitfor screen "Duration" 30000
```

- 録画は `xwd -id` を既定 10fps(`SCREEN_FPS` で変更可)で連続取得し、停止後に ffmpeg でまとめる。
  1 枚の取得は約 7ms。**フレーム間隔は sleep による概算なので UI の確認用**であり、
  タイミング測定には使わない(実測: 50 枚を 5.27 秒で取得 → 5.00 秒の mp4、640×480)。
- 黒画面でないことは数値で確かめられる: `convert <png> -format '%[fx:mean] %k' info:`
  (x11grab のときは平均輝度 0.0002 / 68 色、ウィンドウ指定では 0.18 / 579 色だった)。

**クリックで画面遷移を確認する場合**(Phase 18 で実施):
§1-8 の「クリックは信頼できないので使わない」は不変条件として残してあるが、
**キャプチャで届き先を毎回確かめながら**なら画面遷移の確認に使える(Phase 18 では全クリックが
意図どおり届いた)。座標は **論理座標 ×2**(`hostapi_sdl.c` の `WINDOW_SCALE`)。

```bash
pid=$(pgrep -x midibox_host | head -1)
for w in $(DISPLAY=:0 xdotool search --name "MidiAppBox WASM host"); do
  [ "$(DISPLAY=:0 xdotool getwindowpid "$w")" = "$pid" ] && WIN=$w
done
DISPLAY=:0 xdotool mousemove --window "$WIN" <論理x*2> <論理y*2>; sleep 0.3
DISPLAY=:0 xdotool click --window "$WIN" 1
./scripts/screen-still.sh captures/<タスク名> <名前>   # 届いたかを撮って確かめる
```

- 撮影のタイミングを ms で待つときは `sleep "$(printf '%d.%03d' $((ms/1000)) $((ms%1000)))"`。
  `sleep 0.$(printf %03d $ms)` は 1,000ms を超えると桁が崩れる(Phase 18 で撮り逃した)。

## §4 一巡チェックモード(routine)

本書の手順を「そのまま一巡実行して確認するだけ」の回として実行する場合の追加ルール:

- **変更してよいのは docs/results/check-workflow-routine.md への実施記録の追記のみ**。
  scripts/・CLAUDE.md・ソースコードは触らない。新ラベル追加・レイアウト変更もしない。
- 手順: §3.0 → §3.1 → §3.2 → §3.3(操作内容は「なにかしらのアプリを実行する」で
  よい。metronome 推奨)。
- 想定外の失敗はその場で修正・回避せず、報告して停止する。docs/lessons.md
  記載の既知対処のみ適用可(それでも解決しなければ停止)。
- 完了条件:
  - 全手順が hpane.sh 経由で完走。
  - `captures/<タスク名>/` に実機の動画+静止画。`git status` に `captures/` が出ない。
  - free heap 一致・警告なしを確認済み。
  - docs/results/check-workflow-routine.md に追記(指示書参照、ビルド/フラッシュ/
    モニタの成否、heap 確認、警告有無の要約)。
  - `git status --porcelain` の差分が上記追記のみ。コミットもそれのみ(英語)。

## §5 推奨手順を改善したくなったら

1. 実行前に、変更点(現行 → 提案)と理由・期待効果を提示して承認を得る。
2. 承認後に試し、成功したら本書 §3 を更新。新たな教訓は docs/lessons.md
   にも一行追加する。
3. 失敗したら元のやり方に戻し、試行と結果を docs/results/ の該当ファイルに記録する。
4. §1 の不変条件に触れる変更は、より慎重に: 過去の失敗実績(該当する教訓)を
   引用した上で、なぜ今回は成立するのかを説明すること。

## §6 環境定義(ペイン構成・タイムアウト・初回セットアップ)

### 6.1 ペイン一覧(ラベル固定)

全ラベルは **このセッション自身のタブ(プロンプトペインが属するタブ)の中に、
プロンプトを最上段・全幅(既定で高さ 35%)、その下を 2 列 x 3 行で分割配置**
される(`scripts/hpane.sh` が `herdr pane split`/`pane rename` でペイン単位のラベルを
解決・作成する。タブ単位ではなくペイン単位のラベルなので `herdr tab list` では見えない
点に注意)。`ensure`/`run`/`send`/`waitfor`/`read` の呼び出しインタフェースは
「ラベルごとに別タブ」だった旧方式・「作業ペインだけの共有タブ」だった旧方式(いずれも
check-workflow-routine で廃止)から変わらない。

```
プロンプト(全幅、既定で高さ 35%)
------------------------------------------------
esp32-build   | unix-build
esp32-monitor | zenn
camera        | screen
```

列はプロンプトペインから down split で作った `esp32-build` を左列ルート、
そこから right split した `unix-build` を右列ルートとし、各列内は真上のペインから
down split して積む。プロンプトとの高さ比率は `HPANE_PROMPT_ROW_RATIO`
(既定 0.35)で調整できる。作成後のペインサイズはユーザーが `herdr pane resize`
等で自由に変えてよい。全ラベルの一括操作には `hpane.sh ensure-all`
(まとめて展開)/`hpane.sh close-all`(まとめて閉じる。既に閉じているラベルは
スキップ)、単体には `hpane.sh close <name>` が使える。

| ラベル | 用途 | 実行形式 |
|---|---|---|
| `esp32-build` | ESP32 実機ビルド / フラッシュ | 常に docker を含む一発コマンド(§3.2) |
| `esp32-monitor` | シリアルモニタ(常駐) | `send` で起動、`waitfor` でログ待ち |
| `unix-build` | Linux ホスト(SDL)ビルド / 実行 | 一発コマンド |
| `camera` | カメラ撮影(ffmpeg / v4l2-ctl、`scripts/cam-rec.sh`/`cam-still.sh`) | `send`(常駐)+ `run`(単発) |
| `zenn` | Zenn ドキュメント作成関連 | 一発コマンド |
| `screen` | Linux ホスト(SDL ウィンドウ)の画面撮影用(`scripts/screen-rec.sh`/`screen-still.sh`)。**Phase 18 でウィンドウ単位の取得(`xwd -id` / `import -window`)に切り替えて使えるようになった**(x11grab は黒くなるので使わない) | `send`(録画、Enter で停止)+ `run`(静止画) |

新しいラベルを増やす場合は事前にユーザーの承認を得ること。

### 6.2 タイムアウト既定値(ms)

| 操作 | timeout |
|---|---|
| ESP32 フルビルド | 1800000 (30分) |
| ESP32 インクリメンタルビルド | 600000 (10分) |
| フラッシュ | 300000 (5分) |
| Linux ホストビルド | 600000 (10分) |
| モニタのログ待ち | 30000〜60000 |

タイムアウトした場合は勝手に次へ進まず、`read` でログを確認して状況を報告し
停止すること(§1-7)。

### 6.3 初回セットアップ(ユーザーが一度だけ実施)

```bash
# herdr 公式エージェントスキルの導入(Claude Code が herdr 操作を正しく学ぶ)
npx skills add ogulcancelik/herdr --skill herdr -g

# ヘルパー配置
chmod +x scripts/hpane.sh
```

`.claude/settings.local.json` の permissions に以下を追加:

```json
"Bash(herdr:*)",
"Bash(./scripts/hpane.sh:*)"
```
