// MP3 プレーヤー(Phase 6B/6C)。
// - hostapi_fs_list でミュージックルートの .mp3 を列挙し、一覧に出す(最大 16 曲)
// - 行のタップでその曲を頭から再生。ステータス行の右で ▶ / ‖(再生 / 一時停止)、その左の ■ で停止
// - 毎 tick get_state をポーリングし、FINISHED で次の曲へ(最後の曲なら停止)
//
// Phase 22c: metronome(Phase 22b)と同じ骨格(ヘッダ + ステータス行 + 黒の本体)・配色(appui::theme)に作り直した。
// タイトルの `(wasm)` と `state:` の行を消し、PLAY / PAUS / STOP のボタンをステータス行の記号に、
// 一覧のスクロールを `^` / `v` のボタンから縦スワイプ(行単位)に変えた。画面と操作の仕様は docs/apps/mp3player/spec.md。
//
// ホスト API (module "env") のみ使用。no_std / アロケータ不要。
#![no_std]

use core::ptr::{addr_of, addr_of_mut};

use appui::theme::{
    BAR_FG, BODY_BG, BODY_Y, HDR_BG, HDR_H, HINT_X, LEAF, PLAY2_X, PLAY_HIT_X, PLAY_X, ROW_BG, ROW_SEL, SCREEN_H,
    SCREEN_W, STA_BG, STA_H, STA_TEXT_Y, STA_Y, SYM_PAUSE, SYM_PLAY, SYM_STOP, TITLE_X, TITLE_Y, TXT_CREAM, TXT_DIM,
    TXT_HINT, TXT_ON, TXT_STOP,
};
use appui::{Action, Gesture};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

extern "C" {
    fn hostapi_draw_text_rgb(x: i32, y: i32, ptr: *const u8, len: u32, rgb888: u32);
    fn hostapi_fill_rect(x: i32, y: i32, w: i32, h: i32, rgb888: u32);
    fn hostapi_poll_event(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_now_ms() -> u32;
    fn hostapi_audio_play(path: *const u8, path_len: u32) -> i32;
    fn hostapi_audio_ctrl(cmd: i32) -> i32;
    fn hostapi_audio_get_state() -> i32;
    fn hostapi_fs_list(idx: i32, buf: *mut u8, buf_len: u32) -> i32;
}

/// shared/hostapi_defs.h と同一レイアウト(12 bytes, LE)
#[repr(C)]
#[derive(Clone, Copy)]
struct Event {
    ev_type: u16,
    param: u16,
    x: i16,
    y: i16,
    time_ms: u32,
}

const EV_BUF_LEN: usize = 16;

const CMD_PAUSE: i32 = 1;
const CMD_RESUME: i32 = 2;
const CMD_STOP: i32 = 3;

const ST_PLAYING: i32 = 1;
const ST_PAUSED: i32 = 2;
const ST_FINISHED: i32 = 3;
const ST_ERROR: i32 = 4; // play 失敗。次の play まで保持

// ---- ステータス行: 右端 = ▶ / ‖、その左 = ■(停止中は出さない)----
const PLAY_HIT_SPLIT: i32 = 276; // 当たり判定: ■ = x 240〜276、▶ / ‖ = x 276〜320(停止中は 240〜320 が ▶)
const HINT_STOPPED: &[u8] = b"tap a song, swipe to scroll";
const CANNOT_PLAY: &[u8] = b"cannot play: ";

// ---- 本体: 曲の一覧(27px × 7 行。Phase 22c の D4 で決めた案 B)----
const LIST_Y0: i32 = BODY_Y + 2;
const ROW_H: i32 = 27;
const ROW_GAP: i32 = 3; // 行の間の黒
const ROWS: usize = 7;
const ROW_X: i32 = 4;
const ROW_W_FULL: i32 = 312;
const ROW_W_BAR: i32 = 306; // スクロールの帯を出すとき
const MARK_X: i32 = 10; // 行頭の記号(今の曲: 再生中 ▶ / 一時停止中 ‖)
const NAME_X: i32 = 28;
const TEXT_DY: i32 = 4; // 行の上端から文字の上端まで
// スクロールの位置の帯(曲が行に収まらないときだけ)
const BAR_X: i32 = 313;
const BAR_W: i32 = 4;
const BAR_H: i32 = ROWS as i32 * ROW_H - ROW_GAP;
const BAR_MIN: i32 = 12;
// ヘッダ右の曲の番号 `2/12`(右端をそろえる)
const NUM_RIGHT: i32 = 312;
// 曲が無いとき
const EMPTY_Y: i32 = 136;

const MAX_TRACKS: usize = 16;
const NAME_MAX: usize = 64;

/// ASCII 32〜126 の字幅(px)。実機の Montserrat 14 の adv_w と Linux の DejaVu 13pt の大きいほうを切り上げた値。
/// 行に入らない名前を字幅で切るのに使う(Host API に文字の幅を測る手段は無い)
const GLYPH_W: [u8; 95] = [
    5, 6, 6, 11, 9, 13, 11, 4, 6, 6, 7, 11, 5, 6, 5, 5, 10, 9, 9, 9, 10, 9, 9, 9, 9, 9, 5, 5, 11, 11, 11, 8, 15,
    11, 11, 11, 12, 10, 9, 11, 12, 5, 8, 11, 9, 14, 12, 12, 11, 12, 11, 9, 9, 12, 10, 16, 10, 10, 10, 6, 5, 6,
    11, 7, 9, 9, 10, 8, 10, 9, 5, 10, 10, 4, 4, 9, 4, 15, 10, 9, 10, 10, 6, 7, 6, 10, 8, 13, 8, 8, 8, 9, 5, 9, 11,
];
const NON_ASCII_W: i32 = 14; // UTF-8 の先頭バイト(続きのバイトは 0)

static mut TRACKS: [[u8; NAME_MAX]; MAX_TRACKS] = [[0; NAME_MAX]; MAX_TRACKS];
static mut TRACK_LENS: [usize; MAX_TRACKS] = [0; MAX_TRACKS];
static mut TRACK_COUNT: usize = 0;
static mut CUR: usize = 0; // 選んでいる曲(▶ で頭から鳴らす曲)
static mut TOP: usize = 0; // 一覧の先頭の行の曲
static mut PRESS_TOP: usize = 0; // 押したときの TOP(スワイプの基準)
static mut ERROR: bool = false; // 再生に失敗した(次の操作で消す)
static mut GESTURE: Gesture = Gesture::new();
// 描いたもの(変わったときだけ描き直す)
static mut SHOWN_ST: i32 = -1;
static mut SHOWN_ERROR: bool = false;
static mut SHOWN_CUR: usize = usize::MAX;
static mut SHOWN_TOP: usize = usize::MAX;
static mut SHOWN_NUM_X: i32 = -1;
static mut SHOWN_THUMB_Y: i32 = -1;

struct Line {
    buf: [u8; 80],
    len: usize,
}

impl Line {
    fn new() -> Line {
        Line { buf: [0; 80], len: 0 }
    }
    fn push(&mut self, s: &[u8]) -> &mut Line {
        for &b in s {
            if self.len < self.buf.len() {
                self.buf[self.len] = b;
                self.len += 1;
            }
        }
        self
    }
    fn push_u32(&mut self, mut v: u32) -> &mut Line {
        let mut digits = [0u8; 10];
        let mut i = digits.len();
        loop {
            i -= 1;
            digits[i] = b'0' + (v % 10) as u8;
            v /= 10;
            if v == 0 {
                break;
            }
        }
        let mut tmp = [0u8; 10];
        let n = digits.len() - i;
        tmp[..n].copy_from_slice(&digits[i..]);
        self.push(&tmp[..n])
    }
    fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }
}

fn text(x: i32, y: i32, s: &[u8], rgb: u32) {
    unsafe { hostapi_draw_text_rgb(x, y, s.as_ptr(), s.len() as u32, rgb) };
}

fn rect(x: i32, y: i32, w: i32, h: i32, rgb: u32) {
    unsafe { hostapi_fill_rect(x, y, w, h, rgb) };
}

fn char_w(b: u8) -> i32 {
    match b {
        32..=126 => GLYPH_W[(b - 32) as usize] as i32,
        0x80..=0xbf => 0,
        _ => NON_ASCII_W,
    }
}

fn text_w(s: &[u8]) -> i32 {
    s.iter().map(|&b| char_w(b)).sum()
}

/// 表示名: 拡張子 `.mp3` を取る(22a のメニューと同じ)
fn display_name(idx: usize) -> &'static [u8] {
    unsafe {
        let all = &*addr_of!(TRACKS);
        let name = &all[idx][..TRACK_LENS[idx]];
        let n = name.len();
        if n > 4 && name[n - 4] == b'.' && name[n - 3..].eq_ignore_ascii_case(b"mp3") {
            &name[..n - 4]
        } else {
            name
        }
    }
}

/// 幅 `max_w` に入るように切る。入らなければ末尾を `..` にする(UTF-8 の途中では切らない)
fn fit(out: &mut Line, s: &[u8], max_w: i32) {
    if text_w(s) <= max_w {
        out.push(s);
        return;
    }
    let budget = max_w - text_w(b"..");
    let mut w = 0;
    let mut end = 0;
    for (i, &b) in s.iter().enumerate() {
        let cw = char_w(b);
        if cw > 0 && w + cw > budget {
            break;
        }
        w += cw;
        end = i + 1;
    }
    while end > 0 && end < s.len() && (s[end] & 0xc0) == 0x80 {
        end -= 1; // 文字の途中で切らない
    }
    out.push(&s[..end]).push(b"..");
}

fn scan_tracks() {
    unsafe {
        TRACK_COUNT = 0;
        for i in 0..MAX_TRACKS {
            let n = hostapi_fs_list(i as i32, (*addr_of_mut!(TRACKS))[i].as_mut_ptr(), NAME_MAX as u32);
            if n < 0 {
                break;
            }
            TRACK_LENS[i] = n as usize;
            TRACK_COUNT += 1;
        }
    }
}

fn state() -> i32 {
    unsafe { hostapi_audio_get_state() }
}

/// 一覧に収まらないか(スクロールの帯を出すか)
fn scrollable() -> bool {
    unsafe { TRACK_COUNT > ROWS }
}

fn max_top() -> usize {
    unsafe { TRACK_COUNT.saturating_sub(ROWS) }
}

/// 今の曲が一覧の外なら、見える位置まで送る(再生中の要素は追う。ui-conventions §3.5)
fn reveal_cur() {
    unsafe {
        if CUR < TOP {
            TOP = CUR;
        } else if CUR >= TOP + ROWS {
            TOP = CUR + 1 - ROWS;
        }
    }
}

fn draw_header_num() {
    unsafe {
        let mut l = Line::new();
        if TRACK_COUNT == 0 {
            l.push(b"0/0");
        } else {
            l.push_u32(CUR as u32 + 1).push(b"/").push_u32(TRACK_COUNT as u32);
        }
        let x = NUM_RIGHT - text_w(l.as_bytes());
        if SHOWN_NUM_X >= 0 && SHOWN_NUM_X != x {
            text(SHOWN_NUM_X, TITLE_Y, b"", TXT_CREAM); // 幅が変わったら前の位置を消す
        }
        SHOWN_NUM_X = x;
        text(x, TITLE_Y, l.as_bytes(), TXT_CREAM);
    }
}

/// ステータス行: 左 = 停止中だけ手引き / 失敗、右 = ■ と ▶ / ‖
fn draw_status(st: i32) {
    unsafe {
        let mut l = Line::new();
        let mut color = TXT_HINT;
        if ERROR {
            l.push(CANNOT_PLAY);
            let room = PLAY_HIT_X - HINT_X - text_w(CANNOT_PLAY);
            fit(&mut l, display_name(CUR), room);
            color = TXT_STOP;
        } else if TRACK_COUNT > 0 && st != ST_PLAYING && st != ST_PAUSED {
            l.push(HINT_STOPPED);
        }
        text(HINT_X, STA_TEXT_Y, l.as_bytes(), color);

        let active = st == ST_PLAYING || st == ST_PAUSED;
        text(PLAY2_X, STA_TEXT_Y, if active { SYM_STOP } else { b"" }, TXT_STOP);
        if TRACK_COUNT == 0 {
            text(PLAY_X, STA_TEXT_Y, SYM_PLAY, TXT_DIM);
        } else if st == ST_PLAYING {
            text(PLAY_X, STA_TEXT_Y, SYM_PAUSE, TXT_CREAM);
        } else {
            text(PLAY_X, STA_TEXT_Y, SYM_PLAY, TXT_ON);
        }
    }
}

fn draw_list(st: i32) {
    unsafe {
        if TRACK_COUNT == 0 {
            text(HINT_X, EMPTY_Y, b"no mp3 files in /sdcard/music", TXT_HINT);
            return;
        }
        let row_w = if scrollable() { ROW_W_BAR } else { ROW_W_FULL };
        for r in 0..ROWS {
            let idx = TOP + r;
            let y = LIST_Y0 + r as i32 * ROW_H;
            let ty = y + TEXT_DY;
            if idx >= TRACK_COUNT {
                rect(ROW_X, y, 0, 0, BODY_BG);
                text(MARK_X, ty, b"", TXT_CREAM);
                text(NAME_X, ty, b"", TXT_CREAM);
                continue;
            }
            rect(ROW_X, y, row_w, ROW_H - ROW_GAP, if idx == CUR { ROW_SEL } else { ROW_BG });
            if idx == CUR && st == ST_PLAYING {
                text(MARK_X, ty, SYM_PLAY, LEAF);
            } else if idx == CUR && st == ST_PAUSED {
                text(MARK_X, ty, SYM_PAUSE, TXT_CREAM);
            } else {
                text(MARK_X, ty, b"", TXT_CREAM);
            }
            let mut l = Line::new();
            fit(&mut l, display_name(idx), ROW_X + row_w - NAME_X - 4);
            text(NAME_X, ty, l.as_bytes(), TXT_CREAM);
        }
        if scrollable() {
            // つまみは TOP ごとに 1 枚(座標でスロットを取るので、前のつまみは w = h = 0 で消す)
            let th = (BAR_H * ROWS as i32 / TRACK_COUNT as i32).max(BAR_MIN);
            let y = LIST_Y0 + (BAR_H - th) * TOP as i32 / max_top() as i32;
            if SHOWN_THUMB_Y >= 0 && SHOWN_THUMB_Y != y {
                rect(BAR_X, SHOWN_THUMB_Y, 0, 0, BAR_FG);
            }
            SHOWN_THUMB_Y = y;
            rect(BAR_X, y, BAR_W, th, BAR_FG);
        }
    }
}

/// 状態・選択・スクロールが変わっていたら描き直す
fn refresh() {
    let st = state();
    unsafe {
        let st_changed = st != SHOWN_ST || ERROR != SHOWN_ERROR;
        if st_changed || CUR != SHOWN_CUR || TOP != SHOWN_TOP {
            draw_list(st);
        }
        if st_changed || CUR != SHOWN_CUR {
            draw_status(st);
        }
        if CUR != SHOWN_CUR {
            draw_header_num();
        }
        SHOWN_ST = st;
        SHOWN_ERROR = ERROR;
        SHOWN_CUR = CUR;
        SHOWN_TOP = TOP;
    }
}

/// 選んでいる曲を頭から鳴らす
fn play_cur() {
    unsafe {
        if CUR < TRACK_COUNT {
            ERROR = hostapi_audio_play((*addr_of!(TRACKS))[CUR].as_ptr(), TRACK_LENS[CUR] as u32) != 0;
        }
    }
}

/// ▶ / ‖: 再生中なら一時停止、一時停止中なら続きから、それ以外は選んでいる曲を頭から
fn toggle_play() {
    match state() {
        ST_PLAYING => unsafe {
            hostapi_audio_ctrl(CMD_PAUSE);
        },
        ST_PAUSED => unsafe {
            hostapi_audio_ctrl(CMD_RESUME);
        },
        _ => play_cur(),
    }
}

fn on_tap(x: i32, y: i32) {
    unsafe {
        if TRACK_COUNT == 0 {
            return;
        }
        if y >= STA_Y && y < STA_Y + STA_H {
            if x >= PLAY_HIT_X {
                let st = state();
                let active = st == ST_PLAYING || st == ST_PAUSED;
                ERROR = false;
                if active && x < PLAY_HIT_SPLIT {
                    hostapi_audio_ctrl(CMD_STOP);
                } else {
                    toggle_play();
                }
            }
            return;
        }
        if y >= LIST_Y0 && y < LIST_Y0 + ROWS as i32 * ROW_H {
            let idx = TOP + ((y - LIST_Y0) / ROW_H) as usize;
            if idx < TRACK_COUNT {
                CUR = idx;
                play_cur();
            }
        }
    }
}

fn on_action(a: Action) {
    unsafe {
        match a {
            Action::Press { .. } => PRESS_TOP = TOP,
            // 長押しに意味は無いので、長押しのまま離してもタップと同じ(点滅もしない。ui-conventions §2 / §4)
            Action::Tap { x, y } | Action::LongPressFired { x, y } => on_tap(x, y),
            // 縦スワイプ: 行単位で送る(上へはらうと後ろの曲が出る)。指に px 単位では追従しない
            // (描画スロットが座標で引かれるので、行の y を動かすとスロットを使い切る)
            Action::Scroll { dy } => {
                let t = PRESS_TOP as i32 - dy / ROW_H;
                TOP = t.clamp(0, max_top() as i32) as usize;
            }
            _ => {}
        }
    }
}

#[no_mangle]
pub extern "C" fn app_init() -> i32 {
    rect(0, 0, SCREEN_W, HDR_H, HDR_BG);
    rect(0, STA_Y, SCREEN_W, STA_H, STA_BG);
    rect(0, BODY_Y, SCREEN_W, SCREEN_H - BODY_Y, BODY_BG);
    text(TITLE_X, TITLE_Y, b"MP3 Player", TXT_CREAM);
    unsafe {
        CUR = 0;
        TOP = 0;
        PRESS_TOP = 0;
        ERROR = false;
        GESTURE = Gesture::new();
        SHOWN_ST = -1;
        SHOWN_ERROR = false;
        SHOWN_CUR = usize::MAX;
        SHOWN_TOP = usize::MAX;
        SHOWN_NUM_X = -1;
        SHOWN_THUMB_Y = -1;
    }
    scan_tracks();
    if scrollable() {
        rect(BAR_X, LIST_Y0, BAR_W, BAR_H, ROW_BG); // スクロールの帯の地(つまみより先に作って下に置く)
    }
    refresh();
    0
}

#[no_mangle]
pub extern "C" fn app_tick() {
    let mut evs = [Event { ev_type: 0, param: 0, x: 0, y: 0, time_ms: 0 }; EV_BUF_LEN];
    let n = unsafe {
        hostapi_poll_event(evs.as_mut_ptr() as *mut u8, (EV_BUF_LEN * core::mem::size_of::<Event>()) as u32)
    };
    for ev in &evs[..n.max(0) as usize] {
        let a = unsafe { (*addr_of_mut!(GESTURE)).on_event(ev.ev_type, ev.x as i32, ev.y as i32, ev.time_ms) };
        on_action(a);
    }
    let a = unsafe { (*addr_of_mut!(GESTURE)).tick(hostapi_now_ms()) };
    on_action(a);

    // 自然終了 → 次の曲へ(最後の曲なら停止)。次の曲が一覧の外なら追う。
    // 失敗は play の戻り値のほか、あとから ERROR になる場合も拾う(次の曲へは進まない)
    let st = state();
    if st == ST_ERROR {
        unsafe { ERROR = true };
    }
    if st == ST_FINISHED {
        unsafe {
            if CUR + 1 < TRACK_COUNT {
                CUR += 1;
                reveal_cur();
                play_cur();
            } else {
                hostapi_audio_ctrl(CMD_STOP);
            }
        }
    }
    refresh();
}
