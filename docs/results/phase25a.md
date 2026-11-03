# Phase 25a 実施記録: バッテリー残量の表示(画面の右上に常に出す)

- 指示書: `docs/prompts/phase25a.md`(追記 2026-11-03 を含む)
- 状態: **完了(2026-11-03)**。Host API / ABI は不変。`wasm-apps/metronome` と `mp3player` はヘッダ右の座標だけを変えて再ビルドした

---

## 決めたこと(ユーザー)

- 表示は**画面の右上に常に**(メニューもアプリの中も)。USB 給電中は**白抜き**(記号は使わない)、電池で動いているときは普通の色の文字、20% 以下は赤(USB のときは赤の白抜き)。
  数字は `%` つき。
- 座標はモックアップ(`captures/phase25a/mock.png` / `mock2.png`。git の対象外)を見て承認: **右端 316 に右寄せ、文字は y=5、白抜きの地は x0〜320・y 3〜23、予約は x ≥ 276**。
- 追加の要件(実機で確認したあと): **スクリーンセーバー(減光)と画面消灯の間は消す**(焼き付き防止)。

## 実装

- `shared/battery_badge.h`(両ホスト共有): 座標・配色・`battery_badge_text`。
- **実機**: `src/components/wasm_runtime/battery_badge.cpp`。`lv_layer_top()` に `lv_label` を 1 つ置き(クリックは受けない、タッチは下へ通る)、`lv_timer`(2 秒)で `battery::Get` を読んで更新する。
  **新しいタスクは作らない。** 検知の無いボードでは何も作らない(`nm` で、CrowPanel のオブジェクトには LVGL も `battery::` のシンボルも無い)。
  - USB 給電かどうかは `PowerKey::is_battery_mode()` を `battery::SetExternalPower(!...)` で渡す(電源キーで起動していない = 外部電源。起動時に決まり、あとから USB を挿しても変わらない)。
  - **スクリーンセーバー**: `screensaver.cpp` の `enter_saver_locked` / `wake_locked` から `battery_badge_suppress(true / false)`(消灯は減光のあとなので、そのまま消えている)。復帰したらすぐ出す。
- **Linux ホスト**: `hosts/linux/hostapi_sdl.c` の `draw_battery_badge`(マスター設定の帯より下、アプリより上)。電池が無いので模擬値
  `KYBOTOS_SIM_BATT=<0..100>`(電池)/ `usb<0..100>`(USB の白抜き)、未設定なら出さない。撮影は `scripts/badge-shots-linux.sh <タスク名>`(`captures/phase25a-linux/`)。
- **ヘッダ右の部品を左へ寄せた**(承認済み。`shared/launcher_theme.h` ほか):

| 部品 | 旧 | 新 |
|---|---|---|
| メニューの `Settings`(文字の右端 / 当たり判定) | 310 / x 220〜320 | **270 / x 196〜274**(`MENU_SETTINGS_HIT_X1` を足した) |
| metronome `120bpm`(x / ドラッグの当たり判定) | 200 / 194〜258 | **186 / 180〜244** |
| metronome `4/4` の x | 268 | **246** |
| mp3player `1/10` の右端 | 312 | **270** |

  Linux ホストのメニュー(`MENU_SETTINGS_X` 254 → 216、当たり判定に `MENU_SETTINGS_HIT_X1`)も同じ。`.wasm` は `wasm-apps/README.md` の手順で再ビルドしてコミット。
- **非公開の app-sequencer のアプリ**のヘッダ右(BPM / 位置、x 200〜316)はバッジと重なる(この repo では直せない)。非公開の repo の課題として記録した。

## 確認

- **Linux ホスト**(`captures/phase25a-linux/sheet.png`): メニュー(USB 98% の白抜き / 64% / 15% の赤 / USB 15% の赤の白抜き / 模擬値なしで出ない)、metronome(64% / USB 98%)、mp3player(USB 100% / 15%)。
  ヘッダの部品と重ならない。Linux の文字は DejaVu 13pt で、実機(Montserrat 14)と幅が少し違う。
- **実機(Waveshare)**: ユーザーが目視で、メニュー・metronome・mp3player とも**表示は問題なし**。USB 給電中は白抜き。
  **スクリーンセーバー(60 秒)・消灯(180 秒)でバッジが消え、タッチの復帰ですぐ戻る**(ユーザー確認)。
- **回帰 3 本 PASS**(`KYBOTOS_DEV_APPS=1`、`device-regress.sh --board waveshare_lcd28`。`captures/phase25a-waveshare2/report.md`):

| アプリ | 開始 free_int | 終了 free_int | 差分 | シナリオ | 判定 |
|---|---|---|---|---|---|
| metronome ×3 | 147,100 | 147,100 | +0 | PASS(19 手順) | PASS |
| mp3player | 147,100 | 146,628 | −472 | PASS(17 手順) | PASS |
| hostapi_check | 146,628 | 146,452 | −176 | PASS(6 手順) | PASS |

  free_int は 25 のときの 147,108 から **−8B**(LVGL の部品は内部メモリをほとんど使わない)。`MIN_FREE_INT`(146,000)との余裕は **452B**。回帰の conf は変えていない。
  回帰のシナリオはヘッダの座標を叩かない(`120bpm` などは文字で待つ)ので、座標を変えても通る。
- CrowPanel: ビルドが通り、バッジのオブジェクトは空の関数 2 つだけ(何も動かさない)。実機の起動は 25 で確認済みで、今回の変更は何も足さない。

## 後片付け

Waveshare を普段使い(`KYBOTOS_DEV_APPS=0`)のファームに焼き直し、共用の SD から検査用の 3 本を `rm` で消した(SD のアプリは sequencer / metronome / mp3player)。
`seed_file` が埋め込みと内容の違う SD の `.wasm` を書き直すので、SD の metronome / mp3player は新しい座標のものになる。

## 完了条件の確認

| 完了条件 | 結果 |
|---|---|
| 1. 右上にメニューでもアプリの中でも出る。USB は白抜き、電池は普通の文字、少ないと赤 | ✅(USB 給電を実機で、電池モードは Linux の模擬値で。電池モードの実機の目視はしていない) |
| 2. 既存の部品と重ならず、タッチが従来どおり。セーバー中は出ない | ✅ |
| 3. Linux でも出る(模擬値)。Host API / ABI は不変。`.wasm` を再ビルドしてコミット | ✅ |
| 4. 回帰 3 本 PASS、CrowPanel は何も出ずにビルド。普段使いに戻し SD の検査用アプリを消す | ✅ |
| 5. 記録・roadmap・status、非公開のアプリの追従、25b への引き継ぎ | ✅(下) |

## 25b への引き継ぎ

- バッジは `battery::Get` を 2 秒おきに読むだけ。**警告・電源断は未実装**(25b)。20% 以下の赤は見た目だけ。
- **電池モードの実機の見た目**(USB を抜いて電源キーで起動)は、まだ目視していない。25b で電池を放電させるときに、あわせて見る。
- **`MIN_FREE_INT` との余裕は 452B。** 25b で何か足すなら、先に基準の更新を相談する。
- メニューの題名と `Settings` は y=8、アプリのヘッダは y=5、バッジは y=5 に統一した。メニューでは `Settings` の方が 3px 低く並ぶ(見た目の差。気になれば直す)。
- **非公開の app-sequencer のヘッダ右は未追従**(重なる)。

## 追記 (2026-11-03): CrowPanel の実機の確認と、25b / 25c の扱い

- **25b・25c は実施しない**(ユーザー判断: 優先度が低い)。roadmap の未割当の課題 U-36(電池が少ないときの動き)/ U-37(アプリへ出す Host API)として管理する。上の「25b への引き継ぎ」はそのまま U-36 の前提になる。
- **CrowPanel の確認**: 25a のコードでビルドして焼いた(`captures/phase25a/crowpanel-boot.log`、git の対象外)。`APP: board: crowpanel_adv28`、タッチ FT6336 が初期化、`batt` は `err not supported`、WARN / ERROR なし。
  起動の途中の値は Phase 25 の CrowPanel と −8B(Audio_Init 後 free 220,708 / seq init 後 170,884 / WASM ready 170,676)で、Waveshare の差と同じ。
  SD を CrowPanel に差し替えて、ユーザーが実機で確認した: メニューの右上に何も出ず、metronome / mp3player が開いて操作でき、戻る / ホームも効き、見た目の崩れなし(OK)。
- CrowPanel の回帰 3 本は回していない(電池まわりは何も動かさず、起動値が一致し、操作はユーザーが確認したため)。
- 焼く前に、Waveshare 用の `fw.sh monitor` が `/dev/ttyACM0` を握っていて esptool が「chip stopped responding」で失敗した(2 枚とも ttyACM0。別のボードに焼く前にモニタを止める)。

