# Phase 22d: Kybotos Menu の配色を見直す — 実施記録

- 指示書: `docs/prompts/phase22d.md`
- 実施: 2026-10-04

## 案の比較と決定

Linux ホストと同じ文字(DejaVu 13pt)で 320×240 のモックアップを描き、Metronome / MP3 Player の画面と並べて比べた
(撮影物は `captures/menu-color-mock/`。`sheet.png` = 今 + 案 A〜D、`sheet-d.png` = D1 / D2)。

| 案 | 中身 |
|---|---|
| 今 | 全面が濃緑(スプラッシュの余白と同じ)、行は中緑 |
| A | アプリと同じ骨格(濃緑のヘッダ + ステータス行 + 黒の本体)、行は mp3player の一覧と同じ暗い緑、`Settings` はヘッダ右 |
| B | A の骨格 + 黒の本体、行は今の中緑のボタン |
| C | 今の並びのまま題名の帯だけ濃緑、残りは黒 |
| D | 全面黒、題名の下に中緑の細線、行は暗い緑の地 + 左端に中緑の印 |
| **D1** | **D + `Settings` をヘッダ右に若葉の文字だけで** |
| D2 | D + `Settings` をヘッダ右に行と同じ形の小さなボタンで |

**ユーザーの決定: D1。**

## 変えたもの

| ファイル | 変更 |
|---|---|
| `shared/launcher_theme.h` | 背景を黒 `0x000000` に(22a では濃緑 `0x183c29`)。題名の下の線 `0x3e6648`、行の地 `0x101a14`(押下 `0x24483a`)と左端の印 `0x3e6648`、`Settings` は若葉 `0x8fd18b`(押下はクリーム)、状態行はアプリの手引きと同じ `0x5f7466`。**形(題名・線・行・状態行の座標、`Settings` の当たり判定)も両ホストで共有する定数にした** |
| `src/components/wasm_runtime/launcher.cpp`(実機) | 題名の下の線、ヘッダ右の `Settings`(当たり判定 x 220〜320 × y 0〜28 の透明の箱に文字を右寄せ)。一覧から `Settings` の行を外した。行は LVGL の既定のボタン(角丸・影・文字が中央)をやめ、24px の四角・左端に 4px の印・文字は左寄せにした(**Linux と同じ形**)。一覧の箱の余白・枠を外し、6 行で状態行の手前まで |
| `hosts/linux/main.c` | 同じ形で描く。`Settings` の当たり判定をヘッダ右に。状態行の `click to launch` を `tap to launch` に |
| `scripts/linux-launcher-shots.sh` | 1 行目のアプリのタップ位置を新しい配置に(y 64 → 50) |

Host API / ABI、`.wasm` は変えていない。

## 確かめたこと

- **Linux**(`captures/phase22d-linux/sheet.png`): メニューが D1 の形、ヘッダ右の `Settings` のクリックでマスター設定が開く、1 行目のタップで metronome が起動する。
- **実機**(`captures/phase22d-device-cam/sheet.png` / `sheet2.png`): シリアルから操作を注入してカメラで撮った。6 本(回帰用のファーム)の行が状態行の手前に収まる、
  `tap 280 14`(ヘッダ右)でマスター設定が開き、✕ で閉じる、`tap 100 78` で 2 行目のアプリ(`midi_loopback`)が起動する。
  - 途中で、閉じたあとのタップがもう一度開いた帯の中(Master の −)に当たり、マスター音量を 50 → 49 にしてしまった。+ で 50 に戻した(装置の設定は持ち越されないので、再起動でも 50 に戻る)。
- **回帰 3 本 PASS**(実機 `captures/phase22d-device-regress/report.md`、Linux `captures/phase22d-linux-regress`)。数値は Phase 22c と同じ:
  Linux の highmark は metronome 33,576 / mp3player 27,208 / hostapi_check 34,728、実機の開始時 free_int 150,264・largest_int 98,304、
  差分は metronome +0 ×3 / mp3player −472 / hostapi_check −176、許容外の WARN / ERROR 0 件。
- 実機は普段使い(`KYBOTOS_DEV_APPS=OFF`)のファームに戻し、検査用アプリ 3 本を `rm` で消した(メニューは 3 本)。

## 残したこと

- スプラッシュ(全面濃緑のロゴ)からメニューに切り替わると背景が黒になる(22a の「地続き」は失われる。指示書で受け入れ済み)。
- マスター設定の帯は青系の配色のまま、実機の状態行の文言(`SD ready`)もそのまま(指示書の「含まない」)。
- メニューのアプリ名は小文字のファイル名(`metronome`)で、アプリの題名(`Metronome`)と表記が違う(指示書の「含まない」)。
- `docs/workflow.md` / `docs/lessons.md`: 反映なし(手順は変えていない。マスター設定の帯の中に注入のタップが当たる件は、帯の座標を `shared/master_ui.h` で見れば避けられる一度きりの手違い)。
