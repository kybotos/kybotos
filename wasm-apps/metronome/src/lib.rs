// メトロノーム本体。Phase 13 で**音楽時間軸 API(transport / tempomap / seq)だけ**で
// 書き直した(移行ステップ 3。docs/architecture.md §10、docs/hostapi.md §6 要件 1)。
//
// 旧版(Phase 7B/7C/7D/8b)からの機能は維持する:
//   BPM 40-240(±5 / ±1、長押し連打加速)、拍子 2/3/4/6、START/STOP、拍ランプ、
//   小節頭のアクセント音。
//
// Phase 21c: 音を CLICK ポートから**内蔵音源(SYNTH)の note 34(小節頭)/ 33** へ移した
// (Click は「機能的なクリック」として既定 MUTE になったため)。音量 UI(Vol: / V- / V+)は
// 装置の設定(ミキサー)へ移ったので外した。音量はミキサーの Synth チャネルに乗る。
//
// 旧版との違い(内部だけ。使い勝手は同じ):
//   - クリックは `hostapi_tone_schedule` の毎 tick 再予約ではなく、
//     `seq_write`(21c から port=SYNTH / Note On)で **playback tick** に予約する。
//     供給はプレフィックス受理契約どおり(docs/hostapi.md §5 / §10)。
//   - MIDI Clock はホスト(L1)が 40 tick グリッドから生成する。**アプリは
//     Start/Stop も含めて MIDI を一切送らない**(`hostapi_midi_send` は使わない。
//     送るとクロックが二重に出る)。
//   - テンポ / 拍子の変更は「位置 0 へ locate してマップの at_tick=0 を上書きする」
//     方式。旧版の rearm(now)(変更した瞬間から小節をやり直す)と同じ意味論で、
//     テンポマップのエントリが増えない(SEQCORE_TEMPO_MAX = 32 の枯渇を避ける)。
//     詳細は docs/results/phase13.md のステップ 1。
//
// Phase 22b: Kybotos のサンプルアプリとしての統一感(Sequencer と同じヘッダ + ステータス行、▶ / ■、
// 対話規約の BPM・拍子の変え方)と、単機能アプリとしての簡素さ(BPM と拍子を巨大な数字で)に作り直す。
// 設計は docs/results/phase22b.md のステップ 0。配色はメニュー(shared/launcher_theme.h)に合わせ、本体は黒。
//
// ホスト API (module "env") のみ使用。no_std / アロケータ不要。
#![no_std]

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

extern "C" {
    fn hostapi_draw_text_rgb(x: i32, y: i32, ptr: *const u8, len: u32, rgb888: u32);
    fn hostapi_fill_rect(x: i32, y: i32, w: i32, h: i32, rgb888: u32);
    fn hostapi_poll_event(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_now_ms() -> u32;

    fn hostapi_transport_start() -> i32;
    fn hostapi_transport_stop() -> i32;
    fn hostapi_transport_locate(song_tick: i32) -> i32;
    fn hostapi_transport_get_position(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_tempomap_set_tempo(at_tick: i32, us_per_quarter: i32) -> i32;
    fn hostapi_tempomap_set_meter(at_tick: i32, numer: i32, denom: i32) -> i32;
    fn hostapi_seq_write(buf: *const u8, buf_len: u32) -> i32;
    fn hostapi_seq_filled_until() -> i32;
}

const PPQN: u32 = 960;
const BEAT: u32 = PPQN; // 4 分音符 = 1 拍(denom は常に 4)

// 内蔵音源(shared/hostapi_defs.h の SYNTH ポートの契約)。チャンネルは無視されるが GM に倣い ch10
const PORT_SYNTH: u8 = 2;
const NOTE_ON_CH10: u8 = 0x99;
const NOTE_METRO_CLICK: u8 = 33; // 通常拍
const NOTE_METRO_BELL: u8 = 34; // 小節頭(高く長い)
const VELOCITY: u8 = 127; // 強弱は音色の式で付ける(調整箇所を 1 つにするため)

#[repr(C)]
#[derive(Clone, Copy)]
struct Event {
    ev_type: u16,
    param: u16,
    x: i16,
    y: i16,
    time_ms: u32,
}

const EV_TOUCH_DOWN: u16 = 1;
const EV_TOUCH_UP: u16 = 2;

/// hostapi_seq_event_t(16 バイト、ABI 凍結)
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

// ---- 配色(Phase 22b。shared/launcher_theme.h のメニューの色から。本体は焼き付きを避けて黒)----
const HDR_BG: u32 = 0x18_3c_29; // ヘッダ: メニューの背景と同じ濃緑(MENU_BG_RGB888)
const STA_BG: u32 = 0x0b_14_0f; // ステータス行: 黒に近い緑
const BODY_BG: u32 = 0x00_00_00;
const TXT_CREAM: u32 = 0xf3_f1_e4; // 題名・数字(MENU_TITLE_RGB888)
// 状態の色は対話規約(docs/design/ui-conventions.md §4)。アプリをまたいで同じ
const TXT_ON: u32 = 0x40_e0_70; // ▶
const TXT_STOP: u32 = 0xf0_60_60; // ■
const BEAT_OFF: u32 = 0x1c_26_20; // 拍の枠(消灯)
const BEAT_ON: u32 = 0x8f_d1_8b; // 拍の枠(点灯): 若葉
const BEAT_ACCENT: u32 = 0xf0_a0_40; // 1 拍目: 橙(ステップ 0 の D6)

// ---- ヘッダ(26px)+ ステータス行(24px)。Sequencer と同じ骨格 ----
const HDR_H: i32 = 26;
const STA_Y: i32 = 26;
const STA_H: i32 = 24;
const BODY_Y: i32 = STA_Y + STA_H;
const TITLE_X: i32 = 8;
const TITLE_Y: i32 = 5;
const HDR_BPM_X: i32 = 256; // ヘッダ右の `120bpm`
const PLAY_X: i32 = 292; // ▶ / ■
const PLAY_Y: i32 = STA_Y + 4;
const PLAY_HIT_X: i32 = 240; // 当たり判定はステータス行の右 80px(D13)
const SYM_PLAY: &[u8] = b"\xEF\x81\x8B"; // U+F04B ▶
const SYM_STOP: &[u8] = b"\xEF\x81\x8D"; // U+F04D ■

// ---- 本体(ステップ 2〜4 で巨大な数字と拍の枠に置き換える)----
const LAMP_Y: i32 = 76;
const LAMP_H: i32 = 36;
const LAMP_W: i32 = 44;
const LAMP_GAP: i32 = 8;
const LAMP_X0: i32 = 12;
const MAX_BEATS: usize = 6;

const FINE_Y: i32 = 120;
const FINE_H: i32 = 44;
// -1 は BPM- の真上、+1 は BPM+ の真上(右の 2 枠は Phase 21c で V-/V+ を外して空けた)
const FINE_LABELS: [&[u8]; 2] = [b"-1", b"+1"];

const BTN_Y: i32 = 176;
const BTN_H: i32 = 52;
const BTN_W: i32 = 70;
const BTN_XS: [i32; 4] = [12, 90, 168, 246];
const BTN_LABELS: [&[u8]; 3] = [b"BPM-", b"BPM+", b"BEAT"];

const BPM_MIN: u32 = 40;
const BPM_MAX: u32 = 240;
const SIGS: [u32; 4] = [2, 3, 4, 6]; // 1 小節の拍数

// 長押し連打加速(Phase 7D)。押下直後に 1 ステップ、HOLD_INITIAL_DELAY_MS 後から
// 自動連打を開始し、保持時間に応じて 400ms→200ms→100ms へ縮める。
const HOLD_INITIAL_DELAY_MS: u32 = 500;
const HOLD_ACCEL_1_MS: u32 = 1500;
const HOLD_ACCEL_2_MS: u32 = 3000;
const HOLD_INTERVAL_1_MS: u32 = 400;
const HOLD_INTERVAL_2_MS: u32 = 200;
const HOLD_INTERVAL_3_MS: u32 = 100;

static mut BPM: u32 = 120;
static mut SIG_IDX: usize = 2; // 4 拍子
static mut RUNNING: bool = false;

// L2 の供給状態。OFFSET は song tick 0 に対応する playback tick
// (locate / start の直後に取り直す。それ以外では不変)
static mut OFFSET: u32 = 0;
static mut NEXT_BEAT: u32 = 0;
static mut LAMP_LIT: usize = usize::MAX;
static mut LAST_BEAT_KEY: u64 = u64::MAX;

// プレフィックス受理契約(docs/hostapi.md §5)の未受理分。1 拍 = 1 イベント
const CHUNK_MAX: usize = 1;
static mut PENDING: [SeqEvent; CHUNK_MAX] = [SeqEvent::zero(); CHUNK_MAX];
static mut PENDING_LEN: usize = 0;
static mut PENDING_OFF: usize = 0;

// 長押し連打の状態。HELD_DELTA==0 は「保持中の BPM ボタンなし」
static mut HELD_DELTA: i32 = 0;
static mut HELD_SINCE: u32 = 0;
static mut NEXT_REPEAT_AT: u32 = 0;

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
        let start = i;
        let n = digits.len() - start;
        for k in 0..n {
            let b = digits[start + k];
            if self.len < self.buf.len() {
                self.buf[self.len] = b;
                self.len += 1;
            }
        }
        self
    }
    fn draw(&self, x: i32, y: i32, rgb: u32) {
        unsafe { hostapi_draw_text_rgb(x, y, self.buf.as_ptr(), self.len as u32, rgb) };
    }
}

/// transport 位置(hostapi_position_t の必要フィールドだけ)
struct Pos {
    tick: u32,
    song_tick: u32,
    bar: u32,
    beat: u16,
}

fn get_position() -> Option<Pos> {
    let mut b = [0u8; 32];
    if unsafe { hostapi_transport_get_position(b.as_mut_ptr(), 32) } != 0 {
        return None;
    }
    Some(Pos {
        tick: u32::from_le_bytes([b[8], b[9], b[10], b[11]]),
        song_tick: u32::from_le_bytes([b[12], b[13], b[14], b[15]]),
        bar: u32::from_le_bytes([b[16], b[17], b[18], b[19]]),
        beat: u16::from_le_bytes([b[24], b[25]]),
    })
}

/// BPM → µs / 4 分音符(SMF の set tempo と同じ単位)。端数は四捨五入する。
/// 残差は L1 のアンカーからの絶対計算で吸収され、蓄積しない。
fn upq_of(bpm: u32) -> i32 {
    ((60_000_000u32 + bpm / 2) / bpm) as i32
}

fn beats_per_bar() -> u32 {
    unsafe { SIGS[SIG_IDX] }
}

/// キューを捨てる操作(start / stop / locate)の後に呼ぶ。未受理分を破棄し、
/// song tick 0 = 拍 0 から供給し直す(docs/hostapi.md §5 の契約)。
fn resync() {
    unsafe {
        PENDING_LEN = 0;
        PENDING_OFF = 0;
        NEXT_BEAT = 0;
        OFFSET = match get_position() {
            Some(p) => p.tick.wrapping_sub(p.song_tick),
            None => 0,
        };
        LAST_BEAT_KEY = u64::MAX;
    }
}

/// 拍 i(song tick = i * BEAT)のクリックイベントを組み立てる。
/// 小節頭は Metronome Bell(34)、他は Metronome Click(33)。
fn build_beat(i: u32, out: &mut [SeqEvent; CHUNK_MAX]) -> usize {
    unsafe {
        let note = if i % beats_per_bar() == 0 { NOTE_METRO_BELL } else { NOTE_METRO_CLICK };
        out[0] = SeqEvent {
            tick: OFFSET.wrapping_add(i.wrapping_mul(BEAT)),
            port: PORT_SYNTH,
            status: NOTE_ON_CH10,
            data1: note,
            data2: VELOCITY,
            param: 0,
            reserved: 0,
        };
    }
    1
}

/// L2 の供給ループ(docs/hostapi.md §10)。受理されなかった残りは PENDING に
/// 持ち越して次回再送する(プレフィックス受理契約)。
fn supply(now_tick: u32) {
    unsafe {
        let horizon = beats_per_bar() * BEAT * 2; // 2 小節先まで
        loop {
            if PENDING_OFF == PENDING_LEN {
                if hostapi_seq_filled_until() >= now_tick.wrapping_add(horizon) as i32 {
                    return;
                }
                PENDING_LEN = build_beat(NEXT_BEAT, &mut PENDING);
                PENDING_OFF = 0;
                NEXT_BEAT += 1;
            }
            let remain = PENDING_LEN - PENDING_OFF;
            let ptr = PENDING.as_ptr().add(PENDING_OFF) as *const u8;
            let n = hostapi_seq_write(ptr, (remain * 16) as u32);
            if n <= 0 {
                return; // キュー満杯。次の tick で残りを再送する
            }
            PENDING_OFF += n as usize;
        }
    }
}

/// 演奏中に「今この瞬間から小節をやり直す」。song tick を 0 へ戻すだけで、
/// MIDI クロックのグリッド(playback tick 基準)には触らない。
fn restart_bar() {
    unsafe {
        if !RUNNING {
            return;
        }
        hostapi_transport_locate(0); // キューの未発火イベントは破棄される
        resync();
        if let Some(p) = get_position() {
            supply(p.tick);
        }
    }
}

/// テンポ変更。演奏中は「位置 0 へ戻して at_tick=0 のエントリを上書き」する
/// (旧版の rearm(now) と同じ意味論。テンポマップのエントリが増えない)。
///
/// locate と set_tempo の 2 呼び出しの間に song tick が 1 tick でも進むと
/// 「過去の at_tick は変更できない」規則で -1 になるため、数回だけ試す
/// (1 tick は 120bpm で 520µs あり、実際にはまず起きない)。
fn apply_tempo() {
    unsafe {
        if !RUNNING {
            hostapi_tempomap_set_tempo(0, upq_of(BPM));
            return;
        }
        for _ in 0..4 {
            hostapi_transport_locate(0);
            if hostapi_tempomap_set_tempo(0, upq_of(BPM)) == 0 {
                break;
            }
        }
        resync();
        if let Some(p) = get_position() {
            supply(p.tick);
        }
    }
}

/// 拍子変更。マップは at_tick=0 の 1 エントリを上書きする。演奏中は旧版と同じく
/// その場で小節をやり直す。
fn apply_meter() {
    unsafe {
        hostapi_tempomap_set_meter(0, beats_per_bar() as i32, 4);
    }
    restart_bar();
}

fn text(x: i32, y: i32, s: &[u8], rgb: u32) {
    unsafe { hostapi_draw_text_rgb(x, y, s.as_ptr(), s.len() as u32, rgb) };
}

fn draw_status() {
    unsafe {
        // ヘッダ右は Sequencer と同じ `120bpm`(回帰が BPM を読む手がかりでもある。D7)
        let mut h = Line::new();
        h.push_u32(BPM).push(b"bpm");
        h.draw(HDR_BPM_X, TITLE_Y, TXT_CREAM);
        let mut l = Line::new();
        l.push(b"beats/bar: ").push_u32(SIGS[SIG_IDX]);
        l.draw(12, 56, TXT_CREAM);
    }
}

fn draw_lamps(lit: usize) {
    unsafe {
        let n = SIGS[SIG_IDX] as usize;
        for i in 0..MAX_BEATS {
            let x = LAMP_X0 + (i as i32) * (LAMP_W + LAMP_GAP);
            let color = if i >= n {
                BODY_BG // 拍子の外は背景色で消す
            } else if i == lit {
                if i == 0 { BEAT_ACCENT } else { BEAT_ON }
            } else {
                BEAT_OFF
            };
            hostapi_fill_rect(x, LAMP_Y, LAMP_W, LAMP_H, color);
        }
        LAMP_LIT = lit;
    }
}

/// ステータス行右の ▶ / ■(停止中は ▶ = 押すと鳴る、再生中は ■ = 押すと止まる。ui-conventions §4)
fn draw_run_button() {
    unsafe {
        if RUNNING {
            text(PLAY_X, PLAY_Y, SYM_STOP, TXT_STOP);
        } else {
            text(PLAY_X, PLAY_Y, SYM_PLAY, TXT_ON);
        }
    }
}

fn draw_buttons() {
    for i in 0..BTN_LABELS.len() {
        unsafe {
            hostapi_fill_rect(BTN_XS[i], BTN_Y, BTN_W, BTN_H, 0x3e_66_48);
        }
        text(BTN_XS[i] + 10, BTN_Y + 16, BTN_LABELS[i], TXT_CREAM);
    }
    draw_run_button();
}

fn draw_fine_buttons() {
    for i in 0..FINE_LABELS.len() {
        unsafe {
            hostapi_fill_rect(BTN_XS[i], FINE_Y, BTN_W, FINE_H, 0x24_48_3a);
        }
        text(BTN_XS[i] + 24, FINE_Y + 18, FINE_LABELS[i], TXT_CREAM);
    }
}

/// now_ms は wraparound しうるので、差分を符号付きで見て到達判定する
fn time_reached(now: u32, target: u32) -> bool {
    (now.wrapping_sub(target) as i32) >= 0
}

fn apply_bpm_delta(delta: i32) {
    unsafe {
        let new_bpm = (BPM as i32 + delta).clamp(BPM_MIN as i32, BPM_MAX as i32) as u32;
        if new_bpm != BPM {
            BPM = new_bpm;
            apply_tempo();
        }
    }
    draw_status();
}

fn start_repeat(delta: i32, now: u32) {
    apply_bpm_delta(delta);
    unsafe {
        HELD_DELTA = delta;
        HELD_SINCE = now;
        NEXT_REPEAT_AT = now.wrapping_add(HOLD_INITIAL_DELAY_MS);
    }
}

fn process_repeat(now: u32) {
    unsafe {
        if HELD_DELTA == 0 || !time_reached(now, NEXT_REPEAT_AT) {
            return;
        }
        let delta = HELD_DELTA;
        apply_bpm_delta(delta);
        let elapsed = now.wrapping_sub(HELD_SINCE);
        let interval: u32 = if elapsed < HOLD_ACCEL_1_MS {
            HOLD_INTERVAL_1_MS
        } else if elapsed < HOLD_ACCEL_2_MS {
            HOLD_INTERVAL_2_MS
        } else {
            HOLD_INTERVAL_3_MS
        };
        NEXT_REPEAT_AT = now.wrapping_add(interval);
    }
}

fn toggle_run() {
    unsafe {
        if RUNNING {
            hostapi_transport_stop(); // 0xFC を送出、キュー破棄、クロック停止
            RUNNING = false;
            PENDING_LEN = 0;
            PENDING_OFF = 0;
            NEXT_BEAT = 0;
            draw_lamps(usize::MAX);
        } else {
            // マップは常に at_tick=0 の 1 エントリ。start は song tick 0 から始まる
            hostapi_tempomap_set_meter(0, beats_per_bar() as i32, 4);
            hostapi_tempomap_set_tempo(0, upq_of(BPM));
            hostapi_transport_start(); // 0xFA を送出、クロック生成を開始
            RUNNING = true;
            resync();
            if let Some(p) = get_position() {
                supply(p.tick);
            }
        }
        draw_run_button();
        draw_status();
    }
}

fn handle_tap(x: i16, y: i16) {
    let (x, y) = (x as i32, y as i32);
    let now = unsafe { hostapi_now_ms() };

    // ステータス行の右 = ▶ / ■
    if y >= STA_Y && y < STA_Y + STA_H {
        if x >= PLAY_HIT_X {
            toggle_run();
        }
        return;
    }

    // -1 / +1(Phase 7D の行。V- / V+ は Phase 21c で外した)
    if y >= FINE_Y && y < FINE_Y + FINE_H {
        for i in 0..FINE_LABELS.len() {
            if x >= BTN_XS[i] && x < BTN_XS[i] + BTN_W {
                match i {
                    0 => start_repeat(-1, now),
                    1 => start_repeat(1, now),
                    _ => {}
                }
                break;
            }
        }
        return;
    }

    if y < BTN_Y || y >= BTN_Y + BTN_H {
        return;
    }
    unsafe {
        for i in 0..BTN_LABELS.len() {
            if x >= BTN_XS[i] && x < BTN_XS[i] + BTN_W {
                match i {
                    0 => start_repeat(-5, now),
                    1 => start_repeat(5, now),
                    2 => {
                        SIG_IDX = (SIG_IDX + 1) % SIGS.len();
                        apply_meter();
                        draw_lamps(usize::MAX);
                        draw_status();
                    }
                    _ => {}
                }
                break;
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn app_init() -> i32 {
    unsafe {
        hostapi_fill_rect(0, 0, 320, HDR_H, HDR_BG);
        hostapi_fill_rect(0, STA_Y, 320, STA_H, STA_BG);
        hostapi_fill_rect(0, BODY_Y, 320, 240 - BODY_Y, BODY_BG);
        text(TITLE_X, TITLE_Y, b"Metronome", TXT_CREAM);

        BPM = 120;
        SIG_IDX = 2;
        RUNNING = false;
        HELD_DELTA = 0;
        OFFSET = 0;
        NEXT_BEAT = 0;
        PENDING_LEN = 0;
        PENDING_OFF = 0;
        LAMP_LIT = usize::MAX;
        LAST_BEAT_KEY = u64::MAX;

        // テンポ / 拍子マップは常に at_tick=0 の 1 エントリだけを上書きして使う
        hostapi_tempomap_set_meter(0, beats_per_bar() as i32, 4);
        hostapi_tempomap_set_tempo(0, upq_of(BPM));
    }
    draw_status();
    draw_lamps(usize::MAX);
    draw_fine_buttons();
    draw_buttons();
    0
}

#[no_mangle]
pub extern "C" fn app_tick() {
    let mut evs = [Event { ev_type: 0, param: 0, x: 0, y: 0, time_ms: 0 }; 8];
    let n = unsafe {
        hostapi_poll_event(evs.as_mut_ptr() as *mut u8,
                           (8 * core::mem::size_of::<Event>()) as u32)
    };
    for ev in &evs[..n.max(0) as usize] {
        if ev.ev_type == EV_TOUCH_DOWN {
            handle_tap(ev.x, ev.y);
        } else if ev.ev_type == EV_TOUCH_UP {
            unsafe { HELD_DELTA = 0; }
        }
    }

    let now = unsafe { hostapi_now_ms() };
    process_repeat(now);

    unsafe {
        if !RUNNING {
            return;
        }
        let p = match get_position() {
            Some(p) => p,
            None => return,
        };

        // 2 小節先まで先読み供給する(実時間はアプリでは一切扱わない)
        supply(p.tick);

        // 拍ランプ(視覚は tick 格子で十分。最大 100ms 遅れる)
        let key = (p.bar as u64) * (beats_per_bar() as u64) + p.beat as u64;
        if key != LAST_BEAT_KEY {
            LAST_BEAT_KEY = key;
            draw_lamps(p.beat as usize);
        }
    }
}
