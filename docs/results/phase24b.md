# Phase 24b 実施記録: CrowPanel の音と SD(回帰 3 本 PASS)

- 指示書: `docs/prompts/phase24b.md`
- 状態: **ステップ 0 承認済み(2026-10-10)。ステップ 1 へ**

---

## ステップ 0: 確認と設計(2026-10-10)

### 0-a 実機で鳴らす

一時のコード(`PHASE24B-TEMP`)で、`Audio_Init` の I2S の初期化の直後に IO21 を ON の水準(LOW)にした普段使いのファーム(`KYBOTOS_DEV_APPS=0`)を CrowPanel に焼いた。
**mono の MP3 の I2S の設定は既定のまま**(`I2S_STD_SLOT_LEFT`)。外付けスピーカー、音量は既定の 50。ログ `captures/phase24b/amp.log`、録画 `captures/phase24b/cam_rec_161017.mp4`。
読み終えた後、一時のコードは `git checkout` で外した(`git diff` 0 行)。

起動ログ: `amplifier enable pin GPIO21: off` → `T24B amplifier enable pin GPIO21: ON`(10ms 後)→ 起動音。

シリアルから鳴らした順: 起動音(書き込み後のリセットと、モニタを開いたときのリセットで 2 回)→ `run metronome`、▶ で約 4 秒 → `run mp3player`、`tap 300 38`(▶)で `test.mp3`(**44.1kHz mono**)を約 7 秒 → 一時停止 → `stop`。

| 音 | 耳(ユーザー) | カメラのマイクの rms(0.5 秒のビン。部屋の地の音は約 −40 dB) |
|---|---|---|
| 起動音(ミキサ、L = R) | 聞こえた | 17.0 秒と 21.5 秒に約 −13 dB(2 回の起動) |
| metronome のクリック(ミキサ) | 聞こえた | 28〜33 秒に −28〜−34 dB(短い音なので 0.5 秒の平均では低く出る) |
| **mono の MP3**(I2S を MONO・`SLOT_LEFT` に組み直す) | **聞こえた** | **42〜49 秒に −4〜−16 dB** |
| ポップ音(アンプを入れた瞬間、MP3 の開始 / 終了、停止) | 気にならない(鳴っていないかもしれない) | — |
| 無音のときのノイズ(ヒス) | 無い | — |
| 音量 | Waveshare より大きい(スピーカーが違う) | — |

- **P4 の懸念は当たらなかった**: ESP32-S3 は `I2S_SLOT_MODE_MONO` + `I2S_STD_SLOT_LEFT` でも、R のスロットにも音を出している(R を鳴らす NS4168 で mono の MP3 が鳴った)。
  I2S の設定は変えなくてよい(Waveshare とも同じまま)。
- 内蔵音源の全 note(`synth_probe`)は検査用アプリなので普段使いのファームに入っていない。回帰用のファームで焼くステップ 2 で鳴らす。

### 0-b 設計メモ(案。承認を求める)

**a. アンプの有効化 — 起動時に I2S が動き出してから 1 回だけ ON にし、つけたままにする**

- `Audio_Init`(と `Audio_Click_Init`)で、`amp_init_off()`(OFF の水準で出力にする)→ I2S の初期化 → **ON の水準**。起動音はその後に鳴る。
  ポップ音もヒスも気にならなかったので、無音の間に止める制御は入れない(止め / 入れのたびのポップのほうが心配)。
- `PIN_AMP_EN` が無いボード(Waveshare)は何もしない(今の `amp_init_off` と同じ早期 return)。**Waveshare の経路は変わらない**ことを、`audio.cpp.obj` の逆アセンブルと回帰の数値で確かめる。
- ログは `amplifier enable pin GPIO21: on`(Phase 24 の `off` の行を置き換える)。

**b. mono の MP3 — 変えない**(0-a のとおり R にも出ている)。ボードの記述にスロットの定数は足さない。

**c. SD — Waveshare と同じ SDSPI の設定のまま**(周波数も同じ)。確かめ方は、hostapi_check のファイルの読み書き(回帰に含まれる)と、
MP3 を最後まで続けて再生して途切れないこと(ユーザーの耳 + ログの警告)。8 曲以上を置いた一覧の縦スワイプもここで。

**d. 回帰の基準値 — まず今の共通のしきい値のままで回す**

- CrowPanel のメニューの状態の free_int は 172,424 / largest_int 122,880(Phase 24)で、`MIN_FREE_INT=146000` / `MIN_LARGEST_INT=98304` を上回る。
  `EXPECT_DELTA`(mp3player −472 / hostapi_check −176)は同じコードの一度きりの確保なので、同じ値になる見込み。
- **通れば conf は変えない**(CrowPanel の基準値は記録だけに残す)。違う値が出てボード別の上書きが要るときは、conf のスキーマの変更として改めて承認を求める(ゲート 4)。

### ユーザーに確かめてほしいこと

1. 設計メモ(a〜d)でよいか。

### 0-c ユーザーの承認(2026-10-10)

設計メモ a〜d を承認。
