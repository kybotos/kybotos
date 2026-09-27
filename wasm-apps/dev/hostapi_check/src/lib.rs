// Phase 22 の検査アプリ。Host API をひととおり叩き、合否を画面の最下行に出す
// (`RESULT PASS` / `RESULT WAIT <残り>` / `RESULT FAIL <項目>`)。回帰(scripts/device-regress.sh /
// linux-regress.sh)はホストの `texts` でこの行を読んで判定する。実機と Linux で同じ `.wasm` を走らせる。
//
// 第 1 部(起動と同時に走る。タップ不要。約 20 秒):
//   音楽時間軸 — Phase 11 / 17 の seq_smoke の 12 項目をそのまま移した(テンポを上げて時間を縮めた):
//     tmp  PLAYING 中の tempomap_set_tempo が次の小節頭から効く
//     lop  tempomap_set_loop で song tick が巻き戻り、playback tick は単調増加
//     flu  seq_flush_after で先読み済みの予約が減る
//     loc  transport_locate で song が移動し、playback tick は戻らない
//     stp  transport_stop
//     u2s  STOPPED 中の time_us_to_tick が -1
//     con  transport_continue で停止点から継続
//     u2t  time_us_to_tick(get_position の host_us) ≒ get_position の tick
//     clr  tempomap_clear: STOPPED で 0 と既定値、PLAYING で -1
//     exh  100 小節ぶんのテンポ / 拍子の変化(マップ上限 32 の 3 倍超)で -1 にならず位置が正しい
//     rst  clear 後の再生に前回の予約が混ざらない
//     sta  OP_STOP で予約 tick ちょうどに止まる
//   その他 — Phase 22 で足した:
//     fsw  fs_write → fs_read が一致(データルートの hostapi_check.dat)
//     fsr  fs_read の切り詰め読み、無いファイルと不正なパスで -1
//     ton  tone_define / tone_play の正常値 0 と範囲外 -1、play_click(Click は既定 MUTE なので無音)
//     aud  audio の異常系: 無いファイルの audio_play が -1 で ERROR、ERROR 中の PAUSE が -1、STOP で STOPPED
//     mid  midi_send が 0、midi_recv が 0 以上(ループバックの配線は要求しない)
//     now  now_ms が単調増加し、第 1 部のあいだに 1 秒以上進む
//   draw_text_rgb は判定できない(読み返す API が無い)ので、色付きの行 `rgb 40c0ff` を描くだけにし、
//   回帰のシナリオが `texts` の色で確かめる。
//
// 第 2 部(シナリオが注入したタップ / キーで確かめる):
//     tch  的(TAP)の中の tap で DOWN / UP が 1 回ずつ、MOVE なし、同じ座標
//     mov  横の drag で MOVE が 1 件以上、各 MOVE が 8px 以上離れ、UP が DOWN から 64px 以上右
//     key  app_key(BACK, CLICK) が呼ばれる。1 を返す(止まらない。回帰は自分で stop を送る)
//
// 意図して外したもの: audio_set_volume(読み出しの関数が無く、呼ぶと装置のマスター音量が戻せない)、
// MP3 の再生(mp3player のシナリオに任せる)、app_exit(停止の途中で呼ばれるので画面に出せない)。
//
// 不正なパスの名前はすべて `hcheck_` を含む。ホストは拒否したパスを W 行に出すので、回帰の許容パターンは
// この名前だけに当てる(scripts/device-regress.conf)。
#![no_std]

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

extern "C" {
    fn hostapi_draw_text(x: i32, y: i32, ptr: *const u8, len: u32);
    fn hostapi_draw_text_rgb(x: i32, y: i32, ptr: *const u8, len: u32, rgb888: u32);
    fn hostapi_fill_rect(x: i32, y: i32, w: i32, h: i32, rgb888: u32);
    fn hostapi_poll_event(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_now_ms() -> u32;

    fn hostapi_play_click();
    fn hostapi_tone_define(slot: i32, wave: i32, freq_hz: i32, dur_ms: i32, level: i32) -> i32;
    fn hostapi_tone_play(slot: i32) -> i32;

    fn hostapi_audio_play(path: *const u8, len: u32) -> i32;
    fn hostapi_audio_ctrl(cmd: i32) -> i32;
    fn hostapi_audio_get_state() -> i32;

    fn hostapi_fs_read(path: *const u8, path_len: u32, buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_fs_write(path: *const u8, path_len: u32, buf: *const u8, buf_len: u32) -> i32;

    fn hostapi_midi_recv(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_midi_send(bytes: *const u8, len: u32) -> i32;

    fn hostapi_transport_start() -> i32;
    fn hostapi_transport_stop() -> i32;
    fn hostapi_transport_continue() -> i32;
    fn hostapi_transport_locate(song_tick: i32) -> i32;
    fn hostapi_transport_get_position(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_tempomap_set_tempo(at_tick: i32, us_per_quarter: i32) -> i32;
    fn hostapi_tempomap_set_meter(at_tick: i32, numer: i32, denom: i32) -> i32;
    fn hostapi_tempomap_set_loop(start_tick: i32, end_tick: i32) -> i32;
    fn hostapi_tempomap_clear() -> i32;
    fn hostapi_seq_write(buf: *const u8, buf_len: u32) -> i32;
    fn hostapi_seq_flush_after(tick: i32) -> i32;
    fn hostapi_seq_filled_until() -> i32;
    fn hostapi_time_us_to_tick(us: i64) -> i32;
}

const PPQN: u32 = 960;
const BEAT: u32 = PPQN;
const BAR: u32 = PPQN * 4; // 4/4
const HORIZON: u32 = BAR * 2; // 2 小節先まで供給する

const PORT_DIN_OUT: u8 = 0;
const PORT_CLICK: u8 = 3;
const OP_TONE: u8 = 1;
const OP_STOP: u8 = 2;
const TRANSPORT_STOPPED: u32 = 0;

const ACCENT_SLOT: u32 = 1;

// seq_smoke は 120 / 180bpm で約 60 秒かかった。検査の中身(小節の数え方・件数)は変えずに
// テンポだけを上げて縮める
const TEMPO_DEFAULT: i32 = 500000; // tempomap_clear 後の既定値(120bpm)
const TEMPO_BASE: i32 = 250000; // 240bpm
const TEMPO_FAST: i32 = 166667; // 360bpm

const LOCATE_TARGET: u32 = BAR * 20;

// ---- 判定のビット(表示順) ----
const CHECKS: [(&[u8], u32); 21] = [
    (b"tmp", 1 << 0), (b"lop", 1 << 1), (b"flu", 1 << 2), (b"loc", 1 << 3),
    (b"stp", 1 << 4), (b"u2s", 1 << 5), (b"con", 1 << 6),
    (b"u2t", 1 << 7), (b"clr", 1 << 8), (b"exh", 1 << 9), (b"rst", 1 << 10),
    (b"sta", 1 << 11), (b"fsw", 1 << 12), (b"fsr", 1 << 13),
    (b"ton", 1 << 14), (b"aud", 1 << 15), (b"mid", 1 << 16), (b"now", 1 << 17),
    (b"tch", 1 << 18), (b"mov", 1 << 19), (b"key", 1 << 20),
];
const CHK_TEMPO: u32 = 1 << 0;
const CHK_LOOP: u32 = 1 << 1;
const CHK_FLUSH: u32 = 1 << 2;
const CHK_LOCATE: u32 = 1 << 3;
const CHK_STOP: u32 = 1 << 4;
const CHK_U2T_STOPPED: u32 = 1 << 5;
const CHK_CONT: u32 = 1 << 6;
const CHK_U2T: u32 = 1 << 7;
const CHK_CLEAR: u32 = 1 << 8;
const CHK_NOEXHAUST: u32 = 1 << 9;
const CHK_RESTART: u32 = 1 << 10;
const CHK_STOPAT: u32 = 1 << 11;
const CHK_FSW: u32 = 1 << 12;
const CHK_FSR: u32 = 1 << 13;
const CHK_TONE: u32 = 1 << 14;
const CHK_AUDIO: u32 = 1 << 15;
const CHK_MIDI: u32 = 1 << 16;
const CHK_NOW: u32 = 1 << 17;
const CHK_TOUCH: u32 = 1 << 18;
const CHK_MOVE: u32 = 1 << 19;
const CHK_KEY: u32 = 1 << 20;
const PART1: u32 = (1 << 18) - 1;
const PART2: u32 = CHK_TOUCH | CHK_MOVE | CHK_KEY;

// stage 9(V1): 2/8 と 3/8、テンポ 2 値を小節ごとに交互に予約する(100 小節 = 上限 32 の 3 倍超)
const V1_BARS: u32 = 100;
const V1_SHORT: u32 = 960; // 2/8 の小節長
const V1_PAIR: u32 = 2400; // 2/8 + 3/8
const V1_BEAT: u32 = 480; // 8 分音符
const V1_TEMPO_EVEN: i32 = 75000;
const V1_TEMPO_ODD: i32 = 80000;
const V1_LOOKAHEAD: u32 = V1_PAIR * 3; // app_tick(100ms)より十分先まで予約する
// 位置の照合は小節境界から離れたところだけで行う(境界の直後はディスパッチャが
// 区間を進める前に位置を読むことがあり、テンポがまだ旧値のことがあるため)
const EDGE_MARGIN: u32 = 96;
// stage 11(V3): 2 小節目の頭で止める
const STOP_AT: u32 = BAR * 2;

// 第 1 部の now の判定: この時間以上進んでいること
const NOW_MIN_ELAPSED_MS: u32 = 1000;

// ---- 画面 ----
const TARGET_X: i32 = 220;
const TARGET_Y: i32 = 140;
const TARGET_W: i32 = 90;
const TARGET_H: i32 = 50;
const RGB_LINE: u32 = 0x40_c0_ff;
const MOVE_MIN_PX: i32 = 8; // HOSTAPI_TOUCH_MOVE_MIN_PX
const DRAG_MIN_DX: i32 = 64;

// ---- fs / audio の検査に使う名前(不正な名前はすべて hcheck_ を含む) ----
const FS_NAME: &[u8] = b"hostapi_check.dat";
const FS_MISSING: &[u8] = b"hcheck_missing.dat";
const FS_BAD: [&[u8]; 4] = [
    b".hcheck_dot",
    b"hcheck_/slash",
    b"hcheck_..dots",
    b"hcheck_toolong_0123456789012345678901234567890123456789012345678", // 64 文字
];
const AUDIO_MISSING: &[u8] = b"hcheck_missing.mp3";

const AUDIO_STOPPED: i32 = 0;
const AUDIO_ERROR: i32 = 4;
const AUDIO_CMD_PAUSE: i32 = 1;
const AUDIO_CMD_STOP: i32 = 3;

const EV_TOUCH_DOWN: u16 = 1;
const EV_TOUCH_UP: u16 = 2;
const EV_TOUCH_MOVE: u16 = 3;
const KEY_BACK: i32 = 1;
const KEY_ACTION_CLICK: i32 = 0;

#[repr(C)]
#[derive(Clone, Copy)]
struct Event {
    ev_type: u16,
    param: u16,
    x: i16,
    y: i16,
    time_ms: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct SeqEvent {
    tick: u32,
    port: u8,
    status: u8,
    data1: u8,
    data2: u8,
    param: u32,
    reserved: u32,
}

impl SeqEvent {
    const fn zero() -> SeqEvent {
        SeqEvent { tick: 0, port: 0, status: 0, data1: 0, data2: 0, param: 0, reserved: 0 }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RecvRec {
    timestamp_us: u64,
    byte: u8,
    _reserved: [u8; 7],
}

// L2 の未受理分(プレフィックス受理契約)。1 拍ぶん = クリック + Note On/Off
const CHUNK_MAX: usize = 8;
static mut PENDING: [SeqEvent; CHUNK_MAX] = [SeqEvent::zero(); CHUNK_MAX];
static mut PENDING_LEN: usize = 0;
static mut PENDING_OFF: usize = 0;

static mut RUNNING: bool = false;
static mut NEXT_BEAT: u32 = 0; // 次に供給する拍(playback tick / BEAT)

static mut STAGE: u8 = 0;
static mut CHK: u32 = 0;
static mut FAILED: u32 = 0; // 結論が「不合格」で出た項目(第 1 部の終わりに確定する)
static mut PART1_DONE: bool = false;
static mut PREV_SONG: u32 = 0;
static mut PREV_PB: u32 = 0;
static mut WRAPS: u32 = 0;
static mut PB_MARK: u32 = 0;
static mut SONG_AT_STOP: u32 = 0;
static mut PB_AT_STOP: u32 = 0;

// stage 8〜11 の状態
static mut CLEAR_STOPPED_OK: bool = false;
static mut V1_FIRST: bool = false;
static mut V1_WRITTEN: u32 = 0;
static mut CHECK_FAIL: bool = false;
static mut SAMPLES: u32 = 0;
// 診断用(画面の 1 行): V1 の照合回数と、位置が合わなかった回数・最初に合わなかった小節
static mut V1_SAMPLES: u32 = 0;
static mut V1_MISS: u32 = 0;
static mut V1_MISS_BAR: u32 = 0;
static mut STOP_WRITTEN: bool = false;

// now / midi
static mut NOW_START: u32 = 0;
static mut NOW_PREV: u32 = 0;
static mut NOW_OK: bool = true;
static mut MIDI_SEND_OK: bool = false;
static mut MIDI_RECV_OK: bool = true;
static mut RX_BYTES: u32 = 0;

// 第 2 部: 押してから離すまでの 1 回の操作
static mut G_ACTIVE: bool = false;
static mut G_DOWN_X: i32 = 0;
static mut G_DOWN_Y: i32 = 0;
static mut G_LAST_X: i32 = 0;
static mut G_LAST_Y: i32 = 0;
static mut G_MOVES: u32 = 0;
static mut G_MOVE_OK: bool = true;
static mut T_DOWNS: u32 = 0;
static mut T_UPS: u32 = 0;
static mut T_MOVES: u32 = 0;
static mut T_LAST_X: i32 = -1;
static mut T_LAST_Y: i32 = -1;

struct Line {
    buf: [u8; 48],
    len: usize,
}

impl Line {
    fn new() -> Line {
        Line { buf: [b' '; 48], len: 0 }
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
    fn push_i32(&mut self, v: i32) -> &mut Line {
        if v < 0 {
            self.push(b"-");
        }
        self.push_u32(v.unsigned_abs())
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
        let d = digits;
        self.push(&d[i..])
    }
    /// 行の長さを揃える(同じ座標のスロットへ短い文字列を描くと、実機は古い文字を消すが念のため)
    fn pad(&mut self, n: usize) -> &mut Line {
        while self.len < n && self.len < self.buf.len() {
            self.buf[self.len] = b' ';
            self.len += 1;
        }
        self
    }
    fn draw(&self, x: i32, y: i32) {
        unsafe { hostapi_draw_text(x, y, self.buf.as_ptr(), self.len as u32) };
    }
}

// ---------------------------------------------------------------------------
// 第 1 部: 音楽時間軸(seq_smoke から移した部分)
// ---------------------------------------------------------------------------

/// 拍 n(playback tick 基準)の 1 拍ぶん。クリック(小節頭はアクセント)+
/// DIN_OUT の Note On/Off(8 分音符長。対は必ず同じチャンクに入れる)。
fn build_beat(n: u32, out: &mut [SeqEvent; CHUNK_MAX]) -> usize {
    let tick = n * BEAT;
    let in_bar = n % 4;
    let mut k = 0;

    out[k] = SeqEvent::zero();
    out[k].tick = tick;
    out[k].port = PORT_CLICK;
    out[k].status = OP_TONE;
    out[k].param = if in_bar == 0 { ACCENT_SLOT } else { 0 };
    k += 1;

    let note: u8 = if in_bar == 0 { 60 } else { 67 };
    out[k] = SeqEvent::zero();
    out[k].tick = tick;
    out[k].port = PORT_DIN_OUT;
    out[k].status = 0x90;
    out[k].data1 = note;
    out[k].data2 = 100;
    k += 1;

    out[k] = SeqEvent::zero();
    out[k].tick = tick + BEAT / 2;
    out[k].port = PORT_DIN_OUT;
    out[k].status = 0x80;
    out[k].data1 = note;
    k += 1;

    k
}

fn drop_pending() {
    unsafe {
        PENDING_LEN = 0;
        PENDING_OFF = 0;
    }
}

/// キューを捨てる操作(locate / flush_after / stop)の後に呼ぶ。未受理分を
/// 破棄し、seq_filled_until() から供給を再開する(§5 の契約)。
fn resync_after_discard() {
    unsafe {
        drop_pending();
        let filled = hostapi_seq_filled_until().max(0) as u32;
        NEXT_BEAT = filled / BEAT + 1;
    }
}

/// L2 の供給ループ(docs/hostapi.md §10)。プレフィックス受理なので、
/// 受理されなかった残りは PENDING に持ち越して次 tick で再送する。
fn supply(now_tick: u32) {
    unsafe {
        loop {
            if PENDING_OFF == PENDING_LEN {
                if hostapi_seq_filled_until() >= (now_tick + HORIZON) as i32 {
                    return;
                }
                PENDING_LEN = build_beat(NEXT_BEAT, &mut *core::ptr::addr_of_mut!(PENDING));
                PENDING_OFF = 0;
                NEXT_BEAT += 1;
                if PENDING_LEN == 0 {
                    return;
                }
            }
            let remain = PENDING_LEN - PENDING_OFF;
            let ptr = (core::ptr::addr_of!(PENDING) as *const SeqEvent).add(PENDING_OFF) as *const u8;
            let n = hostapi_seq_write(ptr, (remain * 16) as u32);
            if n < 0 {
                return;
            }
            PENDING_OFF += n as usize;
            if (n as usize) < remain {
                return; // キュー満杯。次の tick で残りを再送する
            }
        }
    }
}

/// stage 9(V1)の小節 k の開始 song tick(2/8 と 3/8 の交互)
fn v1_bar_start(k: u32) -> u32 {
    (k / 2) * V1_PAIR + (k % 2) * V1_SHORT
}

/// song tick が属する stage 9 の小節番号
fn v1_bar_of(song: u32) -> u32 {
    2 * (song / V1_PAIR) + if song % V1_PAIR >= V1_SHORT { 1 } else { 0 }
}

/// 小節 k のテンポと拍子を予約する。どちらかが -1 なら false
fn v1_set_bar(k: u32) -> bool {
    let at = v1_bar_start(k) as i32;
    let (tempo, numer) = if k % 2 == 0 { (V1_TEMPO_EVEN, 2) } else { (V1_TEMPO_ODD, 3) };
    unsafe { hostapi_tempomap_set_tempo(at, tempo) == 0 && hostapi_tempomap_set_meter(at, numer, 8) == 0 }
}

/// 次の検査のために transport を頭から始め直す(供給もやり直す)
fn restart_transport() -> bool {
    drop_pending();
    unsafe {
        NEXT_BEAT = 0;
        hostapi_transport_start() == 0
    }
}

/// PLAYING 中のステージ進行(stage 0〜3 / 6)。now_tick = playback tick、song = song tick
fn advance_playing(now_tick: u32, song: u32, upq: u32) {
    unsafe {
        match STAGE {
            // 1 小節走らせてから、次の小節頭に速いテンポを投入する
            0 => {
                if song >= BAR {
                    let at = (song / BAR + 1) * BAR;
                    if hostapi_tempomap_set_tempo(at as i32, TEMPO_FAST) == 0 {
                        PB_MARK = at;
                        STAGE = 1;
                    }
                }
            }
            // テンポが実際に切り替わったらループ範囲を設定する
            1 => {
                if upq == TEMPO_FAST as u32 {
                    CHK |= CHK_TEMPO;
                    let ls = (song / BAR + 1) * BAR;
                    if hostapi_tempomap_set_loop(ls as i32, (ls + BAR) as i32) == 0 {
                        PREV_SONG = song;
                        PREV_PB = now_tick;
                        WRAPS = 0;
                        STAGE = 2;
                    }
                }
            }
            // song tick が巻き戻り、playback tick は単調増加であること
            2 => {
                if song < PREV_SONG && now_tick > PREV_PB {
                    WRAPS += 1;
                }
                PREV_SONG = song;
                PREV_PB = now_tick;
                if WRAPS >= 2 {
                    CHK |= CHK_LOOP;
                    hostapi_tempomap_set_loop(0, 0);
                    // seq_flush_after: 先読み済みの未発火分が実際に減ることを確認。
                    // filled_until を「前後で減ったか」で見る(キューが空になると
                    // 現在 playback tick が返り、その値は時々刻々進むため)
                    let filled_before = hostapi_seq_filled_until();
                    let removed = hostapi_seq_flush_after(now_tick as i32);
                    if removed > 0 && hostapi_seq_filled_until() < filled_before {
                        CHK |= CHK_FLUSH;
                    }
                    hostapi_transport_locate(LOCATE_TARGET as i32);
                    resync_after_discard();
                    PB_MARK = now_tick;
                    STAGE = 3;
                }
            }
            // locate 後: song が移動し、playback tick は戻っていないこと
            3 => {
                if song >= LOCATE_TARGET && now_tick >= PB_MARK {
                    CHK |= CHK_LOCATE;
                    if now_tick >= PB_MARK + BEAT {
                        SONG_AT_STOP = song;
                        PB_AT_STOP = now_tick;
                        if hostapi_transport_stop() == 0 {
                            CHK |= CHK_STOP;
                        }
                        RUNNING = false;
                        resync_after_discard();
                        STAGE = 5;
                    }
                }
            }
            // continue 後: 停止点から継続していること。2 小節走らせて止め、stage 8 へ
            6 => {
                if now_tick >= PB_AT_STOP && song >= SONG_AT_STOP {
                    CHK |= CHK_CONT;
                }
                if now_tick >= PB_AT_STOP + BAR * 2 {
                    hostapi_transport_stop();
                    RUNNING = false;
                    resync_after_discard();
                    STAGE = 8;
                }
            }
            _ => {}
        }
    }
}

/// STOPPED 中に進めるステージ(stage 5 / 8 / 10)
fn advance_stopped(host_us: u64) {
    unsafe {
        match STAGE {
            // STOPPED 中の time_us_to_tick は -1、その後 continue する
            5 => {
                if hostapi_time_us_to_tick(host_us as i64) == -1 {
                    CHK |= CHK_U2T_STOPPED;
                }
                if hostapi_transport_continue() == 0 {
                    RUNNING = true;
                    resync_after_discard();
                    STAGE = 6;
                }
            }
            // tempomap_clear(STOPPED で 0・既定値に戻る)→ V1 を始める
            8 => {
                CLEAR_STOPPED_OK = false;
                if hostapi_tempomap_clear() == 0 {
                    let mut pos = [0u8; 32];
                    if hostapi_transport_get_position(pos.as_mut_ptr(), 32) == 0 {
                        let upq = u32::from_le_bytes([pos[20], pos[21], pos[22], pos[23]]);
                        CLEAR_STOPPED_OK = upq == TEMPO_DEFAULT as u32;
                    }
                }
                V1_WRITTEN = 0;
                SAMPLES = 0;
                CHECK_FAIL = !v1_set_bar(0);
                // 始める前に先読みの範囲まで予約しておく。テンポが速いので、最初の app_tick(100ms 後)には
                // song がもう小節 1 の頭を過ぎていて、過去の tick には書けない(-1)
                while V1_WRITTEN < V1_BARS && v1_bar_start(V1_WRITTEN + 1) <= V1_LOOKAHEAD {
                    V1_WRITTEN += 1;
                    if !v1_set_bar(V1_WRITTEN) {
                        CHECK_FAIL = true;
                    }
                }
                V1_FIRST = true;
                if restart_transport() {
                    RUNNING = true;
                    STAGE = 9;
                }
            }
            // clear → 基本テンポ・4/4 で始め直し、STOP_AT に OP_STOP を予約する。
            // V1 で畳み込みが起きているので、clear が効いていなければ at_tick=0 へは書けない
            10 => {
                SAMPLES = 0;
                CHECK_FAIL = !(hostapi_tempomap_clear() == 0
                    && hostapi_tempomap_set_tempo(0, TEMPO_BASE) == 0
                    && hostapi_tempomap_set_meter(0, 4, 4) == 0);
                if restart_transport() {
                    // transport_start はキューを空にするので、OP_STOP は start の後に積む
                    let mut stop = SeqEvent::zero();
                    stop.tick = STOP_AT;
                    stop.status = OP_STOP;
                    STOP_WRITTEN = hostapi_seq_write(&stop as *const SeqEvent as *const u8, 16) == 1;
                    RUNNING = true;
                    STAGE = 11;
                }
            }
            _ => {}
        }
    }
}

/// PLAYING 中に進めるステージ(stage 9 / 11)
fn advance_playing_late(now_tick: u32, song: u32, bar: u32, beat: u32, upq: u32, state: u32) {
    unsafe {
        match STAGE {
            // V1: 上限 32 件の 3 倍を超える変化を通過させる
            9 => {
                if V1_FIRST {
                    V1_FIRST = false;
                    // PLAYING 中の clear は -1
                    if CLEAR_STOPPED_OK && hostapi_tempomap_clear() == -1 {
                        CHK |= CHK_CLEAR;
                    }
                }
                while V1_WRITTEN < V1_BARS && v1_bar_start(V1_WRITTEN + 1) <= song + V1_LOOKAHEAD {
                    V1_WRITTEN += 1;
                    if !v1_set_bar(V1_WRITTEN) {
                        CHECK_FAIL = true;
                    }
                }
                let k = v1_bar_of(song);
                let off = song - v1_bar_start(k);
                let len = if k % 2 == 0 { V1_SHORT } else { V1_PAIR - V1_SHORT };
                if k < V1_BARS && off >= EDGE_MARGIN && off + EDGE_MARGIN <= len {
                    let tempo = (if k % 2 == 0 { V1_TEMPO_EVEN } else { V1_TEMPO_ODD }) as u32;
                    if bar != k || beat != off / V1_BEAT || upq != tempo {
                        if V1_MISS == 0 {
                            V1_MISS_BAR = k;
                        }
                        V1_MISS += 1;
                        CHECK_FAIL = true;
                    }
                    SAMPLES += 1;
                    V1_SAMPLES = SAMPLES;
                }
                if song >= v1_bar_start(V1_BARS) {
                    if !CHECK_FAIL && SAMPLES >= 50 {
                        CHK |= CHK_NOEXHAUST;
                    }
                    hostapi_transport_stop();
                    RUNNING = false;
                    resync_after_discard();
                    STAGE = 10;
                }
            }
            // V2 + V3: 前回の予約が混ざらないこと、OP_STOP で止まること
            11 => {
                if state == TRANSPORT_STOPPED {
                    if STOP_WRITTEN && song == STOP_AT && now_tick == STOP_AT {
                        CHK |= CHK_STOPAT;
                    }
                    if !CHECK_FAIL && SAMPLES >= 10 {
                        CHK |= CHK_RESTART;
                    }
                    finish_part1();
                    return;
                }
                if song < STOP_AT {
                    let off = song % BAR;
                    if off >= EDGE_MARGIN && off + EDGE_MARGIN <= BAR {
                        if bar != song / BAR || beat != off / BEAT || upq != TEMPO_BASE as u32 {
                            CHECK_FAIL = true;
                        }
                        SAMPLES += 1;
                    }
                }
                if now_tick > STOP_AT + BAR * 2 {
                    hostapi_transport_stop(); // OP_STOP が効かなかった
                    finish_part1();
                }
            }
            _ => {}
        }
    }
}

// ---------------------------------------------------------------------------
// 第 1 部: その他(起動時にまとめて実行する)
// ---------------------------------------------------------------------------

fn fs_read(path: &[u8], buf: &mut [u8]) -> i32 {
    unsafe { hostapi_fs_read(path.as_ptr(), path.len() as u32, buf.as_mut_ptr(), buf.len() as u32) }
}

fn fs_write(path: &[u8], buf: &[u8]) -> i32 {
    unsafe { hostapi_fs_write(path.as_ptr(), path.len() as u32, buf.as_ptr(), buf.len() as u32) }
}

fn check_fs() {
    let mut data = [0u8; 100];
    for (i, b) in data.iter_mut().enumerate() {
        *b = (i as u8).wrapping_mul(37).wrapping_add(11);
    }
    let mut buf = [0u8; 128];
    let mut ok = fs_write(FS_NAME, &data) == 0
        && fs_read(FS_NAME, &mut buf) == data.len() as i32
        && buf[..data.len()] == data[..];
    unsafe {
        if ok {
            CHK |= CHK_FSW;
        }
    }
    // 切り詰め読み(ファイルが buf より大きくても成功する)、無いファイル、不正なパス
    let mut small = [0u8; 10];
    ok = fs_read(FS_NAME, &mut small) == 10 && small[..] == data[..10];
    ok &= fs_read(FS_MISSING, &mut small) == -1;
    for bad in FS_BAD.iter() {
        ok &= fs_read(bad, &mut small) == -1;
        ok &= fs_write(bad, &data[..4]) == -1;
    }
    unsafe {
        if ok {
            CHK |= CHK_FSR;
        }
    }
}

fn check_tone() {
    let ok = unsafe {
        hostapi_tone_define(ACCENT_SLOT as i32, 0 /*SINE*/, 1568, 30, 100) == 0
            && hostapi_tone_define(8, 0, 1000, 30, 100) == -1 // スロット範囲外
            && hostapi_tone_define(2, 9, 1000, 30, 100) == -1 // 未知の波形
            && hostapi_tone_play(ACCENT_SLOT as i32) == 0
            && hostapi_tone_play(8) == -1
    };
    unsafe {
        hostapi_play_click(); // 戻り値なし。呼べてトラップしないこと
        if ok {
            CHK |= CHK_TONE;
        }
    }
}

fn check_audio() {
    unsafe {
        let ok = hostapi_audio_play(AUDIO_MISSING.as_ptr(), AUDIO_MISSING.len() as u32) == -1
            && hostapi_audio_get_state() == AUDIO_ERROR
            && hostapi_audio_ctrl(AUDIO_CMD_PAUSE) == -1
            && hostapi_audio_ctrl(AUDIO_CMD_STOP) == 0
            && hostapi_audio_get_state() == AUDIO_STOPPED;
        if ok {
            CHK |= CHK_AUDIO;
        }
    }
}

fn drain_rx() {
    unsafe {
        let mut recs = [RecvRec { timestamp_us: 0, byte: 0, _reserved: [0; 7] }; 16];
        loop {
            let n = hostapi_midi_recv(recs.as_mut_ptr() as *mut u8,
                                      (16 * core::mem::size_of::<RecvRec>()) as u32);
            if n < 0 {
                MIDI_RECV_OK = false;
                return;
            }
            RX_BYTES += n as u32;
            if (n as usize) < 16 {
                return;
            }
        }
    }
}

fn tick_now() {
    unsafe {
        let now = hostapi_now_ms();
        if now < NOW_PREV {
            NOW_OK = false;
        }
        NOW_PREV = now;
    }
}

/// 第 1 部の終わり: 残りの判定を確定させ、不合格の項目を FAILED に移す
fn finish_part1() {
    unsafe {
        RUNNING = false;
        drop_pending();
        STAGE = 12;
        if MIDI_SEND_OK && MIDI_RECV_OK {
            CHK |= CHK_MIDI;
        }
        if NOW_OK && NOW_PREV.wrapping_sub(NOW_START) >= NOW_MIN_ELAPSED_MS {
            CHK |= CHK_NOW;
        }
        FAILED = PART1 & !CHK;
        PART1_DONE = true;
    }
}

// ---------------------------------------------------------------------------
// 第 2 部: 注入したタップ / キー
// ---------------------------------------------------------------------------

fn in_target(x: i32, y: i32) -> bool {
    x >= TARGET_X && x < TARGET_X + TARGET_W && y >= TARGET_Y && y < TARGET_Y + TARGET_H
}

fn on_event(ev: &Event) {
    let (x, y) = (ev.x as i32, ev.y as i32);
    unsafe {
        match ev.ev_type {
            EV_TOUCH_DOWN => {
                T_DOWNS += 1;
                G_ACTIVE = true;
                G_DOWN_X = x;
                G_DOWN_Y = y;
                G_LAST_X = x;
                G_LAST_Y = y;
                G_MOVES = 0;
                G_MOVE_OK = true;
            }
            EV_TOUCH_MOVE => {
                T_MOVES += 1;
                if G_ACTIVE {
                    let d = (x - G_LAST_X).abs().max((y - G_LAST_Y).abs());
                    if d < MOVE_MIN_PX {
                        G_MOVE_OK = false;
                    }
                    G_MOVES += 1;
                    G_LAST_X = x;
                    G_LAST_Y = y;
                }
            }
            EV_TOUCH_UP => {
                T_UPS += 1;
                if G_ACTIVE {
                    // タップ: 的の中で押して同じ座標で離し、MOVE なし
                    if G_MOVES == 0 && x == G_DOWN_X && y == G_DOWN_Y && in_target(x, y) {
                        CHK |= CHK_TOUCH;
                    }
                    // ドラッグ: 右へ DRAG_MIN_DX 以上、MOVE が 1 件以上で間引きどおり、縦にずれない
                    if G_MOVES >= 1 && G_MOVE_OK && x - G_DOWN_X >= DRAG_MIN_DX && y == G_DOWN_Y {
                        CHK |= CHK_MOVE;
                    }
                }
                G_ACTIVE = false;
            }
            _ => {}
        }
        T_LAST_X = x;
        T_LAST_Y = y;
    }
}

#[no_mangle]
pub extern "C" fn app_key(key: i32, action: i32) -> i32 {
    unsafe {
        if key == KEY_BACK && action == KEY_ACTION_CLICK {
            CHK |= CHK_KEY;
        }
    }
    1 // 処理した(止まらない)
}

// ---------------------------------------------------------------------------
// 表示
// ---------------------------------------------------------------------------

fn draw_checks() {
    unsafe {
        for row in 0..4 {
            let mut l = Line::new();
            for i in 0..6 {
                let k = row * 6 + i;
                if k >= CHECKS.len() {
                    break;
                }
                let (name, bit) = CHECKS[k];
                l.push(name).push(if CHK & bit != 0 { b"=o " } else { b"=- " });
            }
            l.pad(36).draw(12, 40 + 16 * row as i32);
        }
    }
}

fn draw_touch() {
    unsafe {
        let mut l = Line::new();
        l.push(b"t ").push_i32(T_LAST_X).push(b",").push_i32(T_LAST_Y)
         .push(b" d").push_u32(T_DOWNS).push(b" u").push_u32(T_UPS).push(b" m").push_u32(T_MOVES);
        l.pad(24).draw(12, 146);
    }
}

fn draw_diag() {
    unsafe {
        let mut l = Line::new();
        l.push(b"v1 n").push_u32(V1_SAMPLES).push(b" miss").push_u32(V1_MISS)
         .push(b" at").push_u32(V1_MISS_BAR).push(b" rx").push_u32(RX_BYTES);
        l.pad(36).draw(12, 112);
    }
}

fn draw_result() {
    unsafe {
        let mut l = Line::new();
        if !PART1_DONE {
            l.push(b"RESULT ... stage ").push_u32(STAGE as u32);
        } else if FAILED != 0 {
            l.push(b"RESULT FAIL");
            for (name, bit) in CHECKS.iter() {
                if FAILED & bit != 0 {
                    l.push(b" ").push(name);
                }
            }
        } else if CHK & PART2 != PART2 {
            l.push(b"RESULT WAIT");
            for (name, bit) in CHECKS.iter() {
                if PART2 & bit != 0 && CHK & bit == 0 {
                    l.push(b" ").push(name);
                }
            }
        } else {
            l.push(b"RESULT PASS");
        }
        l.pad(36).draw(12, 220);
    }
}

// ---------------------------------------------------------------------------
// エントリ
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn app_init() -> i32 {
    unsafe {
        hostapi_fill_rect(0, 0, 320, 30, 0x30_50_90);
        hostapi_fill_rect(0, 30, 320, 210, 0x10_18_28);
        hostapi_fill_rect(TARGET_X, TARGET_Y, TARGET_W, TARGET_H, 0x20_60_40);
        let title = b"hostapi_check";
        hostapi_draw_text(12, 8, title.as_ptr(), title.len() as u32);
        let tap = b"TAP";
        hostapi_draw_text(TARGET_X + 33, TARGET_Y + 20, tap.as_ptr(), tap.len() as u32);
        let rgb = b"rgb 40c0ff";
        hostapi_draw_text_rgb(12, 166, rgb.as_ptr(), rgb.len() as u32, RGB_LINE);

        NOW_START = hostapi_now_ms();
        NOW_PREV = NOW_START;
        let cc = [0xB0u8, 0x77, 0x00];
        MIDI_SEND_OK = hostapi_midi_send(cc.as_ptr(), 3) == 0;
    }
    check_fs();
    check_tone();
    check_audio();

    // 音楽時間軸: 時間軸を空にしてから始める(前回の V1 で畳み込みが起きていると、
    // clear しない限り at_tick=0 へは書けない。Phase 17)
    unsafe {
        hostapi_tempomap_clear();
        hostapi_tempomap_set_tempo(0, TEMPO_BASE);
        hostapi_tempomap_set_meter(0, 4, 4);
        hostapi_tempomap_set_loop(0, 0);
        NEXT_BEAT = 0;
        STAGE = 0;
        drop_pending();
        RUNNING = hostapi_transport_start() == 0;
    }
    draw_checks();
    draw_touch();
    draw_result();
    0
}

#[no_mangle]
pub extern "C" fn app_tick() {
    drain_rx();
    tick_now();

    let mut evs = [Event { ev_type: 0, param: 0, x: 0, y: 0, time_ms: 0 }; 16];
    let n = unsafe {
        hostapi_poll_event(evs.as_mut_ptr() as *mut u8, (16 * core::mem::size_of::<Event>()) as u32)
    };
    for ev in &evs[..n.max(0) as usize] {
        on_event(ev);
    }

    if unsafe { STAGE } < 12 {
        let mut pos = [0u8; 32];
        if unsafe { hostapi_transport_get_position(pos.as_mut_ptr(), 32) } == 0 {
            let host_us = u64::from_le_bytes([pos[0], pos[1], pos[2], pos[3],
                                              pos[4], pos[5], pos[6], pos[7]]);
            let now_tick = u32::from_le_bytes([pos[8], pos[9], pos[10], pos[11]]);
            let song = u32::from_le_bytes([pos[12], pos[13], pos[14], pos[15]]);
            let bar = u32::from_le_bytes([pos[16], pos[17], pos[18], pos[19]]);
            let upq = u32::from_le_bytes([pos[20], pos[21], pos[22], pos[23]]);
            let beat = u16::from_le_bytes([pos[24], pos[25]]);
            let state = u32::from_le_bytes([pos[28], pos[29], pos[30], pos[31]]);
            unsafe {
                if !RUNNING {
                    advance_stopped(host_us); // stage 5 / 8 / 10
                } else {
                    // time_us_to_tick(get_position の host_us) は同 tick を返すはず。
                    // 2 回の呼び出しの間に進む分だけずれるので余裕を持って判定する。
                    let t = hostapi_time_us_to_tick(host_us as i64);
                    if t > 0 {
                        let tu = t as u32;
                        let d = if tu > now_tick { tu - now_tick } else { now_tick - tu };
                        if d <= 100 {
                            CHK |= CHK_U2T;
                        }
                    }
                    advance_playing(now_tick, song, upq);
                    advance_playing_late(now_tick, song, bar, beat as u32, upq, state); // stage 9 / 11
                    if RUNNING {
                        supply(now_tick);
                    }
                }
            }
        }
    }

    draw_checks();
    draw_diag();
    draw_touch();
    draw_result();
}
