//! サンプルアプリが共有する画面の骨格・配色・記号(Phase 22c)。
//!
//! Phase 22b で metronome に入れた骨格(ヘッダ + ステータス行 + 黒の本体)を、mp3player と共有するために
//! ここへ移した。**値は定数だけ**で、描画は持たない(Host API に依存しないため)。
//!
//! - 配色はメニュー(`shared/launcher_theme.h`。Kybotos のロゴの色)に合わせ、本体は焼き付きを避けて黒。
//!   ヘッダの濃緑とクリームは launcher_theme.h と同じ値にしておくこと。
//! - 状態の色(緑 / 赤 / 黄 / 暗い灰)は対話規約(`docs/design/ui-conventions.md` §4)。アプリをまたいで同じ。
//! - 座標は論理座標(320×240)。

// ---- 配色 ----

/// ヘッダ: メニューの背景と同じ濃緑(`MENU_BG_RGB888`)
pub const HDR_BG: u32 = 0x18_3c_29;
/// ステータス行: 黒に近い緑
pub const STA_BG: u32 = 0x0b_14_0f;
/// 本体: 黒
pub const BODY_BG: u32 = 0x00_00_00;
/// 題名・大事な値: クリーム(`MENU_TITLE_RGB888`)
pub const TXT_CREAM: u32 = 0xf3_f1_e4;
/// 手引き・補助の文字: くすんだ緑
pub const TXT_HINT: u32 = 0x5f_74_66;
/// 若葉(ロゴの色。点灯・今の要素の印)
pub const LEAF: u32 = 0x8f_d1_8b;
/// ON / 実行できる(▶)
pub const TXT_ON: u32 = 0x40_e0_70;
/// 停止(■、押すと止まる)・失敗
pub const TXT_STOP: u32 = 0xf0_60_60;
/// 値の変更中(点滅)
pub const TXT_EDIT: u32 = 0xff_e0_60;
/// その画面では意味が無い(操作も受け付けない)
pub const TXT_DIM: u32 = 0x4a_4f_4c;
/// 一覧の行の地
pub const ROW_BG: u32 = 0x10_1a_14;
/// 一覧の選んでいる行(メニューの Settings の行と同じ `MENU_SETTINGS_BG_RGB888`)
pub const ROW_SEL: u32 = 0x24_48_3a;
/// スクロールの位置の帯のつまみ(メニューの中緑 `MENU_APP_BG_RGB888`)
pub const BAR_FG: u32 = 0x3e_66_48;

// ---- 骨格: ヘッダ(26px)+ ステータス行(24px)+ 本体 ----

pub const SCREEN_W: i32 = 320;
pub const SCREEN_H: i32 = 240;
pub const HDR_H: i32 = 26;
pub const STA_Y: i32 = 26;
pub const STA_H: i32 = 24;
pub const BODY_Y: i32 = STA_Y + STA_H;
/// ヘッダ左の題名
pub const TITLE_X: i32 = 8;
pub const TITLE_Y: i32 = 5;
/// ステータス行の文字の y(左の手引き・右の記号)
pub const STA_TEXT_Y: i32 = STA_Y + 4;
/// ステータス行の左の手引き
pub const HINT_X: i32 = 8;
/// ステータス行の右端: そのアプリの主操作のトグル(▶ / ■、▶ / ‖)
pub const PLAY_X: i32 = 292;
/// その左: 補助の操作(mp3player の ■)
pub const PLAY2_X: i32 = 258;
/// 右端の記号の当たり判定の左端(ステータス行の右 80px。演奏中に 1 タップで届くように広く取る)
pub const PLAY_HIT_X: i32 = 240;

// ---- 記号(実機のフォントの FontAwesome。Linux ホストは図形で描く)----

/// U+F04B ▶
pub const SYM_PLAY: &[u8] = b"\xEF\x81\x8B";
/// U+F04C ‖(Phase 22c)
pub const SYM_PAUSE: &[u8] = b"\xEF\x81\x8C";
/// U+F04D ■
pub const SYM_STOP: &[u8] = b"\xEF\x81\x8D";
