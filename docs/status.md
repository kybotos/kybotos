# 現在地

各エントリの詳細は `docs/results/` の該当ファイルを参照。このファイルは
CLAUDE.md から独立して更新する(CLAUDE.md 本体は書き換えない)。
日付ごとの節は当時のスナップショットで、書き換えない。**「(非公開)」を付けたファイルは、
2026-09-26 の repo の分割で非公開の app-sequencer へ移ったもの**(最新の節を参照)。

## 2026-08-23 時点

- メニューのスクリーンセーバー / バックライト消灯(phase 外作業、2026-09-06)。
  メニュー表示中に限り、無操作 60 秒で黒地+漂う図形、180 秒でバックライト消灯。
  タッチ(または電源キー短押し)で復帰し、**復帰のタップはオーバーレイが食う**ので
  アプリを誤起動しない。アプリ実行中は無効。時間は Kconfig で変更可。
  詳細は docs/results/screensaver.md。

- check-workflow(docs/prompts/check-workflow.md)完了。herdr ペイン運用を
  「ラベルごとに別タブ」から「**共有タブ1つに3列×2行で分割配置**」に変更
  (`scripts/hpane.sh` 改修。`run`/`send`/`waitfor`/`read` のインタフェースは不変)。
  撮影スクリプトを `scripts/cam-rec.sh` 等に整備し、出力を `captures/`
  (.gitignore 対象)に集約。Linux ホストの画面キャプチャ(x11grab)・
  クリックによる UI 自動操作は本開発環境(Wayland/XWayland + GNOME)の制約で
  信頼できる形にできず、スコープ外として持ち越し(詳細は
  docs/prompts/check-workflow.md 追記節、docs/results/check-workflow.md)。
- Phase 7(7A 予約発音 / 7B メトロノーム / 7B-fix DMA 二重クリック / 7C トーンパレット /
  7D テンポ1刻み・ボリューム調整)完了。詳細は docs/results/phase07.md。
- Zenn 連載: 第 1〜7 回公開済み、第 8〜17 回はスケジュール公開設定済み(〜2026-07-28、詳細は docs/zenn.md)。
- Phase 8a(docs/prompts/phase08a_midi_out_bringup.md、MIDI OUT 疎通確認)完了。
  自作 MIDI OUT 回路(GPIO18=UART1 TX、2SC1815)を UM-ONE 経由で確認、
  UM-ONE LED 点灯・`aseqdump` で Note On/Off 正常受信を確認。検証専用コードは
  確認後に削除済み(Host API/ABI 変更なし)。詳細・トラブルは
  docs/results/phase08a.md。
- Phase 8b(docs/prompts/phase08b_midi_clock_api.md、MIDI Clock 出力 Host API)
  完了。`hostapi_midi_send` を追加し、メトロノームの START/STOP に MIDI
  Start/Stop を相乗り、host 内部で 24ppqn クロックを生成(実機 UART1・Linux
  は ALSA シーケンサ経由で UM-ONE へ実送信)。テンポ変更時にクロックが暴走する
  不具合を実機検証で発見し、テンポ導出ロジックを「直前発音時刻」基準から
  「直前に受け取った予約時刻」基準に設計変更して解消。詳細は
  docs/results/phase08b.md。
- Phase 8c(docs/prompts/phase08c.md、MIDI IN ハードウェア検証・受信バイト
  ダンプ)完了。TLP2361 受信回路の UART1 RX(GPIO15)を実機検証、外部機器
  (UM-ONE)からの Note On/Off・ランニングステータス・アクティブセンシング・
  SysEx(302バイト)を完全一致で受信、自機 OUT→IN ループバックでも Start/
  Clock(24ppqn)/Stop がバイト落ちなく往復することを確認(IN側直列抵抗は
  220Ωのまま変更不要)。**本 Phase の実装コード(RX 受信ダンプ機能)は
  検証専用のため `feature/midi-in-rx-dump` ブランチにのみ保持し、main には
  マージしていない。** 詳細・トラブル(フローティング入力のノイズ拾い、
  IN回路の接触不良など)は docs/results/phase08c.md。
- Phase 9a(docs/prompts/phase09a.md、MIDI IN 受信 Host API)完了。
  `hostapi_midi_recv(buf_ptr, buf_len) -> n` を追加(16 バイトアラインドの
  `hostapi_midi_recv_t { timestamp_us: u64, byte: u8, _reserved[7] }`、
  `hostapi_poll_event` と同型の out-buffer API)。実機は UART1 RX
  (GPIO15、Phase 8c 検証済み設定)のイベントタスクが受信直後に
  `esp_timer_get_time()` で打刻しリングバッファ(256件)へ積む。Linux は
  ALSA シーケンサ(snd_seq DUPLEX)経由で UM-ONE から実受信(当初案の
  「0件スタブ」からユーザー承認で実受信に変更)。パースは一切行わない
  (Phase 9b の責務)。実機・Linux 双方で UM-ONE からの Note On/Off・
  Clock の実受信とタイムスタンプ単調増加を確認、回帰(free heap 一致・
  WARN/ERROR なし)も確認済み。詳細は docs/results/phase09a.md。
- Phase 9b(docs/prompts/phase09b.md、ループバック診断アプリ)完了。
  `wasm-apps/midi_loopback/` を Stage 1(受信生バイトの16進表示)→
  Stage 2(直近24クロック移動平均からの実測 BPM)→ Stage 3(セッション統計:
  クロック間隔 min/max/σ・公称値との偏差・受信数 vs 期待数、整数 Welford 法)
  の順に実機検証しながら実装。Host API/ABI 変更なし。120bpm ループバック
  実測により、**受信クロック数が期待値を大きく下回る(試験により71〜93%
  程度)現象と、公称間隔の数十〜数百倍に達する巨大な外れ値**を複数回
  (画面無操作の条件でも)確認し、3仮説のうち「(b) クロックの取りこぼし」が
  最も有力と判断(系統的な平均間隔のずれを示す (a) の証拠はなし)。
  **追記(2026-08-17)**: ユーザーが実機配線の接触不良を発見・修正し
  再測定した結果、平均偏差は +183µs、外れ値は公称の約2倍(1回分の
  取りこぼし相当)、clocks/exp は 98.6% まで改善。**当初の大きな外れ値・
  大幅な取りこぼしの主因は配線の接触不良だった**と判断を訂正。修正後も
  残る小さな偏差(BPM 118.22、~1.4%の取りこぼし)の再現性確認が次の
  調査対象。詳細は docs/results/phase09b.md。
- herdr ペイン運用を「共有タブ1つに3列×2行」から「**セッション自身のタブに
  プロンプトを最上段・全幅(既定で高さ35%)、その下2列×3行(左列:
  esp32-build/esp32-monitor/camera、右列: unix-build/zenn/screen)**」に変更
  (`scripts/hpane.sh` 改修。ルートペインの作成元が「新規タブ」から「呼び出し元
  セッションのプロンプトペイン」に変わった以外、`ensure`/`run`/`send`/`waitfor`/
  `read` のインタフェースは不変)。`ensure-all`/`close`/`close-all` コマンドも追加。
  詳細は docs/workflow.md §6.1。
- check-workflow-routine(docs/prompts/check-workflow-routine.md)完了
  (2026-08-23)。上記の新レイアウトで §3.0〜§3.3 を一巡実行し全完走(exit 0 /
  `app_main` 到達 / free heap 開始時と一致 / 既知の1件を除き警告なし)。
  途中で `hosts/linux/build/` が旧リポジトリパスの stale な CMakeCache を
  指していてビルド失敗する事象を発見、対処を docs/workflow.md §3 に追記して
  から再実行し解消。詳細は docs/results/check-workflow-routine.md。
- CLAUDE.md とその周辺ドキュメントを整理(2026-08-23)。`docs/dev-log.md` を
  `docs/results/`(フェーズ毎ファイル、`docs/prompts/` と対応)へ分割・移動。
  「現在地」(本ファイル)・「アーキテクチャ方針」(docs/architecture.md)・
  「教訓チェックリスト」(docs/lessons.md)を CLAUDE.md から分離。herdr/hpane
  関連の重複記載は docs/workflow.md に一本化。**追記**: 当初 `docs/poc-results.md`
  はそのまま `docs/results/poc-results.md` へ移動しただけだったが、Phase 4
  専用の内容なので `docs/results/phase04.md` へ統合(`## 計測結果詳細` 節)し、
  `poc-results.md` は削除。合わせて元の `docs/results/phase01-04.md` は
  `phase01-03.md`(Phase 1〜3)と `phase04.md`(Phase 4)に分割した。
- Phase 12(docs/prompts/phase12.md、基盤整備)完了(2026-09-06)。詳細は
  docs/results/phase12.md。
  - **作業 1 パーティション拡張**: 16MB フラッシュ + `src/partitions.csv`(custom)。
    nvs / phy_init / factory のオフセットは既定表と同一のまま factory を 1MB → 4MB に拡張。
    残容量 4,112 B(0%)→ 3,149,840 B(75%)。NVS 消失なし、`erase-flash` 不要。
    起動警告 `spi_flash: Detected size(16384k) larger than ...` が消えた。
    予約データ領域は「切らない」を採用(パーティションエントリ 1 個 = internal heap 56B の実測に基づく)。
  - **作業 2 アプリ整理**: Host API カバレッジ表に基づき 10 本 → **6 本**
    (touch_demo / mp3player / clicktest / metronome / midi_loopback / seq_smoke)。
    削除は hello / demo / bars / bench(タグ `pre-app-prune`)。hello・bench 専用の
    native ハーネス(呼び出し元ゼロ)も削除。フラッシュ削減は 2,512 B で、
    **容量逼迫の主因は埋め込みではなく Flash Code(634KB)だった**ことを実測で確認。
  - **作業 3 実機テストの自動化**: USB Serial/JTAG のコマンドコンソール
    (`CONFIG_MIDIBOX_SERIAL_CMD`、既定 y。ping / ls / run / stop / heap、応答はタグ `MBCMD`)と
    `scripts/device-regress.sh` を新設。**物理操作なしで 6 本の回帰表が約 100 秒で出る**。
    以後の回帰はこれを既定とする(docs/workflow.md §2.2 / §3.4)。
    待ちはペイン出力ではなくログファイルの差分行に対して行う(スクロールバック誤マッチ対策)。
    副産物として **mp3player の「既知の −44B」は MP3 再生時にだけ出る**ことを特定した。
  - **作業 4 PSRAM 可否**: 判定 **条件付き go**。P10-4 のリブートループの真因は
    **SDMMC プローブ**(ピン競合でも DMA バッファ配置でもない)。飛ばせば PSRAM 有効で
    20/20 連続起動。ただし **PSRAM を有効にしても largest free block は 31,744 のまま増えず**、
    WASM linear memory の逼迫は緩和されない。PSRAM レイテンシ実測は internal 比 +3%
    (キャッシュ内)/ 約 2.5 倍(キャッシュ超え)。本番反映は別フェーズ。
    **main は no-PSRAM 構成**で最終自動回帰に合格(free heap 49160 / largest 31744 / 警告 0)。
  - **重要な運用上の発見**: `allocate linear memory failed` が出たら SD の初期化経路を疑う。
    SDMMC(20.00 MHz)なら largest 31744 で正常、SDSPI フォールバック(11.43 MHz)だと
    15360 に落ちて全アプリが起動しない。**復旧は USB 抜き差しによる電源断**
    (ソフトリセットでは直らない)。P10-4 の「実際に使われるのは SPI 経路のみ」という
    記述はこの実測で訂正した。

- Phase 9c(docs/prompts/phase09c.md、MIDI Clock 送信タイミングの再測定と
  欠落要因の特定)完了(2026-08-23)。`wasm-apps/midi_loopback/` に E1
  (ヒストグラム・ロバスト統計・外れ値・見かけBPM分布)を恒久機能として追加、
  実機で E1(受信側)・E2(送信側、`#ifdef PHASE9C_TXLOG_TEST` 検証専用)を
  同一セッションで3回測定。`Midi_NotifyBeatFired()` の毎拍位相リセット
  (`esp_timer_stop`→`start_periodic`)が120bpmでマージン8µsしかなく、
  クリックスケジューラのジッタで約61%の拍でクロックが1発欠落することを
  送受信双方のデータで確認(「有力仮説」節の全予測値と実測が高精度で一致)。
  E3(`#ifdef PHASE9C_FREERUN_TEST`、周期不変なら再アームしない対照実験)で
  外れ値が3回とも完全に0件になることを確認し、位相リセットが直接の原因と
  実証。E3は暫定検証のみで、検証専用コード(TXLOG/FREERUN/ログ転送フック)は
  全て削除し main の挙動を測定前に復元(`git diff` 差分なしを確認)。
  実機検証中に E2 用の検証専用バッファ(12KB)が ESP32 の一般ヒープを
  圧迫し WASM の "allocate linear memory failed" を誘発する事象を発見・
  解消(教訓を docs/lessons.md に追記)。既存アプリ7種の回帰・Linux ホスト
  起動も確認済み。詳細は docs/results/phase09c.md。
- Phase 10(docs/prompts/phase10.md、新アーキテクチャ先行調査・実装なし)開始
  (2026-08-30)。**P10-1(I2S 再生位置取得の go/no-go ゲート)完了、判定 go**。
  I2S TX の `on_sent` コールバックは無音時もフリーランで発火し(960B=240
  フレーム=5442.2µs 粒度)、打刻間隔 σ0.6〜1.0µs、線形補間誤差は負荷込み
  最悪 ±62µs(基準 ±500µs の 1/8)。既存 click/MP3 経路への干渉なしを
  A/B/A 対照で確認。調査コードは削除済み(パッチは captures/phase10/ に保存、
  P10-2 で再利用)。実機には計測ビルドが焼かれたまま(P10-2 で継続使用)。
  詳細は docs/results/phase10.md。
  **P10-2(実効サンプルレート ppm 計測)完了**: 約 7 分 × 2 回のアイドル計測で
  実効 fs = 44100.0000 Hz、公称比 −0.00 ppm、30 秒窓の変動幅 ≤0.06 ppm。
  I2S と esp_timer が同一 XTAL 系で、分数分周が 44.1kHz の厳密比
  (160MHz × 441/6250 = 256×44100)を達成するため原理的にもゼロ。
  Clock Authority の対応更新は**固定比で足りる**(逐次推定不要)と結論。
  **P10-3(ワンショット再アーム方式のジッタ実測)完了**: 絶対時刻グリッド
  (予定時刻+20833µs)のワンショット連鎖で、アイドル×2・loopback E1×2・
  mp3+タッチ×2 の全条件で**クロック欠落 0・追いつき 0**、TX 発火偏差
  max 104µs、RX 間隔 mean 20833.0µs ちょうど(σ: アイドル 21µs、最悪負荷窓
  ≤114µs)。09c の外れ値 155〜185 件 / BPM 二峰性は完全に消失。設計候補 2
  (ワンショット 1 本の L0 ディスパッチャ)の成立を確認。実機は P10-3
  計測ビルドのまま(metronome の MIDI Clock は出ない状態。次の計測で上書き)。
  **P10-4(メモリ監査)部分完了**: 4 時点のヒープ実測(WASM アプリ実行中の
  largest free block は 13〜14KB = 9c の逼迫水準と一致)、internal 16B ランダム
  アクセス ~360ns/op(mp3 再生中に最大 1385ns/op へ跳ねる回あり)、L0 キューは
  **internal 静的 BSS 4KB = 256 イベント**(4 声部 16 分音符で 2 小節分)が
  現実的な出発点と結論。**PSRAM(8MB octal 搭載)は有効化すると SD 初期化の
  SDMMC プローブで TG1WDT リブートループに陥ることが判明**し、選択肢から除外
  (教訓を docs/lessons.md に追記)。PSRAM 側レイテンシベンチのみ未取得。
  実機は main ビルドへ復旧済み(正常起動を確認)。
  **P10-5(UART リアルタイムバイト割り込み挿入 PoC)完了**: 0xF8 を挟んでも
  ノート列は 24,043 メッセージでエラー 0(完全一致)。TX FIFO 占有量プローブに
  より **`uart_write_bytes` で足り、FIFO 直叩きは不要**(挿入遅延は平均 16µs・
  最悪 640µs)と確定。ノート送出時に見えた σ1.19ms は、ペーシング実装
  (FIFO 占有が証明可能にゼロ)でも同値だったことから**受信側の打刻バッチング
  由来の計測アーティファクト**と切り分け済み(`rx_task` が UART イベント内の
  3〜4 バイトに同一時刻を付けるため。ボーレート既知なので
  `T-(N-1-i)x320µs` で補正可能)。SL MK3 実機でもノート受信中のクロック検知
  テンポが安定(ユーザー目視確認)。ただし SysEx 等の大バーストを一括で
  FIFO に流すと最大 41ms 待たされるため、ポート層は分割送出すること。
- **Phase 10 の調査項目 P10-1〜P10-5 は完了**(P10-4 の PSRAM ベンチのみ
  別途実施)。実機は main ビルドで正常動作中(調査コードは全て削除済み)。
- **Phase 10 の設計書一式を作成し、現在ユーザーレビュー中(未承認)**
  (2026-08-31)。成果物は 2 ファイル:
  - `docs/architecture-next.md`(**後に `docs/architecture.md` へ確定反映し削除**):
    層構成 L0〜L3、Clock Authority(レートマスター = I2S サンプルカウント)、
    tick 座標系(PPQN 960 / u32)、MIDI クロックのグリッド生成、ポート抽象と
    送出規律、メモリ配置方針、Phase 11 以降の移行順序案、**数値根拠表**
    (全定数を P10-1〜5 の実測値に紐付け)。
  - `docs/hostapi-next.md`: Host API 仕様案(`transport_*` / `tempomap_*` /
    `seq_*` / `time_us_to_tick` の全 12 関数)。ABI レイアウト・エラーコード・
    既存 API との関係(残す/非推奨化する)、**アプリ要件突き合わせ表**、
    `shared/hostapi_defs.h` へ取り込むコード片。
  - 検証結果: **5 要件(高精度メトロノーム / 楽曲メトロノーム / SMF インポート /
    2trk シーケンサー+録音 / ドラムマシン)をすべて通しても API 語彙は
    増えなかった**(指示書のレビュー観点を満たす)。
  - **既存の `docs/architecture.md` と `shared/hostapi_defs.h` は未変更**
    (承認前に実装フェーズの作業を始めないゲートを守るため、改訂案は
    別ファイルとして提示している)。
  - レビュー論点は §11 に 5 点を列挙した。特に
    論点 1(ループの tick 表現: L0 のソートキーを単調増加に保つため
    playback tick / song tick の 2 座標に分離した案)。
  - **初版レビュー完了(2026-09-05)**。承認条件とされた 3 点
    (ループ tick 表現のトレードオフ明記 / L0 ディスパッチャのロック規律 /
    レート切替時の Clock Authority 継続規則)を反映済み。論点 2〜5 と
    追加論点(STOPPED 中のクロック挙動)の決定も `docs/architecture.md` §11
    に記録した。
- **Phase 10 の最終回帰完了(2026-09-05)、合格**。
  - **実機 7 アプリ**: free heap は全アプリで開始時と一致(mp3player のみ
    −44B = 9c 以前からの既知挙動)。largest block は全区間 31744 で不変。
  - **P10-3 で観測した midi_loopback の −220B は計測ビルド起因と確定**
    (main では +0B)。**main に恒久的なリークはない**。
  - metronome 実行中に `MIDI RX: ring buffer full` が 239 件出たが、
    **ループバック配線を挿したまま、受信をドレインしないアプリを動かした
    ことによる構成依存の挙動**と特定(256 件 ÷ 24ppqn = 5.33 秒後に初回警告、
    実測 5.3 秒と一致)。main の元コードのままで Phase 10 の変更とは無関係、
    通常使用では発生しない。新アーキテクチャでは L0 が常時ドレインするため
    Phase 11 以降で自然に解消する見込み。
  - **Linux ホスト 7 アプリ**: 全て `app_init=0` / `app started` / `app stopped`、
    警告・エラー 0 行、残留プロセスなし。
  - 詳細は `docs/results/phase10.md` の「最終回帰」節。
- **Phase 10 完了(2026-09-05)。設計を確定した。**
  - 確定反映は 2 段に分けた(ユーザー承認済み):
    1. **実施済み**: `docs/architecture-next.md` の内容を `docs/architecture.md`
       へ反映し、ドラフトは削除した。ドラフト表記を外し、§11 を
       「設計判断の記録」として残してある(**§11-1 の「意図的に放棄した性質」は
       削除しないこと**)。
    2. **Phase 11 のステップ 2 で実施**: `docs/hostapi-next.md` §8 のコード片を
       `shared/hostapi_defs.h` へ取り込む。Phase 10 のゲート「本フェーズでは
       Host API / ABI を変更しない」に抵触するため本フェーズでは行わない。
  - `docs/hostapi-next.md` は**承認済みの仕様**として残す(Phase 11 で
    hostapi_defs.h へ反映したのち、置き場所を整理する)。
- **次のフェーズ(Phase 11)の推奨スコープ**(レビューでの助言):
  - 移行表(`docs/architecture.md` §10)の**ステップ 1〜3 まで**を 1 フェーズとする。
    ステップ 3(metronome を新 API で書き直し → midi_loopback の E1 統計で
    9c と前後比較)が本改訂の価値を初めて実証する地点で、ここまでで
    「クロック欠落 61% → 0」という対外的に語れる結果が出る。
  - ステップ 4〜5(既存経路の置換・`hostapi_midi_send` の副作用削除)は
    挙動変更を含むので**別フェーズに分ける**(回帰の切り分けが楽になる)。
  - **Linux ホストへの同時実装(ステップ 2)は必ず同フェーズ内で行う**。
    遅らせると Clock Authority の抽象が実機都合に引きずられ、Phase A
    (ブラウザ)の移植性という当初の狙いが検証されないまま固まる。
    Linux 側のレートマスターは SDL オーディオコールバックの累計サンプル数で、
    `on_sent` と同型に書けるはず。
- **別途切り出した独立課題**: PSRAM 有効化時に SDMMC プローブがハングする件
  (P10-4)。将来サンプルプレーヤーの波形メモリで PSRAM が必要になった時のため。
- 次の候補: Phase 9c で確定した原因(毎拍位相リセット)を踏まえ、ホスト側に
  音楽時間軸(テンポマップ・拍/小節カウンタ)を持たせるアーキテクチャ刷新
  フェーズの設計。120bpm 以外のテンポでの系統誤差確認、Song Position
  Pointer 等の高度な MIDI 同期はスコープ外として持ち越し。着手はユーザー
  指示待ち。

## 2026-09-06 時点

- **Phase 11(docs/prompts/phase11.md、新アーキテクチャの実装)完了。**
  詳細は `docs/results/phase11.md`。
  - **ステップ 0(設計の穴埋め、承認済み)**: `seq_write` の部分受理を
    **プレフィックス受理 + アプリが残りを保持する契約**で確定
    (`architecture.md` §11-9)。代案「全件受理か 0 か」は、チャンクが空きを
    上回ると**前進しないまま無音になる**うえ「空き件数の照会」という語彙を
    増やす方向に働くため不採用。あわせて、キューの未発火イベントを破棄する
    操作(`transport_locate` / `seq_flush_after` / `transport_stop`)の後は
    アプリ側の未受理分も破棄して `seq_filled_until()` から供給し直す契約を明記。
    未発火 note-off の破棄(鳴りっぱなし)は **v1 はアプリ責務のまま凍結**
    (§11-8。`hostapi_midi_send` が L0 を通らない以上ホスト側の追跡は原理的に
    不完全になるため)。
  - **ステップ 1(L0/L1 を native に実装、既存経路と並存)完了**。静的追加は
    約 4.6KB(L0 キュー 256 件 = 4KB + テンポ/拍子マップ)。Clock Authority は
    I2S TX の `on_sent` をレートマスターにし、固定比換算・アンカー・レート切替時の
    継続規則・ppm 監視を 1 モジュールに閉じ込めた。
  - **ステップ 2(Host API 12 関数、実機 + Linux 同時)完了**。実装の重複を避けるため
    **L0/L1 のロジックを `shared/seq_core.c`(移植可能な C)へ切り出し、両ホストが
    同一ソースを使う**形にした。プラットフォーム依存は 7 個のフック
    (`now_us` / `lock` / `unlock` / `arm` / `disarm` / `send_midi` / `click`)に
    外出ししてあり、ここが Phase A(ブラウザ)への移植点になる。
    Linux のレートマスターは SDL オーディオコールバックの累計フレーム数、
    時刻源は `hostapi_midi_recv` と同一時基の単調増加 µs、ディスパッチは
    `pthread_cond_timedwait`(CLOCK_MONOTONIC。`SDL_AddTimer` は ms 分解能で
    20833µs のグリッドを表現できない)。
  - **12 関数すべてを実機・Linux 双方で検証済み**。`wasm-apps/seq_smoke/` を
    自動一巡する検証アプリに拡張し、**同一の .wasm** で実機 `chk 255` /
    Linux `chk 255`(8 項目すべて合格)。ALSA 経由で採った送出タイミングは
    120bpm: mean **20833.1µs** / σ33.6、180bpm: mean **13888.9µs** / σ32.3 で、
    PLAYING 中のテンポ変更がキュー積み直しなしに効くことも確認。
  - **回帰**: 実機・Linux とも既存 7 アプリに影響なし。実機の `largest block` は
    Phase 10 最終回帰と同じ **31744** のまま(本フェーズを通して一度も縮んでいない)。
  - **ドキュメント整理**: `docs/hostapi-next.md` → **`docs/hostapi.md`** に改名し、
    §8 のコード片は `shared/hostapi_defs.h` への参照に置き換え(二重管理の解消)。
    §3 の `transport_locate` の記述矛盾(「次の start/continue の開始位置」)を修正。
- **スコープ変更(2026-09-06、指示書の追記節)**: ステップ 3(metronome の書き直しと
  前後比較)は **Phase 12 へ移管**。理由は、実機が WASM アプリを 1 つしか動かせず
  「metronome を動かしながら midi_loopback の E1 で測る」が成立しないこと、
  Phase 09 の実装は実用に耐えず「前」の再測定に価値がないこと、アプリパーティション
  残が少ないこと。Phase 12 は**絶対値目標**(欠落 0 / clocks÷expected = 100% /
  BPM 単峰 / 平均間隔 20833µs)で判定し、metronome は別ディレクトリを作らず
  **上書きで書き直す**。
- **申し送り**: アプリパーティション残が 4112B(0%)。実フラッシュは 16MB あるが
  設定が 2MB。Phase 12 で足りなくなったら seq_smoke の埋め込みを外すか設定を
  見直す。また現在 Linux で ALSA が使えない(リモートデスクトップ経由)ため、
  タイミング測定の前に ALSA が使える状態を用意する必要がある。

- **Phase 13(docs/prompts/phase13.md、metronome を新 API で書き直す = 移行ステップ 3)
  完了(2026-09-06)。** 詳細は `docs/results/phase13.md`。
  - `wasm-apps/metronome/` を**音楽時間軸 API(transport / tempomap / seq)だけ**で
    上書き書き直し。旧経路(`hostapi_click_schedule` / `hostapi_tone_schedule` /
    `hostapi_midi_send`)は `extern` から外し、`.wasm` の import にも現れない。
    クリックは `seq_write(port=CLICK / OP_TONE)` で playback tick に予約、
    MIDI Clock は L1 がグリッドから生成する。`.wasm` は 2,885 → 3,910 B。
  - **演奏中のテンポ/拍子変更は「`transport_locate(0)` → `at_tick=0` のエントリを上書き」**
    の即時方式にした(指示書の「次の小節頭に積む」案から変更、承認済み)。理由は
    (a) 異なる at_tick へ積むとテンポマップ(上限 32)が枯渇する、
    (b) 長押し連打が 1 小節に 1 回しか効かず旧版の機能を維持できない、の 2 点。
    旧版の `rearm(now)`(変更した瞬間から小節をやり直す)と同じ意味論になる。
  - **実測(実機 MIDI OUT → UM-ONE → PC、120bpm・4/4、アイドル 5.5 分 ×3)**:
    **クロック欠落 0 件 / clocks÷expected 100.00% / 見かけ BPM 単峰 /
    平均間隔 20832.8µs**。**09c の「約 61% の拍で 1 発欠落」「BPM 二峰性」は消失**。
    3 回中 1 回だけ外れ値 2 件が出たが、「50ms の空白 → 3µs で 2 発」という並びで、
    DIN の 1 バイト時間(320µs)より短い間隔は物理的にありえないため受信側
    (USB/ALSA)の配送アーティファクトと判断(クロック総数は期待値と完全一致)。
  - 負荷条件(長押しでテンポを 120→237→120 と動かし続けた直後の静粛区間 104.5 秒)でも
    **欠落 0 / 100.0% / 平均 20832.9µs**。
  - **測定ツールを新設**: `tools/midi_clock_probe/`(ALSA のカーネル打刻で受信を記録する
    C プローブ + Python 集計)と `scripts/midi-clock-probe.sh`。使い方は
    `docs/workflow.md` §3.5。seq_smoke で妥当性を確認済み(Phase 11 の実測値と一致)。
  - **回帰 PASS**(6 本、free heap 差分 +0、largest block 31744、警告 0)。
    clicktest は旧経路のまま動作。free heap の水準は 49160 → 49136(−24B、全アプリ同値)。
  - **外部機器での確認(SL MK3)**: 検知テンポは安定し一度も外れず、表示値と期待値が一致。
    演奏中に切り替えても追従し、**BPM 下限 40 / 上限 240 の両端も OK**(ユーザー目視)。
  - **スコープ変更(ユーザー判断)**: 条件 B の正規実行・条件 C(演奏中テンポ変更)・
    条件 D(送信側 σ)はスキップ。条件 D 用の検証コードは実装したが測定しないので削除済み。
  - 次の候補は移行ステップ 4 / 4b / 5(旧クリック経路の置換・削除、
    `hostapi_midi_send` の Start/Stop 副作用の削除)。旧経路の残る利用者は
    clicktest と midi_loopback のみ。

- **Phase 14(docs/prompts/phase14.md、旧経路の削除 = 移行ステップ 4b / 5)
  完了(2026-09-06)。** 詳細は `docs/results/phase14.md`。
  - **ステップ 1**: `wasm-apps/midi_loopback/` を音楽時間軸 API へ移行。
    `hostapi_transport_start/stop` + `tempomap_set_tempo/meter`(120bpm 固定)に
    一本化し、`hostapi_click_schedule` / `hostapi_midi_send` を `extern` から
    削除。可聴クリックは供給しない判断(受信統計に条件を絞る)。E1 統計は
    恒久機能のまま維持。実機ループバック測定(自機 MIDI OUT → 自機 MIDI IN、
    120bpm・4/4、約 6.3 分)で**外れ値 0 件 / clocks÷expected 100.00%
    (18255/18255)/ 見かけ BPM 単峰 / 平均間隔 20836µs**(目標 20833±10µs)を
    確認。測定には Phase 9c と同じ手法(画面外センチネル座標 + 検証専用
    シリアルログ転送フック `PHASE14_STATLOG_TEST`)を使い、測定後に削除。
  - **ステップ 2**: `wasm-apps/clicktest/` を削除(回帰対象 6 本 → 5 本)。
    カバレッジの穴が空かないことを確認済み(`docs/results/phase12.md` 追記)。
  - **ステップ 3**: `hostapi_click_schedule` / `hostapi_tone_schedule` を
    `shared/hostapi_defs.h`・両ホストから削除(native 実装・予約状態・
    `Midi_NotifyBeatScheduled/Fired` の呼び出し側を含む)。トーンパレットの
    即時発音(`tone_define` / `tone_play` / `play_click`)は無影響。
  - **ステップ 4**: `hostapi_midi_send` の Start/Stop 副作用と旧クロック
    生成器(`s_clock_running` 等のテンポ逆算状態、`Midi_NotifyBeatScheduled/
    Fired` の定義)を両ホストから削除。**9c の根本原因(テンポの二重管理・
    毎拍位相リセット)がコードから物理的に消えた**。
  - **回帰 PASS**(5 本、free heap 差分 +0、largest block 31744 で不変、
    警告 0)。free heap の水準は削除が進むごとに増加(49136 → 49288、
    esp_timer ハンドル 2 個分の解放)。Linux ホストも 5 本が
    `app_init=0` / `app started` / `app stopped`、警告 0 で起動・終了。
  - フラッシュ使用量: Phase 13 時点 1,048,816 B → Phase 14 完了時
    1,045,984 B(**−2,832 B**)。
  - 検証専用コード(`PHASE14_STATLOG_TEST`)は削除済み、
    `git grep PHASE14 -- src scripts wasm-apps tools` は該当なし。
  - **次の候補**: 移行ステップ 6(内蔵音源のポート追加)。PSRAM の本番反映は
    Phase 12 の「条件付き go」のまま別フェーズ。`hostapi_midi_recv` の
    タイムスタンプ線速補正(docs/hostapi.md §7)も未実施のまま持ち越し。

- **Phase 15(docs/prompts/phase15.md、PSRAM 本番反映 = WASM linear memory の
  PSRAM 移行)完了(2026-09-12)。** 詳細は `docs/results/phase15.md`、
  設計は `docs/design/phase15-psram.md`。
  - **Phase 12 の「PSRAM を有効にしても効果なし」判定は誤りだった。** largest free block が
    31,744 のまま動かなかったのは事実だが、**linear memory がその領域から出て PSRAM へ
    移っていた**からである。当時 `memory_data` のアドレスを見ていなかったため気づけなかった。
  - **採用: A 案 + `CONFIG_SPIRAM_USE_CAPS_ALLOC`**(`sdkconfig.defaults` のみの変更)。
    `CONFIG_SPIRAM=y` にすると IDF のリネーム機構が旧名 `CONFIG_ESP32S3_SPIRAM_SUPPORT` を
    立て、WAMR が `-DWASM_MEM_DUAL_BUS_MIRROR=1` を付け、`os_mmap` が
    `MALLOC_CAP_SPIRAM` を使う。**`managed_components/` の書き換えは不要。**
  - **T-3 達成**: `-zstack-size` を上げた `touch_demo` で linear memory
    **73,840 B**(internal largest 57,344 超)と **8,257,136 B**(PSRAM largest の 400B 下)が
    実機で起動。8,396,912 B は失敗。**上限は PSRAM の最大連続ブロックで決まる**(基準の約 500 倍)。
  - **`--initial-memory` を上げても効かない**(WAMR の `WASM_ENABLE_SHRUNK_MEMORY` が潰す)。
    実サイズは `align8(__heap_base) + instantiate の heap_size`。
  - 実測(アプリ実行中): `free_int` 105,880 / `largest_int` 57,344 /
    `free_psram` 8,316,904。5 アプリとも int・psram 差分 0、WARN/ERROR 0 件。
  - **LVGL 描画バッファは PSRAM へ移る**(自動)。`psram_dma_direct = 1` を立てないと
    spi_master が転送のたび internal に 19,200 B のバウンスを取る(実測 `largest_int` −20,480B)。
  - **回帰の指標を internal / PSRAM の 4 値に改訂**(`app: stopped free_int=… largest_int=…
    free_psram=… largest_psram=…`)。判定を「余裕の監視 = 下限しきい値」と
    「リーク検出 = 差分の厳密一致」に分離した(`scripts/device-regress.{sh,conf}`)。
  - **T-1(metronome の MIDI クロック、2026-09-12)達成**: 実機 MIDI OUT → UM-ONE → PC、
    120bpm・4/4・アイドル約5.5分で **clocks/expected 100.00% / 外れ値 0件 /
    見かけ BPM 単峰 / 平均間隔 20832.9µs**(目標 20833±10µs)。Phase 13 と同じ
    絶対値目標をすべて満たし、PSRAM 有効構成のまま確定した(指示書の「T-1 不合格なら
    PSRAM 無効に戻す」条件には該当しない)。
  - **T-2(app_tick 実行時間、2026-09-12)取得済み**: 最大 27,499µs(100ms tick 周期の
    27.5%)。悪化はあるが不合格条件(周期を脅かす水準)には該当しない。
  - **T-4(20 回連続再起動、2026-09-12)達成**: run1 からやり直し、**20/20 成功**
    (int/psram 差分すべて +0、largest_int 57,344 で不変、警告 0)。
  - **ステップ 3(カメラ目視確認、2026-09-12)完了**: touch_demo / mp3player / metronome
    の一連操作を録画・静止画で確認。色化け・ティアリング・描画欠けなし。
  - **ステップ 4(SDMMC と PSRAM の共存、2026-09-12)完了、結論「SDSPI 固定を受け入れる」**。
    H1(ピン競合)・H4(WDT が SD 以外で発火)・H5(PSRAM 由来バッファが DMA 経路へ)は
    いずれも否定(H5 はソースレベルで再否定: PSRAM 領域には `MALLOC_CAP_DMA` が
    登録されず、SD カードスタックの確保はすべて `MALLOC_CAP_DMA` を明示要求するため
    PSRAM には流れようがない)。**H2(初期化順序)の実機実験で新事実が判明**:
    SDMMC プローブ直前に 300ms 遅延を入れるとプローブ自体は正常に失敗して SDSPI へ
    フォールバックするが、**直後の SDSPI プロトコルネゴシエーション中(CMD5 応答直後)で
    新たに TG1WDT が発火する**。真因は SDMMC プロトコル単体ではなく、
    **同一物理ピンを SDMMC ペリフェラルとして初期化した直後に SPI3 ペリフェラルとして
    再初期化する 2 段階遷移が PSRAM 有効時に不安定になること**。SDSPI 単体
    (現行 main の経路)ではこの問題は一度も発生していない(T-4 で 20/20 実証)。
    将来 SD からの高速転送(SDMMC ネイティブモード)が必要になった場合のみ
    この知見を踏まえて再調査すればよい。
  - **既知の穴(次フェーズへの申し送り)**: **PSRAM のリーク監視は
    「1 回の起動→停止の差分」までで、同一アプリを N 回反復したときの非減少判定
    (4c)は未実装。** linear memory が PSRAM から取られる以上ここは実質的な監視点なので、
    次フェーズで入れること。
  - **main は PSRAM 有効構成で確定**(`CONFIG_SPIRAM=y` + `CAPS_ALLOC`、
    SD は SDSPI 固定)。`docs/architecture.md` §9・§11-2、`docs/lessons.md` を更新済み。

## 2026-09-13 時点

- **Sequencer トラックを開始。** 仕様 Sequencer の仕様(非公開)、フェーズ計画は
  `docs/roadmap.md`(「① フェーズ計画 / ② フェーズ未割当の課題」の 2 部構成に再編。
  以後のフェーズ計画はここだけを更新する)。**MIDI Clock の 115–119bpm 問題はクローズ**
  (Phase 9c〜14 で解決済み・再発なし。roadmap U-1)。

- **Phase 16(docs/prompts/phase16.md(非公開)、Sequencer コアと Host API ギャップ分析)完了(2026-09-13)。**
  詳細は `docs/results/phase16.md`(非公開)。
  - **`seqcore`(非公開)** を新設。Host API に依存しない `no_std` の rlib で、依存 crate は 0 個
    (`heapless` の代わりに自前の `FixedVec`)。データモデル(spec §3)・解決規則
    (`effective_tempo` / `effective_meter`)・Transport 状態機械(Song / Session scope、
    4 通りのトグル、`QueuedAction`)を持ち、小節境界で送るべきもの(Start / Stop / PC /
    テンポ / 拍子)を `BarEvents` として値で返す。**`cargo test --features std` 34 passed、
    wasm32(no_std)ビルド成功。**
  - **スコープ変更**: SL MK3 の実機実験は削除(公開仕様どおりとユーザーが確認)。
    **ch16 の PC 0..=63 は即時、+64 で再生中パターンの末尾へキュー。** Session 境界は
    キューモードで送り、`PC_LEAD_TICKS = 24`(24ppqn = 4 分音符、暫定)。
    再生開始時の PC は `transport_start` がキューを空にするため `hostapi_midi_send` で先に送る。
  - **メモリ**: Bank 9,842 B(ホスト = wasm32 で一致するよう長さを u8 で持つ)。上限定数は据え置き。
    `Option<Session>` の niche で `Bank::new()` が非ゼロになり `.wasm` の .data を太らせる
    ことをテストで見つけ、空きを `bars == 0` で表す形にした(教訓を `docs/lessons.md` に追加)。
  - **Host API ギャップ**: H1–H6 は既存 API で足りる。**新規は H8(テンポ / 拍子マップが
    `transport_start` で消えず、上限 32 件で長時間再生すると枯渇する)と H9(境界同期の停止)。**
    拍・小節イベントの通知と境界同期の locate は不要(song tick を単調なタイムラインとして使う)。
    Phase 17 の方式案 A / B / C を比較し **B(`tempomap_clear` + 通過済みエントリの剪定 +
    `OP_STOP`)を推奨**。
  - ファームウェア・Linux ホスト・`shared/`・既存 5 アプリ・Host API は変更していない(回帰不要)。
  - **次**: Phase 17(Host API の追加、承認ゲート)の指示書作成。

- **Phase 17(docs/prompts/phase17.md、テンポ / 拍子マップの寿命管理と小節境界での停止)完了(2026-09-13)。**
  詳細は `docs/results/phase17.md`、決定記録は `docs/architecture.md` §11-10。
  - **Host API を追加(方式 B'、承認済み)**:
    - `hostapi_tempomap_clear()`: STOPPED 中のみ使え、マップとループを空にする。
    - **満杯時の畳み込み**: PLAYING 中にマップが満杯になったときだけ、通過済みの区間を 1 件に畳む。小節番号の起点を保持する。
    - `HOSTAPI_SEQ_OP_STOP`: 指定 tick で停止し、その tick のクロックは出さない。
  - **既存アプリの挙動は変わらない**(マップが満杯にならないため)。
    同じアプリで再生を始め直すときは `stop → clear → 初期値 → start`。
  - **検証(すべて合格)**:
    - Linux の C 単体テスト `hosts/linux/tests/seq_core_test.c`(偽の時計、`ctest`)**10/10**
    - `seqcore_selftest` は実機・Linux とも PASS
    - seq_smoke(8 → 12 項目)は実機・Linux とも **chk 4095**
    - **V3 境界停止**: 最終区間のクロックがちょうど 288 発、Stop 後 0 発(両ホスト、probe と aseqdump)
    - **V4 metronome**: 100.00% / 外れ値 0 / 単峰 / 20832.8µs
  - **回帰**: 実機 5 本 PASS(差分 +0、largest_int 57,344 不変、警告 0)、Linux 5 本も警告 0。
  - **ツール**:
    - `midi-clock-probe` に「区間ごと / 停止中のクロック数」を追加
    - `device-regress.conf` の seq_smoke 保持時間を 60 秒に変更
  - **次**: Phase 18(Session 画面 + 単体再生)の指示書作成と、そのときの `docs/roadmap.md` 更新。

- **Phase 18d(docs/prompts/phase18d.md(非公開)、小節ごとの拍子とその編集)完了(2026-09-20)。**
  詳細は `docs/results/phase18d.md`(非公開)。
  - **データモデルを変えた**: `Session.meter`(既定)+ `bar_meter`(変化点の上書き)をやめ、
    **`meters: [TimeSig; 16]` = 各小節が自分の拍子を持つ**形に。`Bank::new()` は全ビット 0 のまま。
    `Session::new` は「全小節をその拍子にする」意味になり、`meter_at` / `set_meter_at` を追加。
    `effective_meter` は残して委譲(**範囲外は最後の小節の拍子**に意味が変わった)。
  - **編集**: **拍子の表示を長押し + 上下左右ドラッグ**(左右 = 分子 1〜16、上下 = 分母 2/4/8/16、
    **16px で 1 段** = BPM の「速さ」とは別の「位置」)。**再生中の Session は編集不可。**
  - **表示**: Bar 一覧は**全小節に拍子**、Session 一覧からは拍子を外した(名前を 10 文字に)。
    `+` で足した小節は**直前の拍子を引き継ぐ**。
  - `appui` の `Shuttle` を **2 軸(dx, dy)** に。テスト **19 件**。`seqcore` は **56 件**。
  - **検証(Linux)**: 4/4 → 3/4 → 3/8 → 3/4、別の小節を 6/4、`+` の引き継ぎをキャプチャで確認。
    **MIDI は 96+72+144+96+96 = 504 発ちょうど・停止後 0 発**。
  - **サイズ**: `.wasm` 17,851 B(+354)、実機プール消費 49,584 / 65,344(**残り 15,760 B**)。
  - **回帰(18b・18c・18d ぶん)をシリーズ末にまとめて実施 — 実機 6 本・Linux 6 本とも PASS**
    (2026-09-20、`captures/phase18-regress/`)。差分 +0、`largest_int` 40,960 で不変、WARN/ERROR 0。
    **プール拡大で基準値が変わった**(free_int 105,832 → **89,368**、largest_int 57,344 → **40,960**。
    しきい値まで 9.4KB / 8.2KB)。実機のタッチ確認(18b〜18d の操作感)は未実施。

- **Phase 18c(docs/prompts/phase18c.md(非公開)、Session と小節の増減)完了(2026-09-20)。**
  詳細は `docs/results/phase18c.md`(非公開)。**編集機能の方針転換の 1 歩目**(spec §1.3 を改訂)。
  - **行頭に `-`、最後の要素の次の行に `+`。`-` の長押し(点滅 → 離す)で削除、`+` のタップで追加。**
    **できないとき(再生中の Session・上限・最後の 1 小節)は記号を出さない。**
  - `seqcore` に `insert_bar` / `remove_bar`(**拍子の上書きをシフト**)/ `free_slot` /
    `remove_session` / `session_mut` を追加。テスト 44 → **51 件**。
  - **roadmap U-16 に着手: WAMR プールを 48KB → 64KB に戻した(承認済み)。**
    `.wasm` が 17,497 B になり 48KB では **`create_exec_env failed`** で起動しなくなったため。
    Phase 7B-fix で縮めた理由(linear memory の連続確保)は **Phase 15 で PSRAM へ移って消えていた**。
    結果: プール消費 48,704 / 65,344(**余裕 16,640 B**)。代償は internal の静的 +16KB で
    **free_int 89,368 / largest_int 40,960**(しきい値まで 9.4KB / 8.2KB)。
  - **`.wasm` 17,497 B で 16KB 超**。roadmap **U-6 が現実の課題**になった(申し送り)。
  - **検証**: Linux で追加・削除・点滅(画素判定)・拍子上書きのシフト・再生中の編集不可を確認。
    MIDI は **S02 = 720 発 / 小節を足した S01 = 480 発、停止中 0 発**。
    実機は 64KB プールで sequencer ほか 4 本が起動・反復 3 回 PASS。
    **実機のタッチ操作(削除・追加)はユーザーが確認済み**(2026-09-20、指摘なし。映像は残していない)。
  - **回帰は 18b とあわせて未実施**(ユーザー指示。Phase 18 シリーズの最後にまとめて回す)。

- **Phase 18b(docs/prompts/phase18b.md(非公開)、ヘッダに操作を集約する)完了(2026-09-20)。**
  詳細は `docs/results/phase18b.md`(非公開)。18a の試用で出た 3 点を直した。
  - **パンくず(ヘッダ左)のタップで 1 階層戻る。** 最上位では何もしない(アプリを終了させるのは HW キーだけ)。
  - **BPM は Tempo 画面をやめ、ヘッダ右の長押し + 左右ドラッグ(シャトル)**で変える。
    **変位が「速さ」**(12px で 1bpm/秒、40px で 4、80px で 12)。変更中は文字が黄色で点滅する。
  - **下段 3 ボタンを廃止し、ヘッダ下に 24px のステータス行**を置いた。`1` / `RPT` は**文字色**で ON/OFF、
    再生 / 停止は **▶ / ■**。他の画面では `1` / `RPT` を暗く落とし、再生中だけ ■ を出す。
  - **Host API を 1 つ追加**: `hostapi_draw_text_rgb`(**同じ座標は `draw_text` と同じスロット**を共有。
    既存 `hostapi_draw_text` は不変なので既存 5 本の `.wasm` は再ビルド不要)。roadmap **U-15 の文字色に着手**。
    記号は実機のフォントにある **U+F04B / U+F04D** を使い、**DejaVu に無い Linux ホストは図形で代替**する。
  - `appui` に**シャトル**(`Action::Shuttle` / `ShuttleEnd`)を追加。テスト 16 → **18 件**。
    18a の「長押し成立後のスワイプは取り消し」は「**成立後のドラッグはシャトル**」に改めた。
  - **`.wasm` 15,671 B(+416)なのに実機 WAMR プールの消費は 44,736 B(−792)、残り 4,224 B。**
    プールはバイト数に比例しないので毎回実測する(教訓)。
  - **検証**: Linux で 17 枚のキャプチャ(パンくず戻り / 文字色 / ▶ ■ / シャトル 120→133→127)、
    S02 = **720 発ちょうど・停止後 0 発**、`no free slot` 0。`appui` 18 件 PASS。
    **回帰はユーザー指示により今回は実施せず**(Phase 19 で必ず回す)。
    実機のタッチ操作は**ユーザーが確認済み**(2026-09-20、指摘なし。映像は残していない)。

- **Phase 18a(docs/prompts/phase18a.md(非公開)、対話規約の確定と Sequencer への適用)完了(2026-09-20)。**
  詳細は `docs/results/phase18a.md`(非公開)、規約の原本は **`docs/design/ui-conventions.md`(新設)**、
  決定記録は `docs/architecture.md` §11-11。
  - **デバイス共通の対話規約を決めた**: **HW キー短押し = 1 階層戻る / 最上位でアプリ終了**、
    タップ = 主アクション、**長押しは成立で点滅 → 離して実行**、**縦スワイプ = スクロール**、
    HW キー 1〜2 秒 = 強制ホーム(脱出路)。**遷移やスクロールのためだけのボタンは置かない。**
  - **Host API を非破壊に拡張**: `HOSTAPI_EV_TOUCH_MOVE`(8px 間引き + 末尾 MOVE の畳み込み)と、
    任意 export **`app_key(key_id, action) -> i32`**(戻り値 0 = ホストの既定動作 = 停止)。
    **既存 5 本の `.wasm` は未変更のまま従来どおり短押しで終了**(実機で metronome を確認)。
    Linux ホストは **Backspace = 戻る / ESC = 強制終了**(回帰・キャプチャ手順は不変)。
  - **`wasm-apps/appui/`(新設)**: ジェスチャ判定と画面スタック。no_std・依存 0・**テスト 16 件**。
  - **sequencer を刷新**: `BACK` / `OPEN` / スクロールボタンを廃止し、**7 行 + 下段 3 セル**
    (`1` / `RPT` / `PLAY`)。テンポはヘッダ右タップで入る **Tempo 画面**(縦スワイプ = 8px で 1bpm)。
    描画スロットは **rect 12 / text 12**。**`.wasm` 15,255 B**(+436 B)。
  - **検証**: Linux は xdotool + キャプチャで全操作(点滅は画素値で判定)、**S02 = 720 発ちょうど・停止後 0 発**。
    実機はユーザー操作を録画(435 秒)し、T1〜T10 すべて合格(`app: key back -> handled/stop`、`app: forced home`)。
    **回帰は実機 6 本・Linux 6 本とも PASS**(差分 +0、WARN/ERROR 0)。
  - **申し送り**: **実機 WAMR プールの残りが 3,432 B / 48,960 B**。`.wasm` +436 B に対しプールは +2,312 B
    増えた(限界費用は約 5 倍)ので、**Phase 19 では roadmap U-16(プールの拡大)を先に片付ける**。

- **Phase 18(docs/prompts/phase18.md(非公開)、Session 画面と単体再生 = Sequencer app の初回 `.wasm`)完了(2026-09-13)。**
  詳細は `docs/results/phase18.md`(非公開)。
  - **`sequencer`(非公開)**(`.wasm` 14,819 B)
    - **3 画面**: Menu / Session 一覧 / Session 画面。描画スロットは全画面で同じ座標を使い回し、rect 14 / text 14
    - **再生**: Play / Stop、トグル `1` / 矢印、再生中小節の点滅、行の長押しジャンプ(Q5)、一覧で BPM±
    - **時間軸**: 計画は **`seqcore::timeline::Planner`**(新設、seqcore のテスト 34 → 44)。先読みは次の 1 小節、変更の締め切りは 150ms
    - **自然終了**: 小節境界の `OP_STOP` で止まる
  - **実機でユーザー操作 T1〜T8 を録画で確認、全合格。** S02 全小節 1 回のクロックは **720 発ちょうど、停止中 0 発**(実機・Linux とも)。
    ユーザーの指摘(一覧で 7/8 の小節に入ったことが見えない)を受け、ヘッダ右に今の小節の拍子を出すようにした。
  - **Linux の画面キャプチャが取れるようになった。** x11grab ではなくウィンドウ ID 指定の `import -window` / `xwd -id`。
    `scripts/screen-still.sh` / `screen-rec.sh` を切り替えた。xdotool のクリック + キャプチャで画面遷移も確かめた。
  - **WAMR プール**: sequencer のロードに実機で 43.2KB / 48KB(**余裕 5.7KB**)、Linux(x86_64)で 58.8KB。
    **Linux だけプールを 96KB にした。** Phase 19 で実機のプールが足りなくなる見込み(申し送り)。
  - **回帰**: 6 本(sequencer を追加、`CLAUDE.md` も更新)。**U-2(同じアプリを 3 回反復して終了値が同一)を `device-regress.sh` に入れて PASS。**

- **Phase 19(docs/prompts/phase19.md(非公開)、Song / Chapter と arrangement 再生)を実装中(2026-09-21)。
  実機が使えない回だったので、Linux までで止めてある。** 詳細は `docs/results/phase19.md`(非公開)。
  - **画面**: Menu の `Song` を有効化し、**Song 一覧 → arrangement → Chapter → Session 画面**を追加
    (スタックの深さ 5。Session 画面は Session ルートと同じ画面で、パンくずだけが変わる)。
    描画スロットは **rect 10 / text 12 のまま**(一覧 4 画面の行を共通化した)。
  - **再生**: `play_song` を配線し、**Session 境界の PC をキューモード(+64)で 4 分音符 1 つ前に予約**。
    Linux の実測で **PC は `ch16 program 0` → Start → `68` → `64` → Stop、クロックは 972 発ちょうど・停止後 0 発**。
  - **`PC_LEAD_TICKS` の単位が違っていたのを直した**(24 → **960**)。Phase 16 は 24ppqn のつもりで定義したが、
    利用者は内部 PPQN(960)の tick として使うため、**PC が境界の 12.5ms 前にしか届いていなかった**。
  - **編集**: Song / arrangement の枠 / Chapter 内の Session 参照を `-` / `+` で増減。
    **arrangement の `-` は孤立した Chapter 定義も消して `ChapterIdx` を詰める**、
    **Chapter の `-` は参照だけ消す**(Session はグローバル)。**再生中の Song は編集不可**。
  - **テンポ**: arrangement / Chapter 画面では `Song.default_bpm` を停止中だけシャトルで編集できる。
  - **テスト**: seqcore **67 件** / appui **20 件** PASS。**Linux 回帰 6 本 ALL-PASS**。
  - **WAMR プールを 64KB → 80KB にした(ユーザー承認済み、2026-09-21)。**
    実機では 64KB だと **instantiate だけで highmark 55,200 / 残り 10,144 B** を使い、
    `create_exec_env`(8KB スタック)が取れず **起動しなかった**。80KB では
    **highmark 63,464 / 残り 18,264 B** で起動する。代償は internal の静的 +16KB で、
    **free_int 89,368 → 73,052 / largest_int 40,960 → 31,744**。
    **回帰のしきい値を `MIN_FREE_INT=65000` / `MIN_LARGEST_INT=24576` に下げた。**
  - **実機回帰 6 本 PASS**(2026-09-21。全行の差分 +0、反復も同一値、WARN/ERROR 0)。
  - (前夜の見立て)**⚠ WAMR プールがゲートに抵触する見込み**: `.wasm` 17,851 → **22,752 B**(一度 25,619 B まで増えたのを
    3 つの手当てで圧縮)。**Linux のプール消費 86,776 B / 96KB** で、**実機は約 63.8KB / 65,344 = 残り約 1.5KB**
    の見積もり(ゲートは 2KB)。**次セッションはまずフラッシュして実測する。**
  - **SL MK3 との end-to-end 完了(2026-09-21)。** 実機 MIDI OUT → SL MK3 直結、Clock は External。
    **SG02 で Session 1 → 5 → 1、SG01 で 7 回すべて期待どおり**に切り替わり、
    **切り替わる位置は実機の小節の頭とそろっていた**(テンポ 120 → 100 と 3/4・7/8 を含む)。
    → **`PC_LEAD_TICKS = 960`(4 分音符 1 つ)は妥当**と確認(spec §4.2 の未検証の前提を消化)。
    デモ動画 6 分 13 秒(`~/ビデオ/zenn-phase19/phase19-slmk3-e2e.mp4`)。Song の 3 画面は実機のタッチでも操作できた。
  - **未実施 / 持ち越し**: **Q6(カウントイン)はユーザー判断「まだ判断できない」で持ち越し**、
    **U-6(`.wasm` を PSRAM へ)は未着手**、**1 回だけ最後の 1 小節が鳴らなかった事象(再現せず)は要監視**。

- **Phase 19a(docs/prompts/phase19a.md(非公開)、Song 画面のタイル表示)を実施中(2026-09-21)。**
  詳細は `docs/results/phase19a.md`(非公開)。
  - **arrangement の画面を横 4 × 縦 2 のタイルにした**(演奏中に見て触るメイン画面)。
    1 枚に**左上の `✕` + Chapter 名**と**その Chapter の合計小節数**。9 枚目以降は縦スワイプ。
  - **タップ = ジャンプ**(再生中は次の小節境界、停止中は開始位置の選択 → ▶ でそこから)、
    **`✕` の長押し = 削除**、**それ以外の長押し = 掘り下げ**(再生中も可)。
    **再生中の枠は背景が拍に同期して点滅**し、画面外に出たら追う(手動スクロール後は枠が変わるまで追わない)。
  - **roadmap U-15 に着手**: 描画スロットを **text 16 → 32 / rect 16 → 24**(両ホスト同値、既存 `.wasm` は不変)。
    **`✕` は U+F00D**(実機フォントにあり、Linux は図形で代替)。**Linux のプールは 96KB → 192KB**。
    **両ホストに「WAMR プールの残り」の恒久ログ**を足した(天井に当たる前に気づくため)。
  - **`fill_rect` は `(x, y)` キーで `w`/`h` を毎回更新するので、`w = h = 0` で消せる**ことを使って、
    行とタイルが同じ領域を取り合う問題を解いた。
  - seqcore にジャンプ(`Change::Jump`)・**開始位置つき再生**(`play_song_at`)・
    **合計小節数**(`slot_bars`)を非破壊で追加(テスト 67 → **71 件**)。
  - **`.wasm` 22,752 → 25,298 B**、**実機プール 69,344 / 81,728(残り 12,384 B)**、
    停止時 free_int 72,700 / largest_int 31,744。
  - **デモ曲 SG03「Good-bye on the road」を追加**(実機の SL MK3 の Session 17〜23。
    Intro A B C Inter2 A B C D E C Coda Repeat = **13 枠 / 92 小節 / 112bpm**。
    13 枠なので常用でタイルのスクロールと追従が効く)。
  - **クリック(メトロノーム)の ON/OFF をステータス行(▶ の左隣)に追加**。
    緑 = ON / 灰 = OFF、どの画面でも操作でき、**効くのは次の小節から**。
  - **Chapter の中の Session を選び直せるようにした**(タイル長押しで中に入り、
    **Session の行を `-` 以外で長押し** → 一覧からタップ)。
    **差し替わるのはその参照 1 つだけ**だが、**同じ Chapter を指す枠はすべて変わる**(参照の原則)。
    ※ 一度「タイル長押し = 選び直し、Chapter 画面は廃止」と取り違えて実装し、revert してやり直した。
  - **締め(2026-09-21)**: `.wasm` **26,465 B**、実機プール **71,488 / 81,728(残り 10,240 B)**、
    スロット **text 29/32・rect 18/24**、テスト **seqcore 71 / appui 20**、**回帰 6 本 PASS(実機・Linux)**。
  - **残課題**: **arrangement に枠を足す `+` の入口が無い**(次フェーズで決める)、
    実機のタッチ感の詰め(✕ の帯、スクロールの重さ)、クリック OFF を今の小節から効かせるか。

- **Phase 19b(docs/prompts/phase19b.md(非公開)、Song タイルの編集)完了(2026-09-21)。** 詳細は `docs/results/phase19b.md`(非公開)。
  - **編集を「長押しで入る編集モード」に統一した。** **通常時のタイルに ✕ は出ない**。
    長押しで**そのタイルが点滅し続け ✕ が出て**、**✕ = 削除 / 名前 = 名称変更 / ドラッグ = 並べ替え /
    本体 = Session リスト**。**空白・他タイル・HW キー**で抜ける。**再生中は編集モードに入れない**
    (長押しは従来どおり Chapter 画面)。
  - **`+` タイル**(最後の次)から **新規 Chapter** か **既存のシャドー**(同じ定義への参照)を追加。
    **名称はプリセット 16 個 + ダッシュ**(`A` / `A'` / `A''`、`CLR` で 0 に戻る)。
    **同名は同じオブジェクトに束ねる**(名前を既存のものに変えると参照が付け替わり、孤立した定義は消える)。
  - **並べ替えはドラッグ**。**その枠の TempoTrigger と選択カーソルが一緒に動く**。
    実測: 並べ替え後の再生で PC の順が新しい arrangement どおり(Intro の次が 81 → 82)。
  - **U-6 を実施(`.wasm` バッファを PSRAM へ)+ プール 96KB**(先に余裕を作ってから機能を積んだ)。
    プールの残り 10,240 → 26,624 B、`app_tick` は avg 2,332 → 1,207µs で悪化なし。
    **最終: `.wasm` 31,352 B / プール 81,560 / 98,112(残り 16,552 B)**、
    停止時 free_int 56,316 / largest_int 31,744、しきい値 48,000 / 24,576。
  - **appui に `allow_drag` + `Drag` / `DragEnd`**(既定 off で既存アプリ不変、テスト 20 → 25)、
    **seqcore に `move_arrangement` / `chapter_with_name` / `retarget_arrangement` / `chapter_bars` / `Name::from_bytes`**
    (テスト 71 → 77)。
  - **回帰 6 本 PASS(実機・Linux)**。
  - **残課題**: **実機のタッチ感は未確認**(編集モードの長押し、ドラッグ、✕ の帯)、
    ドラッグはページをまたげない、クリック OFF を今の小節から効かせるか、永続化は Phase 20。

- **Phase 20(docs/prompts/phase20.md(非公開)、装置全体の永続化)完了(2026-09-21)。**
  詳細は `docs/results/phase20.md`(非公開)。
  - **Menu に `Save File` / `Load File` を追加**し、**Bank(全 Session + 全 Song)を SD カードの
    8 スロット**(`BANK1.MBB` 〜 `BANK8.MBB`)に保存 / 読み込みできるようにした。
    **起動時の自動ロードもオートセーブもしない**(明示操作のみ。回帰の初期状態を SD に依存させないため)。
  - **Host API を 2 つ追加**(非破壊、承認済み): **`hostapi_fs_read`**(先頭から最大 `buf_len` バイト。
    **ヘッダ 36 B だけ読んで一覧を作る**)と **`hostapi_fs_write`**(一時ファイル + rename)。
    データルートは **実機 `/sdcard/data` / Linux `./sdcard/data`** で、**アプリが指せるのはその直下の
    フラットな名前だけ**(`/` も `..` も不可)。ディレクトリはホストが作る。**既存 5 本の `.wasm` は不変**。
  - **ファイル形式は `seqcore::serial` の `MBBK` v1**(36 B ヘッダ + 明示レイアウト + チェックサム)。
    **構造体のメモリダンプはしない**。**壊れたファイルは一時 Bank で弾いてから差し替える**ので、
    現在の曲を失わない。テスト **seqcore 77 → 92 件**。最悪サイズ 8,588 B、デモデータは **813 B**。
  - **UI**: 8 スロットの一覧(ラベル = 先頭 Song 名 / Song 数 / バイト数)。
    **空きへの保存は 1 タップ、上書きと読み込みは長押し**、**再生中は `Load File` を出さない**。
    手引きはステータス行、結果(`saved` / `loaded` / `no card` / `bad file`)はヘッダ右。
    **描画スロットは増えていない**(rect 18 / text 29)。
  - **Linux で検証済み**: 保存 → 編集 → 読み込みで戻る、**プロセスをまたいでも戻る**、
    壊れたファイル(`bad file`)・書き込み失敗(`no card`)で落ちず Bank も無傷。
    **MIDI: PC 0 → 68 → 64(キューモード +64)、区間内クロック 1,164 発ちょうど**で、
    **読み込んだ構造(S01 = 5 小節)どおり**に鳴ることを確認。
  - **メモリ(ゲート 3 に触れたので報告 → 承認 → 実施)**: `.wasm` **31,352 → 39,383 B**、
    プール消費 **101,384**。**96KB では instantiate が失敗、104KB でも `create_exec_env failed`** のため
    **プールを 96KB → 112KB に拡大**(残り 13,112 B)。代償で停止時の基準値が
    **free_int 56,316 → 39,900 / largest_int 31,744 → 15,360** になり、しきい値を
    **`MIN_FREE_INT=32000` / `MIN_LARGEST_INT=8192`** に改訂した。
  - **回帰 6 本 PASS(実機・Linux)。** 既存 5 本の Linux プール消費は 19a / 19b と同値で、
    Host API の追加が既存アプリに影響していないことの裏づけになっている。
  - **実機の手元確認もユーザーが一巡して OK**(電源を切って入れ直してから Load で編集が戻る)。
    **19b から持ち越しだったタッチ感も指摘なしでクローズ。**
  - **残課題**: ホスト間のファイル互換は未確認(SD を PC に差し替える物理操作が要る)、
    `.tmp` の掃除はしていない、**Song 一覧側の編集は U-20 のまま**。

- **Phase 21(docs/prompts/phase21.md、ドラムマシンの土台)完了(2026-09-21)。**
  詳細は `docs/results/phase21.md`。**画面は作らない回**で、21a(パターン画面)の前提を 2 つ用意した。
  - **WAMR プールを PSRAM へ移した**(ユーザー承認済み。大きさは **112KB のまま**)。
    internal 固定の理由(7B-fix)は **Phase 15 の PSRAM 化で既に失効していた**。
    停止時の基準値は **`free_int` 39,900 → 154,520 / `largest_int` 15,360 → 106,496 /
    `free_psram` 8,316,904 → 8,202,204**。**プール消費は 101,384 → 101,200 でほぼ不変**。
    しきい値を **`MIN_FREE_INT=146000` / `MIN_LARGEST_INT=98304` へ引き上げた**(初めて上がった)。
    **`MIN_FREE_PSRAM` は 8,000,000 のまま**(余裕 202KB あり、先回りして緩めない)。
    **同一ファームの A/B で `app_tick` avg 1,391(internal)→ 1,387µs(PSRAM)= 悪化なし。**
    → **これ以降、`.wasm` の伸びしろは internal の天井に縛られない。**
  - **内蔵音源ポート `HOSTAPI_PORT_SYNTH` を実装(roadmap U-5、移行ステップ 6 完了)。**
    **Host API の関数は 1 つも増えていない** — アプリは `seq_write(port=SYNTH, status=0x9n,
    data1=note, data2=velocity)` を積むだけ。**既存 5 本の `.wasm` は再ビルドしていない。**
    **Note Off と velocity 0 は無視**(打楽器はワンショット。**21a のイベント数が半分**)、
    **チャンネルも無視**、**note は GM ドラム準拠の 36/38/42/49**、**同時発音 8**。
  - **実機のオーディオを「1 音を書き切る直列再生」から「常時 1 ブロック(240 フレーム)を書く
    ミキサ」に作り替えた。** **7B-fix の DMA ゼロ埋めは撤去**(常時書き込みでアンダーフローが
    起きないため、対策の前提ごと消える)。**CLICK ポートはミキサのボイス 1 本に載せ替え**
    (語彙・意味は不変)。副次的に**クリックとドラムが重なっても両方鳴る**。
  - **音は合成**(ノイズ + 減衰サイン、xorshift32 + 再帰振動子)。
    **`voice_render()` の中身だけ差し替えればサンプル再生になる**境界にしてある。
  - **検証用アプリ `wasm-apps/synth_probe/` を追加**(**回帰 6 本には入れない**)。
    タップ不要で 4 音同時 / 16 分ハイハット / クリック重ね / ボイス溢れ、
    さらに **MP3 の再生と停止**まで自動で通す。**Linux は `MIDIBOX_WAV_OUT` で WAV に落として機械判定。**
  - **Linux の実測(WAV)**: 4 音同時のピーク **19,839**(単音の最大 10,000 を超える = 加算されている)、
    帯域は低 1336 / 中 273 / 高 66(ハイハット単独は 0.3 / 1.2 / 20.8)で **3 音が同時に乗る**、
    **16 分の間隔は平均 124.9ms**(公称 125.0)でドリフト無し、**クリップ 0 サンプル**。
  - **V4(MIDI Clock)PASS**: `synth_probe` で測定(**ミキサが 8 ボイスで動く最も厳しい条件**)。
    **100.00% / 15,353 発 / 停止後 0 発 / 平均 20832.9µs / 単峰 / 外れ値 0 件** —
    Phase 13・17 の目標どおり。**V5(クリックの互換・二重クリック)もユーザー確認で問題なし。**
  - **回帰 6 本 PASS(実機・Linux)。** Linux の highmark は 6 本とも Phase 20 と同値。
  - **MP3 との受け渡しで不具合を 2 件見つけて直した**(どちらも常時書き込みで初めて届いた経路):
    (1) **`reconfig_rate` がチャネルを無効のまま放置**し、以後**無言で永久に音が出なくなる**
    (`i2s_channel_disable` の失敗で即 return していた)。(2) **`audio_player_get_state()` は
    停止後 `PAUSE` のまま IDLE に戻らない**ので、それを見ていた抑止ガードが永久に止まる。
    → **I2S 専用ミューテックス / 再構成の堅牢化と自己修復 / 自前の占有フラグ / 失敗の WARN 出力**。
    さらに**受け渡しを無音経由**にした(ボイスを消して DMA リング 1 周ぶんの無音を書き切ってから譲る)。
  - **残課題**: **R-1 切替時にたまに無音 / 「ピッ」**(ユーザー判定「**実害は無いので一旦このまま**」。
    通常の演奏では再現しない)、**R-2 V4 の metronome との同条件比較が未実施**、
    **R-3 V6(内蔵音と MIDI OUT のずれ)が未測定**で案 A のまま、
    **R-4 MP3 を実際に再生すると internal が 36〜44B 減る**(回帰では検出されない)、
    **R-5 `i2s_channel_preload_data` は 1 ディスクリプタしか埋められない**。

- **Phase 21a(docs/prompts/phase21a.md(非公開)、ドラムマシンのパターン画面)完了(2026-09-22)。**
  詳細は `docs/results/phase21a.md`(非公開)。
  - **Menu に `Drum Machine`** を足し、**Song のタイルと同形の 4 列 × 2 行**で
    ドラムパターンを打ち込めるようにした。**1 画面 = 1 拍(16 分 × 4)**、
    **横スワイプで拍、縦スワイプでサウンドの組**(Kick/Snare ↔ CHH/Crash)。
    **描画スロットは 1 つも増えていない**(rect 18 / text 29。Song のタイル座標を使い回した)。
  - **操作**(ユーザー回答で原構想どおり領域分けにした): **タップ = 試聴**、
    **上の帯(26px)の長押し = ON/OFF**、**本体の長押し + 上下ドラッグ = ベロシティ**
    (位置、16px で 8)。**本体の長押しをドラッグせずに離しても ON/OFF**。
  - **`seqcore` に `drums` モジュール**: `Pattern`(4 サウンド × 8 小節 × 16 ステップ = **513 B**、
    **0 が OFF** で全ビット 0 のまま = `Bank` は `.bss` のまま)、**1 ステップ = 16 分音符**
    (`steps_in_bar = num * 16 / den`。7/8 は 14)、**`bar_for` で「Session より短いパターンを
    開始小節から繰り返す」写像**を先に用意(紐付けは次フェーズ)。
    **`Scope::Pattern { bars }`** で単体ループ再生(Song / Session の経路は触っていない)。
  - **`appui` に `allow_hswipe`**(`allow_drag` と同じ **opt-in、既定 off**)。
    **立てていない画面の判定は 1 ビットも変わらない。**
    `docs/design/ui-conventions.md` の §2(横スワイプの解禁)と §3.5(**タイルを「格子」として使う**)を改訂。
  - **テスト**: seqcore **107 件**(92 →)、appui **30 件**(25 →)。
  - **`PEND_MAX` 40 → 96**(1 小節 最大 82 件。**Note Off が要らないので 1 打点 = 1 イベント**)。
    **L0 キューは 256 のまま**。
  - **WAV で機械判定**(`MIDIBOX_WAV_OUT`): Kick が step 0/8、Snare が 4/12、HH が偶数ステップ、
    2 小節でループ。**ベロシティも効いている**(Kick 110 / 100 の低域比 **0.906** は設定値の比 0.909 と一致)。
  - **`.wasm` 39,383 → 43,705 B**。**112KB のプールでは `create_exec_env failed` で起動せず**、
    **承認を得て 128KB にした**(実測の限界費用は **2.60 倍**で、見積もりの 2.42 倍では足りなかった)。
    プール消費 **112,424 / 130,880(残り 18,456)**。
  - **プールを 16KB 増やしたのに `free_int` 151,880 / `largest_int` 102,400 は不変。**
    Phase 21 で PSRAM へ移したので **internal の天井から外れた**ことの最初の実例
    (減ったのは `free_psram` の 16KB だけで、**しきい値の変更は不要**)。
  - **V4(MIDI Clock)PASS**: `synth_probe`(4 声部 + MP3 を挟む)で
    **100.00% / 20832.9µs / 単峰 / 外れ値 0 件 / 停止後 0 発**。
  - **回帰 6 本 PASS(実機・Linux)。**
  - **ステータス行の当たり判定を記号に合わせて直した**: クリックの記号は x = 252 なのに
    判定が 260 で切れており、**マークの右半分をタップすると ▶ / ■ に入って
    「メトロノームが切れない」**状態だった(全画面共通の不具合)。
  - **実機の試用所感を D-1〜D-18 として記録**(`docs/results/phase21a.md`(非公開))。
    **最重要は D-1「内蔵スピーカーは低域が出ないので Kick が聞こえない」** —
    **Linux の WAV では判定できない種類の問題**だった。

- **Phase 21b(docs/prompts/phase21b.md、マスターボリュームとマスター設定)完了(2026-09-22)。**
  詳細は `docs/results/phase21b.md`。**ホスト側だけの回で、`.wasm` は 1 本も変えていない。**
  - **音量を「装置の設定」にした。** `hostapi_audio_reset()` が**アプリの起動 / 停止のたびに
    98 へ戻していた**のをやめ、**アプリを切り替えても持続**するようにした。
    **既定値は 98 → 50**(HW のつまみを最大にするとドラムに対して大きすぎたため)。
    **シグネチャは不変**で、**アプリが `hostapi_audio_set_volume` を呼ぶとマスターが動く**
    (mp3player の V− / V+ はそのまま生きる)。契約は `shared/hostapi_defs.h` / `docs/hostapi.md`。
  - **マスター設定を「上から降りてくる不透明の帯」として実装**した。**画面遷移ではない**ので
    **下のアプリはそのまま動き、再生も止まらない**。**アプリの描画スロットは 1 つも使わない**
    (実機は `lv_layer_top()`、Linux は描画の最後)。
  - **入口は「画面の上端から下方向へのスワイプ」+「ランチャーの `Settings` 行」。**
    座標変換が画面外を 0 にクリップするので、**`y <= 3` で始まった押下 = 画面外から来た**と
    表せる。**その押下は結論が出るまでアプリへ渡さない**(ABI に cancel が無く、
    先に渡すと長押しが誤発火するため)。**上端 3px だけなので通常の操作は遅れない。**
  - **ジェスチャ・状態・座標・当たり判定は `shared/master_ui.c` に 1 本化**した
    (Phase 11 の「同じロジックを二重に書かない」)。**描画だけホストごと。**
  - **ミキサー(ポート単位の音量)をスコープに追加**(実機の試聴で
    「Drum は 50、mp3 は 18 がちょうどよい」となったため)。
    **実効音量 = マスター × チャンネル**で、チャンネルは **MP3 / Drums / Click**。
    **既定は Master 50 / MP3 35 / Drums 100 / Click 100**(MP3 の実効 17.5)。
    **ユーザーの試聴で「バランスは OK」「操作感は非常に良い」。**
  - **`−` / `+` は 1 刻み、長押しで連打加速**(metronome の BPM± と同じ実績値)。バー付き。
  - **回帰**: 実機 22 項目 / Linux 6 本 PASS(ミキサー追加後はユーザー判断で省略)。
    実機プール消費は **112,424 で Phase 21a と同値**(ホスト側の変更なので WAMR を食わない)。
  - **実装中に 2 つ踏んだ**: (1) **保留の判定で最初の 8px で諦めていた**
    (Phase 18a の「デッドゾーンで押下を終わらせない」と同じ罠)、
    (2) **重ねた UI の下のボタンが押せてしまった**(透明で CLICKABLE な全画面キャッチャで解決)。
  - **残課題**: **B-1 設定が電源で消える**(保存は U-24 へ。チャンネルが 4 つになり価値が上がった)、
    B-2 metronome / mp3player が音量の初期値 98 をキャッシュしていて表示がずれる、
    B-3 ランチャーでは上端スワイプが効かない(`Settings` 行があるので実害なし)、
    B-4 ブラウザ版では上端スワイプが使えない見込み。

- **Phase 21c(docs/prompts/phase21c.md、ミキサーの MUTE と、アプリ側の音量 UI の整理)完了(2026-09-23)。**
  詳細は `docs/results/phase21c.md`。**「音量と鳴らす / 鳴らさないは Settings で決める」に一本化した。**
  - **ミキサーの各行に MUTE**。**ラベルそのものがトグル**(箱付き。緑 = 鳴る / 灰 = MUTE、値とバーも灰)。
    **MUTE は値と別のフラグ**でアンミュートで元の値に戻る。**`master_ui` がホストへ実効値(MUTE 中は 0)を渡す**ので
    ホストのゲイン計算は不変。**既定で MUTE なのは Click だけ**。「Drums」→「**Synth**」に改名。
  - **`hostapi_audio_set_volume` を `master_ui` 経由にした**(Master の MUTE を素通りしないように。
    Linux の既存不具合 — アプリの音量変更で MP3 のミキサーゲインが外れる — も消えた)。
  - **SYNTH に note 33 Metronome Click / 34 Metronome Bell を追加**(非破壊。Host API の関数は増えない)。
    Snare の描画経路を流用した「減衰サイン + 4ms のノイズ」、Click 1,200Hz / 60ms、Bell 2,000Hz / 150ms。
  - **アプリの手直し**(`.wasm` を再ビルド。無変更ソースでのバイト一致を先に確認):
    **sequencer** はメトロノームボタンを削除(click は常に積み、Click MUTE が決める。▶ / ■ の的を x 234 まで拡大)、
    **metronome** は音を SYNTH の 34 / 33 へ移して Vol / V− / V+ を削除、**mp3player** は vol / V− / V+ を削除。
    **metronome の拍の音は楽器(Synth)扱いで、Click の設定は効かない**(ユーザー判断)。
  - **検証**: Linux の画面と WAV で V1〜V10 PASS(Click 既定 MUTE で sequencer の click 0 発 → アンミュートで鳴る、
    Synth MUTE でドラムと metronome が消える、34 / 33 は 2,000 / 1,200Hz で完全に分離)。**実機はユーザーが試聴して OK**。
  - **回帰 6 本 ALL PASS**(実機 22 項目 / Linux)。既存 3 本の highmark は不変、新基準は mp3player 19,112 /
    metronome 22,952 / sequencer 153,288(Linux)。実機の sequencer プール残り **20,696 B**(21b は 18,456)。
  - **残課題**: C-1 touch_demo / seq_smoke / synth_probe のクリックが既定で鳴らない(**roadmap U-25** に起票)、
    C-2 設定が電源で消える(U-24)。
- **Phase 21d(docs/prompts/phase21d.md(非公開)、Drum クリップのバンク化と Session と同じ枠組みの編集画面)完了(2026-09-23)。**
  詳細は `docs/results/phase21d.md`(非公開)。**Phase 21 を「Drum Track」のフェーズと定め直した最初の回**(ゴールは Drum を Song に組み込むこと。
  21e = Chapter の Drum 列と Song 再生、21f = Chapter のグリッド画面)。
  - **`Bank.drums`(1 本)を Drum クリップ D01〜D32 に**。小節ごとに拍子、1 小節は最大 **24 ステップ**(5/4・12/8・6/4 まで。
    7/4 は選べない = 4/4 + 3/4 に分ける運用)。**拍子を減らしても打点は消さずに隠す**。
  - **Menu の `Drum Machine` → Drum 一覧 → 小節一覧 → ステップ編集**。**小節一覧は Session の Bar 一覧と共通**
    (タップ = 選択、**長押し = 編集状態(抜けるまで点滅)→ ドラッグで拍子、ドラムは再タップでステップ編集**)。
    `+` のクリップは真上と同じ拍子。単体再生は **`1` / `RPT` を含めて Session と同じ**。
  - **ステップ編集は 4 × 4 のタイル**(全サウンド)、**タップ = ON/OFF**、ページは小節の頭で切る、
    ステータス行に**小節のインジケータ**(今の小節の ■ に拍の番号)。
  - **保存形式 `MBBK` v2**(v1 も読める。最悪 33,741 B)。seqcore のテスト **92 → 125**。
  - **承認を得て拡大**: 描画スロット **rect 24 → 48 / text 32 → 80**(両ホスト)、**実機 WAMR プール 128KB → 144KB**
    (残り 18,640 B)、Linux 192KB → 256KB。`.wasm` 43,549 → **52,720 B**(見積もりの倍)。
  - **21a からの潜在不具合 2 件を修正**(`PEND_MAX` の溢れ、ドラム再生中に S01 が再生中扱い)。
  - **検証**: Linux の WAV で 3/4・12/8・5/8・4/4 + 3/4 が小節の長さどおり(抜け 0、ずれ最大 6ms)、全ステップ ON の 12/8 で欠け無し、
    v2 保存 → 読み込み、v1 読み込み。**実機はユーザーが試用して OK**(拍の番号と編集状態の点滅の 2 点をその場で直した)。
  - **回帰 6 本 ALL PASS**(実機・Linux)。既存 5 本の Linux highmark は不変、sequencer は 182,280。
- **Phase 21e(docs/prompts/phase21e.md(非公開)、Chapter の Drum 列と Song 再生 + Chapter のグリッド画面)完了(2026-09-23)。**
  詳細は `docs/results/phase21e.md`(非公開)。**ステップ 0 の報告後、ユーザーの指示で 21f の「Chapter のグリッド画面」を前倒しした**(21f に残るのは Chapter 単位の再生)。
  - **Chapter が Drum の列を持つ**(`{clip, repeat}` の並び、休符 = `0xFF`、最大 16 要素)。**Song 再生で Control(Session の列)の拍子にそろって鳴る**
    (Control の各小節で、クリップの対応する小節の先頭 `min(両者のステップ数)` 個)。Pattern / Session scope の動きは不変。
    **クリップを消すと、参照していた要素はその長さの休符になる**(後ろがずれない)。
  - **Chapter のグリッド**: 行 = Control の小節(Session の最初の小節の行に `- S01`)、右に Drum の列のマス
    (Chord / Bass / Melo の 3 列ぶんは空けてある)。**空きマスのタップ = クリップを置く、置いたマスのタップ = 差し替え / `(none)` で外す、
    長押し → 点滅 → 上下ドラッグ = リピート**。リピートの切れ目で塊を分け `x2` を出す。ステータス行に `Drum 7/8bar`(列 / Control)。
  - **保存形式 `MBBK` v3**(v1 / v2 も読める。最悪 37,965 B)。seqcore のテスト **125 → 149**。
  - **デモ**: D06(1 小節の 8 ビート)、SG02 / SG03 に Drum 列、**SG04「Meters」**(3/4・7/8・4/4・5/4 の混在と、Session の境界をまたぐ Drum。ユーザーの追加指示)。
  - **承認を得て拡大**: 描画スロット **rect 48 → 80**(両ホスト)、**実機 WAMR プール 144KB → 176KB**(160KB では `create_exec_env failed`。
    highmark 148,280)。`.wasm` 52,720 → **62,445 B**。Linux プールは 256KB のまま(消費 213,448)。
  - **検証**: Linux の WAV で **SG04 が 67 / 67 打点一致**(3/4 → 7/8 → 4/4 のまたがり、12/8 のクリップが 20 / 12 / 12 / 12 ステップに切られる)、
    SG02 の 7/8・5/4 とジャンプ、v3 保存 → 読み込み、v2 / v1 読み込み。**実機はユーザーが試用して OK**(グリッドの操作性もよい)。
  - **U-22 の V6 を測り、原因を突き止めた**: **内蔵音源が MIDI OUT(→ ME-1)より約 25ms 遅い**(ばらつき 1.6ms)。
    **主因は出力の DMA リング(6 × 5.44ms)の深さ**で、リングを 3 にした一時ビルドで**予測どおり 17ms 縮む**ことを確認した。
    SL MK3 + ME-1 と重ねた SG03 で**ユーザーが遅れを聞き取り、ブラインドの聴き比べで早めた版を「ジャスト」と判定**。
    **対策(SYNTH の発音の前倒し + ブロック内の位置合わせ = 案 A)は別フェーズ**(ユーザー決定)。
  - **回帰 6 本 ALL PASS**(実機・Linux)。既存 5 本の Linux highmark は不変、sequencer は 213,448。
  - **21f への申し送り**: Chapter のグリッドに `1` / `RPT` を置いて再生(`1` = Chapter。ON なら Chapter を 1 回 / リピート、
    OFF なら Song の最後まで / Song をリピート)、再生中の Drum の編集。
- **Phase 21f(docs/prompts/phase21f.md(非公開)、Chapter のグリッドから再生する + 再生中の Drum 編集)完了(2026-09-23)。**
  詳細は `docs/results/phase21f.md`(非公開)。
  - **Song のトグル**: `Scope::Song { song, chapter(= arrangement の枠), repeat }`。arrangement とグリッドに **`1` / ⟲**
    (`1` ON + ⟲ ON = Chapter ループ、`1` ON = Chapter を 1 回、`1` OFF = その枠から最後まで、⟲ ON なら **Song の頭**へ戻る)。
    ループの境界は同じ Session でもキュー PC を送る。ジャンプするとループの対象も移る。**spec Q7 を決着**。
  - **`1` / ⟲(U+F079、旧 `RPT`)は全画面で ▶ の左**(ユーザー指示。Linux ホストに ⟲ の図形を追加 = 承認済み)。実機の試用で ⟲ が ▶ に近すぎたので離し、間に反応しない帯を入れた。
  - **再生中も Drum のマスを編集できる**(次の小節を積み直す)。グリッドの**追従**。**再生中の変更で次の小節の PC が変わると古い PC が残る潜在バグ(19a から)を直した**。
  - `.wasm` 62,445 → **64,771 B**、実機プール 176KB のまま(highmark 155,088、168KB でも起動 = 余裕 8KB 超)、Linux 221,440。text スロット 76 → 78 / 80。seqcore のテスト **149 → 160**。
  - **検証**: Linux の WAV で SG04 の Chapter ループ 3 周 **118 / 118**、Song リピート **91 / 91**、Chapter 1 回 **33 / 33**、再生中の編集 **133 / 133**(対象の小節の 0.15〜0.27 秒前に確定して 1 周目から鳴る)。
    Linux の MIDI で PC の送り直し(時刻前 / 送信後 / 締め切り後)を確認。
    **実機はユーザーが一部を試用し OK(SL MK3 の動作は時間の都合で見ていない。残課題 R-1)**。
  - **回帰 6 本 ALL PASS**(実機・Linux)。既存 5 本の Linux highmark は不変。

## 2026-09-26 時点

- **repo を分割し、Sequencer アプリを非公開の app-sequencer へ移した。** 詳細は `docs/results/repo-split.md`。
  - この repo に残るのは、仕様(Host API)・ホスト(ESP32-S3 / Linux)・SDK(`appui`)・サンプルアプリ・検証スクリプト。
    Sequencer(`sequencer` / `seqcore`)と、そのフェーズ(16 / 18〜20 / 21a / 21d〜21f)の指示書・記録は app-sequencer にある。
    プラットフォーム側の経緯は `docs/results/phase16-21-platform.md`。
  - **ファームへの埋め込みを表から回す形にした**: 公開アプリの一覧 + CMake 変数 `KYBOTOS_EXTRA_APPS` から
    `embedded_apps.c`(seed の表)を生成し、launcher はそれを回すだけ。この repo の外のアプリも同じ口で載る。
  - **回帰は 5 本**(touch_demo / mp3player / metronome / midi_loopback / seq_smoke)。回帰スクリプトは外のアプリを
    足して回せる(`--conf`、`APP_WASM`、`device-regress.sh` の `--build-dir` / `--mount`)。
  - 確認: ESP32-S3 / Linux のビルド、Linux の回帰 5 本(highmark は Phase 21f と同じ)、実機の回帰 5 本
    (停止時 `free_int` 150,312 / `largest_int` 102,400 / `free_psram` 8,136,668 = Phase 21f と同じ)。
  - **新しい clone では managed component の版が上がる**(LVGL 9.5.0 → 9.6.0~1 など)。そのままだと停止時の値が
    `free_int` −32 B / `largest_int` 98,304(しきい値ちょうど)になる。lock は固定せず最新の版に追従する(ユーザー判断、roadmap U-29)。
