# Phase 22a 実施記録: 起動からメニュー表示までの UI の改善

指示書: `docs/prompts/phase22a.md`

## 結果(2026-09-27)

**完了。** 起動するとロゴ(`docs/images/kybotos.png`)が出て、約 1.5 秒後に `Kybotos Menu` と拡張子なしのアプリ名が並ぶ(実機・Linux)。
回帰は実機・Linux とも 3 本 PASS。**Host API / ABI は不変**、`.wasm` は再ビルドしていない。

| 完了条件 | 結果 |
|---|---|
| 1. ロゴ → `Kybotos Menu` + 拡張子なし | ✅ Linux `captures/phase22a-linux/{splash,menu}.png`、実機 `captures/phase22a-boot/cam_rec_191411.mp4`(2 秒目にロゴ、3 秒目にメニュー)/ `cam_still_191429.png` |
| 2. メニューからのタップ起動 | ✅ 実機: シリアルの `tap 160 95` → `launch: /sdcard/apps/hostapi_check.wasm`(表示名に `.wasm` を付け直して起動)→ `stop` でメニューへ。Linux: `scripts/linux-launcher-shots.sh` でメニューの 1 行目をクリック → metronome が起動 |
| 3. 回帰 | ✅ 実機 `captures/phase22a-device/report.md`(45 秒、警告 0)、Linux `captures/phase22a-linux-regress/`(highmark は Phase 22 と同じ) |
| 4. ファームのサイズ | +115,488 B(1,061,792 → 1,177,280 B、`KYBOTOS_DEV_APPS=ON`)。ほぼロゴの 115,200 B。factory 4MB の 72% が空き |
| 5. Host API / ABI 不変 | ✅ `shared/hostapi_defs.h` と `wasm-apps/` に変更なし |

## 変更

| ファイル | 変更 |
|---|---|
| `scripts/gen_splash_logo.py`(新規) | ロゴの PNG を 240×240 の RGB565 の C 配列にする(Pillow)。余白の色も RGB565 に丸めた値を出す |
| `shared/splash_logo.c` / `.h`(新規、生成物) | ロゴの配列(115,200 B)と、幅・高さ・余白の色(`#183c29`)。両ホストで共有する |
| `src/components/wasm_runtime/launcher.cpp` / `.hpp` | `launcher_show_splash()` を追加(ロゴを `lv_image_dsc_t` で flash のまま描く)。最初の `launcher_show` は表示から 1.5 秒まで待ってメニューに切り替え、スプラッシュの画面を消す。題名を `Kybotos Menu` に、行は拡張子を取った名前にし、タップで `.wasm` を付け直して起動する。アプリが無いときの状態行を `no apps found` に |
| `src/main/app_main.cpp` | LVGL とタッチの初期化の直後にスプラッシュを出す。**`serialcmd::Init` を `launcher_show` の後に移した**(下記) |
| `hosts/linux/main.c` / `hostapi_sdl.c` / `.h` | ランチャーのモードのときだけロゴを 1.5 秒出す(単発実行 = 回帰・CI では出さない)。RGB565 の画像を貼る `host_sdl_image_rgb565` を追加。題名・行・状態行は実機と同じ変更 |
| `scripts/linux-launcher-shots.sh`(新規) | Linux のランチャーを起動してスプラッシュ・メニュー・起動したアプリを撮る(手動の確認用) |

### 決めたこと

- **ロゴは画面の高さいっぱい(240×240)で中央、左右はロゴの背景色**。文字(Kybotos の名前など)は足していない(画像をそのまま出す)。
- **表示時間は最短 1.5 秒**。実機は SD の準備(埋め込みアプリの配置)と重ねるので、ボードのリセットからメニューまでは
  2.34 秒 → 2.88 秒(+0.54 秒。ログの `menu: N app(s) listed`)。スプラッシュは起動 1.33 秒目から出る(それより前は LVGL の開始前)。
- **シリアルコンソールをメニューの表示の後に開く。** スプラッシュの待ちの間に `run` が届くと、起動したアプリの画面を
  待ち明けのメニューが上書きするため。回帰は `KBCMD: ready` を待つので、手順は変わらない。
- **開発者向けの文言はそのまま**: シリアルの `ls` / `run` / `rm`、ログ、Linux の標準出力、起動に失敗したときのエラー
  (`app_init/app_tick not exported` など)。

## 確認

**Linux**: ビルドの警告は既存のもの(`main.c` の `-Wformat-truncation`)だけ。回帰は 3 本 PASS(metronome 4 秒 / mp3player 4 秒 /
hostapi_check 22 秒、highmark 22,952 / 19,112 / 34,728 = Phase 22 と同じ)。最初の撮影では**ロゴの左右に境目が見えた**
(ロゴは RGB565 に量子化されるのに、余白を元の 24bit の色 `#1f3d2e` で塗っていた)。余白の色を RGB565 に丸めた `#183c29` にして解消した
(境目の両側の画素がともに `srgb(24,60,41)`)。

**実機の回帰**(`KYBOTOS_DEV_APPS=ON`):

| アプリ | 開始 free_int | int 差分 | largest_int | シナリオ | 判定 |
|---|---|---|---|---|---|
| metronome #1〜#3 | 150,264 | +0 | 98,304 | PASS(8 手順) | PASS(反復 3 回の終了値も一致) |
| mp3player | 150,264 | −36(U-23、期待どおり) | 98,304 | PASS(6 手順) | PASS |
| hostapi_check | 150,228 | −176(U-30、期待どおり) | 98,304 | PASS(6 手順) | PASS |

**開始時の free_int が Phase 22 の 150,304 から 150,264 に 40 B 下がった。** 切り分けのため、スプラッシュの呼び出しだけを外した一時ビルド
(目印 `PHASE22A-TEMP`、測定後に撤去して差分が残っていないことを確認)で回した:

| ファーム | 開始 free_int | Phase 22 との差 |
|---|---|---|
| Phase 22 | 150,304 | — |
| 22a、スプラッシュなし(起動順の変更・メニューの表記の変更だけ) | 150,288 | −16 B |
| 22a | 150,264 | −40 B |

- LVGL は自前の 64KB のプール(`CONFIG_LV_USE_BUILTIN_MALLOC`)を使い、ロゴの配列は flash にあるので、スプラッシュの
  オブジェクトは heap を取らない。スプラッシュの画面は消している。
- **どちらの差も起動時に 1 回だけで、アプリの起動・停止の差分は +0 のまま**(反復 3 回の終了値も一致)。
  確保の順序が変わったことによる一度きりの差として扱い、しきい値(`MIN_FREE_INT` 146,000 / `MIN_LARGEST_INT` 98,304)は変えていない。
  **何が取っているかまでは追っていない。**
- `largest_int` は 98,304 のまま(`MIN_LARGEST_INT` ちょうど。Phase 22 と同じ)。

**普段使いの状態に戻した**: 既定(OFF)のファームを `captures/phase22a-off-build` でビルド(1,161,024 B)してフラッシュし、
SD の検査用・診断用アプリをシリアルの `rm` で消した。メニューは Settings / sequencer / metronome / mp3player
(sequencer は非公開の app-sequencer から入れたアプリ。`captures/phase22a-off/cam_still_191907.png`)。

## workflow.md / lessons.md への反映

- `docs/lessons.md` に「起動時の UI(Phase 22a)」を追加(RGB565 の余白の色、起動順で free_int が数十 B 動くこと)。
- `docs/workflow.md` は変更なし(回帰・撮影の手順は変わっていない)。
