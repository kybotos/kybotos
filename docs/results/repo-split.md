# repo の分割に伴うビルドと回帰の切り離し(2026-09-26)

Sequencer アプリ(`sequencer` / `seqcore`)とその指示書・記録を、非公開の repo(`kybotos/app-sequencer`)へ移した。
この記録は、そのあとに**この repo だけでビルドと回帰が通る**ようにした変更と、その確かめ方をまとめたものである
(目的・変更・確認はこの記録で完結させる)。プラットフォーム側の Phase 16〜21f の経緯は `docs/results/phase16-21-platform.md`。

## 目的

1. この repo は**単体でビルドと回帰が通る**(Sequencer が無い環境、つまりこの repo を clone しただけの環境で)。
2. **この repo の外のアプリ**(Sequencer など)を、この repo のファーム・Linux ホスト・回帰の仕組みに**載せられる**。
   外のアプリのコードや設定は、この repo に入れない。

## 分割の時点で残っていた結合

| 箇所 | 内容 |
|---|---|
| `src/components/wasm_runtime/CMakeLists.txt` | `EMBED_FILES` に `wasm-apps/sequencer/sequencer.wasm`(もう無いのでビルドが通らない) |
| `src/components/wasm_runtime/launcher.cpp` | アプリごとの `_binary_*_wasm_*` の extern と、SD への初期配置(seed)の行 |
| `scripts/device-regress.conf` / `linux-regress.sh` | 回帰の対象に sequencer。Linux の回帰は `wasm-apps/<app>/<app>.wasm` 決め打ち |
| `CLAUDE.md` / `docs/workflow.md` / `wasm-apps/README.md` | 回帰 6 本、sequencer 専用スクリプトの一覧、sequencer / seqcore の行 |
| `docs/roadmap.md` / `docs/status.md` / コードのコメント | 移したフェーズの記録へのリンク |

## 変更

### ファームへの埋め込み: 表から回す

- `wasm_runtime/CMakeLists.txt` が、公開アプリの一覧(touch_demo / mp3player / metronome / midi_loopback / seq_smoke / synth_probe)に
  **CMake のキャッシュ変数 `KYBOTOS_EXTRA_APPS`**(`.wasm` の絶対パスの `;` 区切り)を足して `EMBED_FILES` に渡し、
  同じ一覧から **seed の表 `embedded_apps.c`** をビルドディレクトリに生成する(中身が変わったときだけ書き換える)。
  launcher はその表(`embedded_apps.h` の `kEmbeddedApps`)を回すだけで、アプリ名を知らない。
- `EMBED_FILES` のシンボル名と中間ファイル(`build/<name>.S`)はファイル名だけで決まるので、**同名のアプリは configure で止める**。
  パスが無いときも止める。
- ESP-IDF が依存関係を集める先行評価(`CMAKE_BUILD_EARLY_EXPANSION`)では、ファイルの確認も生成もしない
  (そこでは `CMAKE_CURRENT_SOURCE_DIR` が別の場所を指し、最初の版は「wasm app not found」で止まった)。
- 外のアプリを入れたファームは、**ビルドディレクトリを分けて**ビルドする(`idf.py -B <dir> -DKYBOTOS_EXTRA_APPS=...`)。
  キャッシュ変数はビルドディレクトリに残るので、`src/build` を使い回すと以後のこの repo だけのビルドにも入ってしまう。
- メニューは従来どおり SD の `/sdcard/apps` の走査で作るので、**SD に手で置いたアプリもそのまま動く**。
  seed の表は「最初から入れておくもの」の入口として 1 か所にまとまった(将来アプリのパッケージ形式を入れるときは、表の要素を替える)。
- ついでに、`-std=gnu++17` を C++ にだけ付けるようにした(C のファイルが増えて警告が 1 つ増えたため)。
- 既知の挙動: **以前のファームが SD に置いた `sequencer.wasm` は残る**(seed は同名のファイルを書くだけで、消さない)。
  メニューには出るが、回帰の対象ではない。

### 回帰: 5 本に戻し、外のアプリを足せるようにした

- `scripts/device-regress.conf` の `APPS` を 5 本(touch_demo / mp3player / metronome / midi_loopback / seq_smoke)に戻した。
  しきい値は変えていない。
- conf に **`APP_WASM`**(Linux の回帰で起動する `.wasm` のパスの上書き)を足し、`linux-regress.sh` に **`--conf`** を足した。
- `device-regress.sh` と `device-pool.sh` に **`--build-dir`(モニタの `idf.py -B`)と `--mount`(docker の `-v`)**を足した。
  外のアプリを埋め込んだファームを、そのビルドの ELF でモニタするため。
- 外のアプリの回帰は、**この conf を source して `APPS` と `APP_WASM` を足した conf を `--conf` で渡す**(`docs/workflow.md` §3.4 / §3.7)。

### 文書とリンク

- `CLAUDE.md` の回帰のルール、`docs/workflow.md`(§2.3 の例、§3.4 の外のアプリ、§3.7 を 5 本に、§3.8 から sequencer 専用スクリプトの一覧を外した)、
  `wasm-apps/README.md`(sequencer / seqcore の行を外し、外のアプリの載せ方と、Sequencer が非公開 repo にあることを書いた)。
- `docs/roadmap.md`: Sequencer トラックの表を**プラットフォーム側の変更だけ**に縮め(行は残す)、Sequencer だけの課題 U-19 / U-20 / U-28 を移した。
  変更履歴と `docs/status.md` は当時のスナップショットなので本文は残し、移したファイルへのパスに「(非公開)」を付けた。
- コードとスクリプトのコメントにあった移したフェーズの記録への参照は、`docs/results/phase16-21-platform.md` の該当節へ張り替えた。
  `docs/prompts/` と `docs/results/` の中は書き換えていない。

## 確認

| 項目 | 結果 |
|---|---|
| ESP32-S3 のビルド(この repo だけ) | OK。生成された表は 6 本、`sequencer` のシンボルは無い |
| Linux ホストのビルドと C の単体テスト | OK(`seq_core_test` 1/1) |
| `appui` のテスト(`cargo test --features std`) | 30 passed |
| **Linux の回帰 5 本** | **ALL PASS**。highmark は touch_demo 15,944 / mp3player 19,112 / metronome 22,952 / midi_loopback 29,120 / seq_smoke 28,968(**Phase 21f と同じ**) |
| **実機の回帰 5 本** | **ALL PASS**。全行 差分 +0、反復 3 回も同一、許容外の WARN/ERROR 0 件。停止時 `free_int` 150,312 / `largest_int` 102,400 / `free_psram` 8,136,668(**Phase 21f と同じ**) |
| 外のアプリを載せる口 | 非公開 repo の Sequencer を `KYBOTOS_EXTRA_APPS` で埋め込んだファーム(別のビルドディレクトリ)で、実機の回帰 6 本、`--conf` による Linux の回帰 6 本が ALL PASS。Sequencer の highmark(実機 155,088 / Linux 221,440)も分割前と同じ |

### 分かったこと: managed component の版が固定されていない

新しく clone した状態で最初にビルド・回帰したとき、実機の停止時の値が **`free_int` 150,280(−32 B)/ `largest_int` 98,304(−4KB)**になり、
`largest_int` が `MIN_LARGEST_INT`(98,304)ちょうどになった。**Sequencer を埋め込んだファームでも同じ値**だったので、分割の変更は原因ではない。

`src/dependencies.lock` は `.gitignore` 対象で、clone し直すと `idf_component.yml` の範囲で最新の版が解決される。
この日の解決は **LVGL 9.5.0 → 9.6.0~1、esp_lvgl_port 2.8.0~1 → 2.9.0** だった。分割前の lock を置いて `idf.py reconfigure build` すると
値は 150,312 / 102,400 に戻った(上の表はこの状態)。

- 版が変わると `sdkconfig` の LVGL の項目も変わる。**lock を戻すだけではビルドが通らない**(`LV_MEM_SIZE >= 2kB is required` ほか)。
  `src/sdkconfig`(生成物)を消して作り直した。作り直した `sdkconfig` は分割前のものと一致した(`sdkconfig.defaults` 以外の手修正は無かった)。
- lock を固定するか(コミットするか)は roadmap **U-29**。

## 変えていないこと

- Host API、ホストの挙動、サンプルアプリの `.wasm`。
- 回帰のしきい値。
- 識別子の `midibox_*` / `MIDIBOX_*` / `MBCMD`、docker のマウント先 `/workspaces/MidiAppBox`(改名は別の作業)。
