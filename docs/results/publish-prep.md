# 公開の準備(2026-09-26)

この repo を Kybotos として公開する前に行った変更と確認。前段の repo の分割は `docs/results/repo-split.md`。

## 変更

| 対象 | 内容 |
|---|---|
| ライセンス | **MIT → Apache License 2.0**(`LICENSE` を差し替え、`NOTICE` を追加、`wasm-apps/appui/Cargo.toml` に `license`)。特許の許諾を含み、依存の WAMR / esp-audio-player とも揃う |
| README | Kybotos として書き直した(構成、ビルドと書き込み、Linux ホスト、アプリの作り方、回帰、ライセンス、サードパーティ)。**Host API を使うだけのアプリはこの repo の派生物とは考えない**ことを明記 |
| CI | 旧 repo を checkout して devcontainer でビルドしていたのを、**この repo を checkout して README と同じイメージで `idf.py build`** する形に直した。Linux ホストのビルドと単体テスト、`appui` のテストと wasm32 ビルドも足した。**tag で draft の Release を作るジョブと、成果物のアップロードは外した**(バイナリは配布しない。配布するときは Helix MP3 デコーダーの RPSL の通知が要る) |
| 個人の運用の記述 | 連載記事の運用メモ(`docs/zenn.md`)と、それを指す記述、個人の保存先のパス、イベントの出展予定を外した。`docs/prompts/` / `docs/results/` の中の当時の記述は記録として残す(旧 repo のローカルパス 1 か所だけ伏せた) |
| `CLAUDE.md` | 見出しを Kybotos に(コードの識別子・パスには旧名 MidiAppBox が残る) |

## 公開前の確認

| 項目 | 結果 |
|---|---|
| 全履歴の作者 / コミッター | GitHub の noreply アドレスだけ |
| 全履歴の秘密情報(API キー、トークン、パスワード、秘密鍵の形) | 0 件 |
| 同梱物のライセンス | `font8x8_basic.h` はパブリックドメイン。`assets/*.mp3` はこのプロジェクトで生成したテスト用の音 |
| 依存(repo には入らない) | LVGL: MIT、esp_lvgl_port / esp_lcd_touch / esp-audio-player: Apache-2.0、WAMR: Apache-2.0 WITH LLVM-exception、Helix MP3: RPSL |
| ビルド用のイメージ | 匿名で取得できる |
| 新しい clone でのビルド | CI と同じコマンドで、ファームウェアと Linux ホストのビルドが通る(下の記録) |
