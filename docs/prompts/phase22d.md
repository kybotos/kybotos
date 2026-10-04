# Phase 22d: Kybotos Menu の配色を見直す(黒の地と、ヘッダ右の Settings)

- 契約日: 2026-10-04
- 参照: `docs/results/phase22a.md`(メニューの配色を決めたフェーズ)、`docs/results/phase22b.md` / `phase22c.md`(アプリの骨格と配色)、
  `shared/launcher_theme.h`(メニューの配色。実機・Linux で共有)、`src/components/wasm_runtime/launcher.cpp`(実機のメニュー)、
  `hosts/linux/main.c`(Linux のメニュー)、`docs/design/ui-conventions.md`
- 結果報告先: `docs/results/phase22d.md`

## 目的

Phase 22b / 22c で metronome と mp3player をメニューの配色(ロゴの緑)に合わせ、焼き付きを避けるため本体を黒にした。
並べて見ると、**メニューだけ全面が濃緑で黒の部分が無く**、統一感が欠ける。メニューの背景も黒でよい。

## 決定済みのスコープ(2026-10-04 のユーザー指示)

モックアップ(今 + 案 A〜D、D の派生 D1 / D2)を比べて、**案 D1** に決めた。

- **背景は全面黒。** 題名 `Kybotos Menu`(クリーム)の下に、メニューの中緑 `0x3e6648` の細い線(2px)。
- **アプリの行は暗い緑の地 `0x101a14`(mp3player の一覧の行と同じ)+ 左端に中緑の印(4px)**、文字はクリーム、左寄せ。
  押している間は少し明るい地にする。
- **`Settings` をヘッダの右に移し、若葉 `0x8fd18b` の文字だけで出す**(一覧の行から外す。一覧はアプリだけ)。
  押すとマスター設定が開く(今の `Settings` の行と同じ)。
- 下端の状態行はくすんだ緑のまま(Linux の `click to launch` は `tap to launch` に直す)。
- **実機と Linux のメニューの形を揃える**(今は実機の行が LVGL の既定の角丸のボタンで文字が中央、Linux は左寄せの四角で、形が違う)。

## 前提

- メニューは**ホストが描く**(アプリの `.wasm` ではない)。**Host API / ABI と `.wasm` は変わらない**ので、回帰の highmark は動かない見込み。
- スプラッシュ(全面濃緑のロゴ)からメニューへの切り替わりで、背景が濃緑から黒に変わる(22a で地続きにした点は失われる。受け入れる)。
- マスター設定の入口は、画面上端からの下スワイプ(実機)と `Settings` の 2 つ。`Settings` の当たり判定は行(幅 300px)から文字の周り(約 80 × 26px)に小さくなる。

## ゲート

1. 回帰 3 本(実機・Linux)が PASS(メニューの変更なので、アプリの起動・終了の経路を通すことの確認)。
2. 実機と Linux のメニューのスクリーンショット / 写真で、D1 の形になっていること。`Settings` から マスター設定が開くこと。
3. 公開の repo なので、非公開の repo の中身は書かない。

## スコープ

### 含む

- `shared/launcher_theme.h` の配色の見直し、`launcher.cpp` / `hosts/linux/main.c` のメニューの描き方と `Settings` の当たり判定。
- `docs/design/ui-conventions.md` に、`Settings` の置き場所の変更を書く(該当の記述があれば)。
- `docs/results/phase22d.md`、`docs/roadmap.md`。

### 含まない

- メニューのアプリ名の表記(`metronome` と題名 `Metronome` の違い。アプリの表示名はアプリの情報が要る)。
- スプラッシュの色、マスター設定の帯の配色、スクリーンセーバー。
- 実機の状態行の文言(`SD ready` など)。

## 完了条件

1. 実機・Linux のメニューが D1 の形・配色になっている。`Settings` がヘッダ右にあり、押すとマスター設定が開く。
2. 回帰 3 本(実機・Linux)PASS、`.wasm` の highmark 不変。
3. 記録 `docs/results/phase22d.md`。

## 追記の置き場所

スコープを変えるときは本文を書き換えず、この下に「追記 (日付)」節を足す。
