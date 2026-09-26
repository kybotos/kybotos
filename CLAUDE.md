# Kybotos

ESP32-S3-Touch-LCD-2.8 (Waveshare) ベースの音楽デバイスファームウェアと、同じ `.wasm` を動かす Linux ホスト。
「サンドボックス化された WASM アプリを組込みデバイスに配信する音楽プラットフォーム」の
成立性検証 PoC を進行中(旧名 MidiAppBox。コード中の識別子やパスには旧名が残っている)。

## ドキュメント構成(必読)

- **CLAUDE.md(本ファイル)**: 常時従うルールのみ。
- **docs/workflow.md**: herdr/hpane を使った標準開発ワークフローの原本
  (不変条件・推奨手順・ペイン構成・タイムアウト・初回セットアップ)。
  **セッションの最初に必ず全体を読むこと。** herdr/hpane に関する記載は
  本ファイルには置かず、すべてこちらに一本化している。
- **docs/status.md**: 現在地(進捗状況)。
- **docs/roadmap.md**: フェーズ計画の**唯一の情報源**。
  ①フェーズ計画と②フェーズ未割当の課題の 2 部構成。
  **計画に変更が生じたらこのファイルだけを更新する。**
  更新のタイミングは**次フェーズの指示書を書くとき**。
  `docs/results/` に書かれたフェーズ計画はスナップショットであり、書き換えない。
- **docs/architecture.md**: アーキテクチャ方針。
- **docs/lessons.md**: 教訓チェックリスト(herdr/ビルド以外の技術的教訓。
  herdr/ビルド関連は docs/workflow.md 側)。
- **docs/prompts/**: フェーズ指示書(課題定義・完了条件の原本)。
  フェーズ開始時にユーザーが指定するファイルを読み、その指示に従う。
  スコープ変更は本文を書き換えず末尾に「追記 (日付)」節を足す。
- **docs/results/**: Phase 0〜 の調査・計画・実施記録・実測値・トラブルの詳細
  (`docs/prompts/` と対応するフェーズ毎ファイル)。過去 Phase に関わる作業
  (API 変更、メモリ調整、回帰など)の前に該当ファイルを読むこと。

## 言語

- ユーザーとのやりとり(会話・報告・質問)は日本語。
- git のコミットメッセージは英語。

## 開発の進め方(このリポジトリでの作業ルール)

- 「小さいターゲットを定めて、テストし、次を計画する」の反復。各フェーズはビルドが通り
  コミット可能な粒度を保つ。
- 開発環境は **herdr**。シェル実行・ペイン運用のすべては **docs/workflow.md** に従う
  (具体的なコマンド形・不変条件はそちらが原本、本ファイルには重複記載しない)。
- Phase 完了まで、ビルド・フラッシュ・モニタ確認を含めて確認なしで自律的に進めてよい。
  各ステップの結果はログとして **docs/results/ の該当ファイル**に残すこと。
  実施記録の冒頭には対応するフェーズ指示書(docs/prompts/phaseXX.md)への参照を書く。
- 依存追加は最小限に留める。
- 既存アプリ(touch_demo / mp3player / metronome / midi_loopback / seq_smoke)の
  回帰を壊さない。回帰対象は Phase 12 作業 2 で 6 本に絞り(判断根拠は
  docs/results/phase12.md のカバレッジ表)、Phase 14 で clicktest を削除して
  5 本になり(判断根拠は docs/results/phase14.md)、Phase 18 で sequencer を
  足して 6 本になった。sequencer を非公開の app-sequencer へ移したので、この 5 本に
  戻した(docs/results/repo-split.md)。この repo の外のアプリは、この repo の回帰の
  仕組み(`scripts/device-regress.conf` を source した conf を `--conf` で渡す)に載せて
  回す(docs/workflow.md §3.4 / §3.7)。
  (旧「MP3 デモモードで分岐」ルールは Phase 6D で解消済み。履歴は
  docs/results/phase00.md・phase06.md。)
- **フェーズが終わったら、そのフェーズで得た内容を必要に応じて docs/workflow.md と
  docs/lessons.md に反映する。** 手順・環境・不変条件に関わるもの(どのアプリの開発でも使う
  ビルド / 回帰 / 測定 / キャプチャのやり方)は workflow.md、技術的な教訓は lessons.md。
  アプリに依存する内容は workflow.md に書かず、そのアプリの仕様書か docs/results/ に残す。
  反映の要否はフェーズの締めで必ず確認する(反映不要と判断した場合もそれでよい)。
