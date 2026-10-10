# Phase 24b 実施記録: CrowPanel の音と SD(回帰 3 本 PASS)

- 指示書: `docs/prompts/phase24b.md`
- 状態: **完了(2026-10-10)**。一覧のスワイプと Waveshare の実機の回帰は 24c へ持ち越し(指示書の追記)

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

---

## ステップ 1: 実装(2026-10-10。コミットは下の表)

- `audio.cpp`: `amp_init_off()` を `amp_set(bool on)` にし、`Audio_Init` / `Audio_Click_Init` の初回だけ「OFF の水準で出力にする → I2S の初期化 → ON の水準」。
  以後つけたまま。ログは `amplifier enable pin GPIO21: off` → `on`(10ms 後)。
- mono の MP3 の I2S の設定、SD の設定、回帰の conf は変えていない(設計メモ b〜d)。
- **Waveshare の `audio.cpp.obj` の逆アセンブル(`objdump -d -r`)は、変更の前後で完全に一致**(`captures/phase24b/audio-obj-{before,after}.txt`)。
  アンプのピンが `GPIO_NUM_NC` のボードでは、`amp_set` はコンパイル時に消える。

## ステップ 2: 確認(2026-10-10)

**CrowPanel の回帰 3 本**(`KYBOTOS_DEV_APPS=1` で焼き、`device-regress.sh --board crowpanel_adv28`。`captures/phase24b-crowpanel/report.md`)

| アプリ | 開始 free_int | 終了 free_int | 差分 | largest_int | シナリオ | 判定 |
|---|---|---|---|---|---|---|
| metronome ×3 | 155,456 | 155,456 | +0 | 106,496 | PASS(19 手順) | PASS |
| mp3player | 155,456 | 154,984 | −472 | 106,496 | PASS(17 手順) | PASS |
| hostapi_check | 154,984 | 154,808 | −176 | 106,496 | PASS(6 手順) | PASS |

**PASS**(WARN / ERROR 0 件)。**今の共通のしきい値と `EXPECT_DELTA` のままで通った**ので、conf は変えていない(設計メモ d)。

**CrowPanel の基準値**(記録のみ): 開始時の free_int **155,456** / largest_int **106,496**(Waveshare は 150,232 / 98,304。電源キーのタスクが無いことなどで +5,224 / +8,192)、
`EXPECT_DELTA` は Waveshare と同じ(mp3player −472 / hostapi_check −176)、metronome の highmark **25,744**(同じ)、free_psram 8,136,668(同じ)。

**耳**(ユーザー)

| 確認 | 結果 |
|---|---|
| 起動音、metronome のクリック、mono の MP3 | ✅(0-a) |
| `synth_probe`(内蔵音源のドラム 4 音と音階、約 16 秒) | ✅ 欠けや割れなし |
| mp3player を指で: 再生 | ✅ ユーザーが足した長い曲(`3 Views Of A Secret.mp3`)を含めて途切れずに再生(ログに WARN / ERROR なし) |
| Settings の音量のバー | ✅ 大きさが変わる |

**SD**: 回帰の hostapi_check のファイルの読み書き(`fs_write` / `fs_read`)が PASS。MP3 の連続再生も途切れなし。

**持ち越し**(指示書の追記): mp3player の一覧の縦スワイプ(SD の曲が 4 曲で一覧が送られない)、Waveshare の実機の回帰(オブジェクトの一致で代えた)。

## ステップ 3: 後片付けと文書(2026-10-10)

- CrowPanel を普段使い(`KYBOTOS_DEV_APPS=0`)のファームに戻し、共用の SD から検査用の 3 本を `rm` で消した(SD のアプリは sequencer / metronome / mp3player)。
  Waveshare は 24a の終わりの普段使いのファームのまま(このフェーズの変更はオブジェクトが同じなので、焼き直していない)。
- README のボードの表、roadmap(24b を done に、24c に持ち越しの 2 項目)、status、lessons(ESP32-S3 の mono の I2S は R にも出る)。

## 完了条件の確認

| 完了条件 | 結果 |
|---|---|
| 1. 起動音・クリック・内蔵音源・mono の MP3 が鳴る、ポップ音・ノイズの所見 | ✅ |
| 2. CrowPanel で回帰 3 本 PASS、基準値の記録 | ✅ |
| 3. SD: 連続再生、読み書き、一覧の縦スワイプ | ✅(スワイプだけ 24c へ。追記) |
| 4. Waveshare: 回帰 3 本 PASS、数値が 24a と同じ | **24c へ**(追記)。`audio.cpp.obj` は完全に一致 |
| 5. 普段使いのファームに戻し、検査用アプリを消す、記録 | ✅ |

## 追加: ホームキー(IO1)(2026-10-10)

指示書の追記「ホームキー(IO1)を足す」。

**配線**(ユーザー): IO1(J9 の TX2)と GND の間に押しボタン。外付けの抵抗は無し。

**変更**

| ファイル | 内容 |
|---|---|
| `board_pins.hpp` | `KB_HAS_POWER_KEY` を足す。ボードの記述に無ければ `KB_HAS_POWER_LATCH` と同じ値。キーがあってラッチが無いボードでは `PIN_PWR_LATCH` を `GPIO_NUM_NC` にする |
| `boards/crowpanel_adv28.h` | `KB_HAS_POWER_KEY 1`、`PIN_PWR_KEY_IN GPIO_NUM_1`(`KB_HAS_POWER_LATCH 0` のまま) |
| `power_key.cpp` | ラッチのピンが NC なら、ラッチの設定をせず `battery_mode_` を false のままにする(長押しの電源断は起きない)。キーは内部プルアップの入力で LOW が押下、チャタリングの吸収(50ms)は Waveshare と同じ処理 |
| `app_main.cpp` | キーのタスクとコールバックを `#if KB_HAS_POWER_LATCH` から `#if KB_HAS_POWER_KEY` に |

Waveshare は `KB_HAS_POWER_KEY` = `KB_HAS_POWER_LATCH` = 1 なので `app_main.cpp` は同じコードになる。`power_key.cpp` には実行時の分岐(ラッチのピンが NC か)が 1 つ増える。
Waveshare のビルドは通る(`captures/homekey/build-waveshare.log`)。実機の回帰は、24c に持ち越した回帰でまとめて確かめる。

**CrowPanel での確認**(普段使いのファーム、`captures/homekey/monitor.log`)

- 起動ログ: `PWR_KEY: Key on GPIO1 without latch: back / force-home only`。
- 短押しで戻る: mp3player と sequencer で `app: key back -> stop` → メニュー。
- 長めに押して強制ホーム: sequencer で `app: forced home` → メニュー。
- メニューの状態の free_int は **150,240**、largest_int 98,304(キーのタスクを持つ Waveshare の 150,232 とほぼ同じ)。

