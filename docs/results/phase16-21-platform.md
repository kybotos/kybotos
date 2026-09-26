# Phase 16〜21f のプラットフォーム側の記録(要約)

Phase 16〜21f は Sequencer アプリの開発と並行して進んだ。repo を分割したとき(2026-09-26)に、
**アプリ側の指示書・実施記録は非公開 repo(`kybotos/app-sequencer`)へ移した**。
この文書は、そのフェーズでプラットフォーム(Host API・ホスト・ランタイム・SDK・検証基盤)に入った
変更と判断の根拠をまとめたものである。コードやコメントに出てくる「Phase 19 で 80KB にした」などの経緯は、ここで引ける。

- **全文が公開のまま**のフェーズ: **17 / 21 / 21b / 21c**(ほぼプラットフォームの変更)。
  `docs/prompts/` と `docs/results/` の該当ファイルを読むこと。
- **この文書に要約した**フェーズ: 16 / 18 / 18a〜18d / 19 / 19a / 19b / 20 / 21a / 21d / 21e / 21f。
  公開の記録(Phase 17 / 21 系など)の中に出てくる `docs/results/phase18a.md` などへの参照は、非公開 repo 側の同名ファイルを指す。
  過去の記録は書き換えない方針(CLAUDE.md)なので、参照はそのまま残している。
- 数値は各フェーズの実施記録の実測値。

## フェーズ一覧

| Phase | 主題 | 記録 | プラットフォーム側の変更 |
|---|---|---|---|
| 16 | Sequencer コア(host 非依存)と SL MK3 の PC 挙動 | 非公開 | なし(Host API のギャップ分析 → Phase 17 の要求) |
| 17 | テンポ / 拍子マップの寿命管理と小節境界での停止 | **公開** | Host API(`docs/results/phase17.md`) |
| 18 | Session 画面と単体再生 | 非公開 | Linux の画面キャプチャ、回帰 6 本 + 反復判定、Linux の WAMR プール 96KB |
| 18a | 対話規約(HW ボタンで戻る / 長押し / スワイプ) | 非公開 | **Host API(入力)**、`appui` の新設、電源キーの配線 |
| 18b | ヘッダに操作を集約する | 非公開 | **Host API(色付きテキスト)**、記号の描画、`appui` のシャトル |
| 18c | Session と小節の増減 | 非公開 | **実機の WAMR プール 48KB → 64KB**(U-16) |
| 18d | 小節ごとの拍子と、その編集 | 非公開 | `appui` の 2 軸ドラッグ |
| 19 | Song / Chapter と arrangement 再生 | 非公開 | **プール 80KB**、回帰しきい値の改訂 |
| 19a | Song 画面のタイル表示 | 非公開 | **描画スロットの拡張**、プールの恒久ログ、Linux のプール 192KB |
| 19b | Song タイルの編集 | 非公開 | **`.wasm` バッファを PSRAM へ(U-6)**、プール 96KB、`appui` のドラッグ |
| 20 | 装置全体の永続化 | 非公開 | **Host API(ファイル読み書き)**、プール 112KB |
| 21 | ドラムマシンの土台 | **公開** | プールの PSRAM 化、内蔵音源 SYNTH ポート、ブロックミキサー |
| 21a | ドラムマシンのパターン画面 | 非公開 | `appui` の横スワイプ、プール 128KB |
| 21b | マスターボリュームとマスター設定 | **公開** | 装置設定、マスター設定の UI |
| 21c | ミキサーの MUTE、アプリ側の音量 UI の整理 | **公開** | ミキサー、SYNTH のクリック音 |
| 21d | Drum クリップのバンク化 | 非公開 | スロット拡張、プール 144KB、**検証スクリプトの整備** |
| 21e | Chapter の Drum 列と Song 再生 | 非公開 | rect スロット 80、プール 176KB、**内蔵音源の遅延の測定(U-22)** |
| 21f | Chapter のグリッドから再生 | 非公開 | Linux の記号の追加のみ |

## 1. Host API の語彙の追加

すべて**追加のみで既存の ABI は不変**。追加のたびに、既存アプリの `.wasm` が再ビルドなしで動くことを確認している。
仕様の原本は `docs/hostapi.md`(§7 / §8、決定記録は §11-11)と `shared/hostapi_defs.h`。

| Phase | 追加 | 要点 |
|---|---|---|
| 16 | (なし) | 既存 API で足りるかを H1〜H6 で判定した。足りないのは **H8: テンポ / 拍子マップが再生の始め直しで消えず、長時間の再生で枯渇する(上限 32 件)** と **H9: 小節境界ちょうどで止める手段がない** の 2 点で、Phase 17 の要求になった。**境界同期の locate や拍 / 小節イベントの通知は不要**と判定した(アプリは song tick を単調な演奏タイムラインとして使い、曲構造は WASM 側で解決する) |
| 18a | `HOSTAPI_EV_TOUCH_MOVE = 3`、`HOSTAPI_TOUCH_MOVE_MIN_PX = 8`、`HOSTAPI_KEY_BACK`、任意 export `app_key(key_id, action) -> i32` | イベントの 12 バイト ABI は不変。MOVE は 8px で間引き、末尾の MOVE を畳み込み、DOWN を配送していない MOVE は捨てる。`app_key` が 0 を返すか export していなければ、ホストは従来どおり停止する |
| 18b | `hostapi_draw_text_rgb` | 描画スロットを共有する規則、記号(U+F04B ▶ / U+F04D ■)の扱いを gfx 節に明記 |
| 20 | `hostapi_fs_read` / `hostapi_fs_write`(`"(*~*~)i"`) | 下記 |

**ファイル読み書き(Phase 20)の契約**

- `fs_read`: データルート直下の `path` を先頭から最大 `buf_len` バイト読み、読めたバイト数を返す。ファイルが大きければ切り詰めて成功を返す(ヘッダだけ読む用途のため)。失敗は -1。
- `fs_write`: `<path>.tmp` へ全部書いてから rename する。FATFS の rename は宛先が存在すると失敗するので、**先に `remove` してから `rename`** する(Linux 側も同じ手順)。`remove` と `rename` の間で電源が落ちると、`.tmp` だけが残る(内容は完全なので手で復旧できる)。
- **サンドボックスの境界**(両ホスト同一): データルート(実機 `/sdcard/data`、Linux `./sdcard/data`)直下の**フラットな名前だけ**を許す。長さ 1..63、`/` と `\` を含まない、`..` を含まない、先頭が `.` でない。
- **ストリーム API(open / read / write / close)にしない理由**: アプリをまたぐ状態をホストに持たせると、異常終了のときにハンドルが残る。用途は「10KB 未満のファイルを丸ごと」なので、状態を持つ利点がない。
- アプリごとのサブディレクトリには分けない(v1)。将来分けるときは、ホストがプレフィックスを付けるだけで済み、ABI は変わらない。

## 2. 描画スロットの上限

| | 当初 | 19a | 21d | 21e |
|---|---|---|---|---|
| text | 16 | 32 | **80** | 80 |
| rect | 16 | 24 | 48 | **80** |

- 実機(`hostapi.cpp`)と Linux(`hostapi_sdl.c`)で**同じ値にする**。上限を増やすのは容量を増やすだけなので、既存アプリの再ビルドは要らない。
- **スロットは座標キーで引かれ、自動では解放されない**(`fill_rect` は `(x, y)` で引かれ、`w` / `h` は毎回更新される)。
  使わなくなった矩形は **`w = h = 0` で描いて消す**(19a)。
- `shared/hostapi_defs.h` のコメントは「rect 48」のまま(21e の 80 が反映されていない)。
- 21f の時点の使用数は rect 49 / 80、text 78 / 80。**次に列を足すなら、text の上限の見直しが先**。

## 3. WAMR プールと `.wasm` の置き場所

プールは WAMR がアプリのロードに使う固定のメモリ。**足りないときの症状は 2 段階ある**:
`instantiate: ... allocate memory failed`(ロード自体ができない)と、`create_exec_env failed`(8KB の WASM スタックが取れない)。

| Phase | 実機のプール | 置き場所 | 契機 / 判断 |
|---|---|---|---|
| 〜18b | 48KB | internal | Phase 7B-fix で縮めた値 |
| 18c | **64KB** | internal | `.wasm` 17.5KB で起動しなくなった。48KB に縮めた理由(linear memory の連続確保)は、Phase 15 で linear memory が PSRAM に移ったので成り立たなくなっていた(U-16、`docs/architecture.md` §9) |
| 19 | **80KB** | internal | 64KB では `create_exec_env failed`。**見積もり(残り 1.5KB)は楽観だった。プールは実機で測る** |
| 19b | **96KB** | internal | 同時に **`.wasm` バッファを PSRAM へ移した**(`heap_caps_malloc(MALLOC_CAP_SPIRAM)`、U-6)。`app_tick` は avg 1,207µs / max 1,362µs で、遅くなるどころか安定した |
| 20 | **112KB** | internal | 96KB では instantiate で、104KB では exec_env で失敗した。停止時の `largest_int` が 31,744 → 15,360 と想定外に半減した(静的領域が DRAM バンクの境界をまたいだ) |
| 21 | 112KB | **PSRAM** | 公開記録(`docs/results/phase21.md`) |
| 21a | **128KB** | PSRAM | **プールを広げても `free_int` / `largest_int` が 1 バイトも動かなかった**。internal の天井から外れた最初の実例 |
| 21d | **144KB** | PSRAM | 128KB では instantiate で失敗 |
| 21e | **176KB** | PSRAM | 160KB では exec_env で失敗、168KB で起動した |

- **Linux のプール**: 48KB → 96KB(18)→ 192KB(19a)→ 256KB(21d)。x86_64 では WAMR の構造体が大きいので、**実機の判断とは切り離して広めに取る**。
- **恒久ログ**(19a): 両ホストが `app: wamr pool total=… free=… highmark=…` を出す。天井に当たる前に気づくため。
- **回帰のしきい値**(`scripts/device-regress.conf`): プールが internal にあった間は、拡大のたびに「新しい基準値の 8KB 下」に置き直した。
  `MIN_FREE_INT` 80,000 → 65,000(19)→ 48,000(19b)→ 32,000(20)、`MIN_LARGEST_INT` 32,768 → 24,576(19)→ 8,192(20)。
  PSRAM に移した後(21 以降)の値は `device-regress.conf` を参照。
- **教訓**(詳細は `docs/lessons.md`):
  - プールの消費はバイト数から見積もれない(`.wasm` の増分に対して 2〜5 倍と一定しない)。**必ず実機で測る**。
  - 限界の近くでは「total − highmark」の残りは当てにならない(160KB は計算上 15.5KB 残るのに起動しなかった)。**8KB 刻みで測り、起動した最小値より 1 段大きい値を採る**。
  - プールを動かしたら、**停止時の 4 値**(`free_int` / `largest_int` / `free_psram` と highmark)で確かめる。
  - WAMR の `highmark_size` は、2 回目以降のロードで壊れた値を返すことがある(18)。1 回目の値を使う。

## 4. 入力と対話規約、`appui`(SDK)

規約の原本は `docs/design/ui-conventions.md`(公開)。

- **戻る / ホームへ**(18a):
  - 実機:電源キーの短押しで `app_request_key_back()`。1000ms 以上押して離すと強制的にホームへ戻る(電池運転時は、2000ms の電源断が先に発火する)。
  - Linux:Backspace が戻る、ESC が強制終了。
  - どちらも `app_tick` の切れ目で配送する(ログは `app: key back -> handled|stop`)。
- **Linux の押下判定**(18a): 押下中かどうかは、自分が配送した DOWN / UP で判断する。SDL のボタンマスクを見ると、xdotool の合成クリックで MOVE が出ない。
- **`wasm-apps/appui`**(18a で新設。no_std、依存 0): `gesture`(Tap / 長押し / スクロール)と `stack`(画面スタック、深さ 6)。
  **意味づけはアプリの仕事**で、`appui` は何が起きたかだけを返す。
  - 18b:シャトル(長押し → ドラッグ)
  - 18d:シャトルの 2 軸化
  - 19b:`allow_drag`(opt-in、既定 off)
  - 21a:`allow_hswipe`(opt-in、既定 off、押下 1 回につき 1 度だけ `HSwipe`)
  
  opt-in の機能は、立てていない画面の挙動を 1 ビットも変えない(テストで固定)。
- **記号**: 実機の LVGL Montserrat にある記号(▶ U+F04B / ■ U+F04D / ✕ U+F00D / メトロノーム U+F001 / ⟲ など)は、Linux ホストでは `draw_symbol` で図形として描く。**新しい記号は両ホストで用意する**。

## 5. 検証の基盤(Linux ホストとスクリプト)

手順の原本は `docs/workflow.md`(公開)。

- **Linux の画面キャプチャ**(18): `ffmpeg -f x11grab` は画面全体を読むので黒くなる。**`xwd -id <win>` / `import -window <win>` ならウィンドウの中身が取れる**。ウィンドウは `midibox_host` の pid と `xdotool getwindowpid` の一致で選ぶ。`scripts/screen-still.sh` / `screen-rec.sh` をこの方式に替えた。キャプチャで結果を確かめられるようになったので、xdotool のクリックでの画面遷移の確認も使えるようになった。
- **回帰**(18): 対象を 6 本にした。`REPEAT_RUNS=3` で各アプリを繰り返し起動し、N 回の終了時の `free_int` / `free_psram` がすべて一致することを判定する(U-2:反復によるリークの判定)。
- **スクリプトの整備**(21d): 検証に使ったスクリプトは `scripts/` に置いてコミットする、というルールにした(`docs/workflow.md` §2.3)。
  - `ui-linux.sh`(タップ / 長押し / ドラッグ / キー / 撮影)
  - `linux-regress.sh`
  - `device-pool.sh`(実機のプールの測定)
  - `wav_onsets.py` / `wav_steps.py`(WAV の打点の判定)
  - `wasm_funcs.py`(`.wasm` の関数サイズ)
- **モニタの後始末**(21d / 21e): 手で起動したモニタが残ったまま回帰を走らせない。止めるコンテナは、**コマンドに `monitor` を含むものだけ**に絞る(IDE の clangd が同じイメージで常駐しているため)。

## 6. 内蔵音源の遅延(U-22、21e)

- **測定**: 同じスケジューラで、内蔵の Snare の 250ms 後に DIN OUT の Snare を予約した。オーディオ I/F の L にマイク(内蔵スピーカー)、R に外部音源(Kurzweil ME-1)のラインを入れて録音した(`scripts/v6_latency.py`、`docs/workflow.md` §3.8)。
- **結果**: **内蔵音源が MIDI OUT より平均 25.2ms 遅れて鳴る**(標準偏差 1.6ms、n = 75)。
- **原因**(ソースで追い、実験で確認した):
  - 内蔵音源の経路は、ミキサーのブロック(240 フレーム / 44.1kHz = 5.44ms)の頭でキューを拾い、I2S の **DMA リング(IDF の既定値 6 × 240 = 32.65ms)が常に満杯**なので、書いたブロックはその後ろに並ぶ。
  - リングを 3 に減らした一時ビルドでは、ずれが **−8.0ms** に縮んだ(差 17.2ms。予測は 3 ブロック = 16.3ms)。ばらつきは変わらず、ブロック単位の丸めで説明がつく。
  - MIDI OUT 側は UART 約 1ms + 外部音源の応答(ME-1 は計算上 約 9〜10ms)。**外部音源の応答は機種ごとに違う**。
- **判断**: SYNTH ポートの発音を出力バッファの深さぶん前倒しし、`at_host_us` でブロック内のサンプル位置に置く方向で、**別のフェーズとして扱う**(Host API / ホスト側の変更。roadmap U-22)。
  前倒しを入れるときは、アプリ側の積み直しの下限と前倒しの量の関係を確かめること(21f の申し送り)。

## 7. 外部機器の知見:Novation SL MK3 の Program Change(16 / 19)

- 受信は **ch16**、Bank Select は不要。PC 0..63 は**即時**に Session が切り替わり、**+64 で再生中のパターンの末尾にキュー**される。Session 1 = PC 0。
- 境界ではキューモードを使い、**境界の 960 tick(4 分音符 1 つ)前に `seq_write`** する。Phase 19 で end-to-end を確認した。
- **`transport_start` は L0 のキューを空にする**ので、再生開始時の PC は `seq_write` で積まず、`transport_start` の前に `hostapi_midi_send` で送る。
