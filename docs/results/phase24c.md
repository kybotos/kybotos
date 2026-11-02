# Phase 24c 実施記録: CrowPanel の MIDI(J6 の UART1)

- 指示書: `docs/prompts/phase24c.md`
- 状態: **完了(2026-11-02)**。**ソフトの変更なし**。24b から持ち越した 2 項目(一覧のスワイプ、Waveshare の実機の回帰)は処理せず、24d へ持ち越し(指示書の「含まない」)

---

## ステップ 1: 確認(2026-11-02)

**構成**: CrowPanel(V1.2、ネイティブ USB の USB1 で PC とつなぐ)の J6 に、Waveshare と同じ自作の MIDI 回路(ユーザー)。DIN の OUT → UM-ONE の IN、UM-ONE の OUT → DIN の IN。
UM-ONE は PC から `hw:1,0,0`。`KYBOTOS_DEV_APPS=1` のファームを焼き、シリアルの `run midi_loopback` でアプリを起動した。ログは `captures/midi-crowpanel/`(git の対象外)。

**起動ログ**

```
I (1214) MIDI: MIDI OUT ready (GPIO17, 31250bps, TXD inverted)
I (1224) MIDI: MIDI IN ready (GPIO18, RX not inverted)
```

**MIDI IN**(PC の `amidi -p hw:1,0,0 -S "<hex>"` で送り、`midi_loopback` の画面をシリアルの `texts` で読んだ)

| 送ったもの | 画面(受信バイト数 / 直近の 16 進) | 判定 |
|---|---|---|
| `90 3C 64 80 3C 00 B0 07 7F`(Note On / Off + CC) | `FE FE FE FE 90 3C 64 80` / `3C 00 B0 07 7F FE FE FE` | ✅ 順序どおり、欠けなし |
| ランニングステータス `90 40 7F 43 7F 47 7F 40 00 43 00 47 00`(13 バイト) | 2 行目 `47 7F 40 00 43 00 47 00`、受信数 +20(13 + その間の `FE`) | ✅ |
| SysEx `F0 7D` + 299 バイト + `F7`(302 バイト) | 受信数 541 → 845(**+304 = 302 + `FE` 2 個**)、末尾 `… 28 29 2A F7 FE FE` | ✅ 欠けなし、オーバーランなし |
| (何も送らない) | `FE` が約 250ms おき | ✅ UM-ONE の Active Sensing(Phase 8c と同じ) |

- **UART のエラー(`MIDI RX: framing error` / `FIFO overflow` / `ring buffer full`)は 0 件**。
- アプリは 100ms ごとに最大 64 バイトを読むので、SysEx の直後の 1 回目の読み取りでは途中まで(+262)しか出ない。少し待つと全部そろう。
- 1 回だけ、2 行の 16 進が 1 バイトずれて重なった表示を読んだ(1 行目の末尾と 2 行目の先頭がどちらも `47`)。受信数は合っており、
  そのほかの読み取りではずれていない。2 行を別々のタイミングで読んだための見かけと見て、追っていない。

**MIDI OUT**(PC の `amidi -p hw:1,0,0 -d -a -c` で受け、`midi_loopback` の START / STOP をシリアルの `tap` で押した)

| 区間 | PC で受けたもの | 判定 |
|---|---|---|
| 何もしない 3 秒 | 0 バイト | ✅ CrowPanel は `FE` を出さない |
| START → 約 3 秒 → STOP | `FA` 1、`F8` 149、`FC` 1 | ✅ 120bpm で毎秒 48 個の見込みどおり |

- **`amidi -d` は既定で Clock(`F8`)と Active Sensing(`FE`)を表示しない。** 最初は `-c -a` を付けずに受けて `FA` しか見えず、Clock が出ていないように見えた。

**ソフトの変更**: なし。ボードの記述のピン(J6 の 17 / 18)と、`midi.cpp` の極性(TX は反転、RX は反転なし)がそのまま合った。

## ステップ 2: 後片付けと文書(2026-11-02)

- 共用の SD から検査用の 3 本(hostapi_check / midi_loopback / synth_probe)を `rm` で消した(SD のアプリは sequencer / metronome / mp3player)。
- CrowPanel を普段使い(`KYBOTOS_DEV_APPS=0`)のファームに戻した。起動ログで、ホームキー(GPIO1)と MIDI(GPIO17 / 18)が初期化され、メニューが 3 本。
  - 戻すときの最初の書き込みは、`idf.py monitor` がポートを開いたままで失敗した(`device reports readiness to read but returned no data (device disconnected or multiple access on port?)`)。
    CrowPanel はダウンロードモードで止まった。モニタを止めて書き直したら通った。
- Waveshare は触っていない(24a の終わりの普段使いのファームのまま)。
- 文書: README のボードの表(MIDI を足した)、roadmap(24c を done、持ち越しを 24d に)、status、`docs/boards.md`(J6 の「まだ何もつないでいない」を直した)、
  `docs/workflow.md` §3.5(`amidi -d` の `-c -a`)。

## 完了条件の確認

| 完了条件 | 結果 |
|---|---|
| 1. MIDI IN: Note On / Off・ランニングステータス・SysEx 302 バイト、UART のエラーなし | ✅ |
| 2. MIDI OUT: START / Clock / STOP が PC に届く | ✅ |
| 3. 普段使いのファームに戻す、検査用アプリを消す、記録、持ち越しを roadmap へ | ✅ |

## 次の段(24d)への引き継ぎ

24b から持ち越し、このフェーズでも処理しなかった 2 項目:

1. **mp3player の一覧の縦スワイプ**を CrowPanel の指で確かめる。SD の `/music` に 8 曲以上(一覧は 7 行)が要る。
2. **Waveshare の実機の回帰**(回帰 3 本、数値が Phase 24a と同じか)。24b の `audio.cpp` と、24b の追記のホームキー(`power_key.cpp` に実行時の分岐が 1 つ増えた)の後で、
   Waveshare には焼き直していない。ビルドは通る。

これで CrowPanel は、回帰 3 本・メモリ・MIDI・戻る / ホームまで Waveshare と並ぶ(電源断とバッテリーは除く)。残りは上の 2 項目の確認だけ。
