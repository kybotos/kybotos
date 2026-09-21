// synth_probe — 内蔵音源ポート(HOSTAPI_PORT_SYNTH)の検証用アプリ(Phase 21)。
//
// タップ不要。app_init から 120bpm・4/4 で再生を始め、4 小節を 1 周として
// 次のパターンを繰り返す。判定は耳(実機)と WAV(Linux、MIDIBOX_WAV_OUT)で行う。
//
//   小節 1: 4 音を **同じ tick** に置く(1 拍目)。単声なら 1 つしか鳴らない
//   小節 2: 16 分のハイハット連打 16 発(ボイスの奪い合いが起きないこと)
//   小節 3: クリック(CLICK ポート)とドラムを重ねる(両方鳴ること)
//   小節 4: 1 拍目に **12 発**を同じ tick に置く(ボイス 8 本を溢れさせる)
//
// 回帰 6 本には入れない(docs/results/phase21.md 0-g)。
#![no_std]

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

extern "C" {
    fn hostapi_draw_text(x: i32, y: i32, ptr: *const u8, len: u32);
    fn hostapi_fill_rect(x: i32, y: i32, w: i32, h: i32, rgb888: u32);
    fn hostapi_transport_start() -> i32;
    fn hostapi_transport_stop() -> i32;
    fn hostapi_transport_get_position(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_tempomap_set_tempo(at_tick: i32, us_per_quarter: i32) -> i32;
    fn hostapi_tempomap_set_meter(at_tick: i32, numer: i32, denom: i32) -> i32;
    fn hostapi_tempomap_clear() -> i32;
    fn hostapi_seq_write(buf: *const u8, buf_len: u32) -> i32;
}

const PPQN: u32 = 960;
const BAR: u32 = PPQN * 4; // 4/4
const PORT_SYNTH: u8 = 2;
const PORT_CLICK: u8 = 3;
const OP_TONE: u8 = 1;

const KICK: u8 = 36;
const SNARE: u8 = 38;
const CHH: u8 = 42;
const CRASH: u8 = 49;

#[repr(C)]
#[derive(Clone, Copy)]
struct SeqEvent {
    tick: u32,
    port: u8,
    status: u8,
    data1: u8,
    data2: u8,
    param: u32,
    _reserved: u32,
}

impl SeqEvent {
    const ZERO: SeqEvent =
        SeqEvent { tick: 0, port: 0, status: 0, data1: 0, data2: 0, param: 0, _reserved: 0 };

    fn drum(tick: u32, note: u8, vel: u8) -> SeqEvent {
        SeqEvent { tick, port: PORT_SYNTH, status: 0x99, data1: note, data2: vel, ..SeqEvent::ZERO }
    }

    fn click(tick: u32, slot: u32) -> SeqEvent {
        SeqEvent { tick, port: PORT_CLICK, status: OP_TONE, param: slot, ..SeqEvent::ZERO }
    }
}

/// 1 小節ぶんを組み立てて積む。受理されなかった分は次の tick で再送する
const PEND_MAX: usize = 64;
static mut PEND: [SeqEvent; PEND_MAX] = [SeqEvent::ZERO; PEND_MAX];
static mut PEND_LEN: usize = 0;
static mut PEND_OFF: usize = 0;

fn push(e: SeqEvent) {
    unsafe {
        if PEND_LEN < PEND_MAX {
            PEND[PEND_LEN] = e;
            PEND_LEN += 1;
        }
    }
}

fn flush() {
    unsafe {
        while PEND_OFF < PEND_LEN {
            let remain = PEND_LEN - PEND_OFF;
            let ptr = (core::ptr::addr_of!(PEND) as *const SeqEvent).add(PEND_OFF) as *const u8;
            let n = hostapi_seq_write(ptr, (remain * 16) as u32);
            if n <= 0 {
                return;
            }
            PEND_OFF += n as usize;
        }
        PEND_LEN = 0;
        PEND_OFF = 0;
    }
}

/// 次に積む小節(0 始まり、4 小節で 1 周)
static mut NEXT_BAR: u32 = 0;

fn build_bar(bar: u32) {
    let t0 = bar * BAR;
    let s16 = PPQN / 4; // 16 分
    match bar % 4 {
        // 4 音を同じ tick に
        0 => {
            for n in [KICK, SNARE, CHH, CRASH] {
                push(SeqEvent::drum(t0, n, 110));
            }
        }
        // 16 分のハイハット連打
        1 => {
            for i in 0..16u32 {
                push(SeqEvent::drum(t0 + i * s16, CHH, 90));
            }
        }
        // クリックとドラムを重ねる
        2 => {
            for i in 0..4u32 {
                push(SeqEvent::click(t0 + i * PPQN, if i == 0 { 1 } else { 0 }));
                push(SeqEvent::drum(t0 + i * PPQN, if i % 2 == 0 { KICK } else { SNARE }, 110));
            }
        }
        // ボイスを溢れさせる(12 発 > 8 ボイス)
        _ => {
            for i in 0..12u32 {
                push(SeqEvent::drum(t0, if i % 2 == 0 { CHH } else { SNARE }, 100));
            }
        }
    }
    flush();
}

fn label(y: i32, s: &[u8]) {
    unsafe { hostapi_draw_text(8, y, s.as_ptr(), s.len() as u32) };
}

#[no_mangle]
pub extern "C" fn app_init() -> i32 {
    unsafe {
        hostapi_fill_rect(0, 0, 320, 240, 0x10_18_28);
        label(10, b"synth_probe (Phase 21)");
        label(40, b"1: 4 sounds on one tick");
        label(66, b"2: 16th hi-hats");
        label(92, b"3: click + drums");
        label(118, b"4: 12 hits, 8 voices");

        hostapi_transport_stop();
        hostapi_tempomap_clear();
        hostapi_tempomap_set_tempo(0, 500_000); // 120bpm
        hostapi_tempomap_set_meter(0, 4, 4);
        // **transport_start は L0 のキューを空にする**(docs/hostapi.md §3)ので、
        // 小節を積むのは start の**後**でなければならない(Phase 16 の教訓と同じ)
        hostapi_transport_start();
        NEXT_BAR = 0;
        build_bar(0);
        build_bar(1);
        NEXT_BAR = 2;
    }
    0
}

#[no_mangle]
pub extern "C" fn app_tick() {
    unsafe {
        flush();
        let mut buf = [0u8; 32];
        if hostapi_transport_get_position(buf.as_mut_ptr(), 32) != 0 {
            return;
        }
        let tick = u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]);
        // 今の小節の次まで積んであればよい(先読み 1 小節)
        let cur_bar = tick / BAR;
        while NEXT_BAR <= cur_bar + 1 {
            let b = NEXT_BAR;
            NEXT_BAR += 1;
            build_bar(b);
        }
        // 進行表示(小節番号)
        let n = cur_bar % 4 + 1;
        let s = [b'b', b'a', b'r', b' ', b'0' + n as u8];
        hostapi_draw_text(8, 160, s.as_ptr(), s.len() as u32);
    }
}

#[no_mangle]
pub extern "C" fn app_exit() {
    unsafe {
        hostapi_transport_stop();
    }
}
