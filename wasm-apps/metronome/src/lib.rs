// メトロノーム本体。Phase 13 で**音楽時間軸 API(transport / tempomap / seq)だけ**で
// 書き直した(移行ステップ 3。docs/architecture.md §10、docs/hostapi.md §6 要件 1)。
//
// 機能: BPM 40-240、拍子 1〜16 / 2・4・8・16(Phase 22b。それまでは x/4 の 2/3/4/6)、▶ / ■、
//   拍の表示、小節頭のアクセント音。拍は分母の音符(6/8 は 8 分音符で 6 回)。
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
//   - Phase 22b: 演奏中の BPM の変更(シャトル)は**次の拍の song tick にテンポを書く**
//     (小節をやり直さない。1 拍に 1 件しか増えず、満杯ならホストが通過済みの区間を畳む。
//     docs/hostapi.md §4)。拍子の変更は**小節をやり直す**: stop → clear → 初期値 → start
//     (再生を始め直すときの契約どおり。Phase 13〜22a の「locate(0) して at_tick=0 を上書き」は、
//     先の位置に書いたテンポのエントリが残るのでやめた。docs/results/phase22b.md のステップ 3)。
//
// Phase 22b: Kybotos のサンプルアプリとしての統一感(Sequencer と同じヘッダ + ステータス行、▶ / ■、
// 対話規約の BPM・拍子の変え方)と、単機能アプリとしての簡素さ(BPM と拍子を巨大な数字で)に作り直す。
// 設計は docs/results/phase22b.md のステップ 0。配色はメニュー(shared/launcher_theme.h)に合わせ、本体は黒。
//
// ホスト API (module "env") のみ使用。no_std / アロケータ不要。
#![no_std]

use core::ptr::{addr_of, addr_of_mut};

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
    fn hostapi_transport_get_position(buf: *mut u8, buf_len: u32) -> i32;
    fn hostapi_tempomap_set_tempo(at_tick: i32, us_per_quarter: i32) -> i32;
    fn hostapi_tempomap_set_meter(at_tick: i32, numer: i32, denom: i32) -> i32;
    fn hostapi_tempomap_clear() -> i32;
    fn hostapi_seq_write(buf: *const u8, buf_len: u32) -> i32;
    fn hostapi_seq_filled_until() -> i32;
}

const PPQN: u32 = 960;

// 内蔵音源(shared/hostapi_defs.h の SYNTH ポートの契約)。チャンネルは無視されるが GM に倣い ch10
const PORT_SYNTH: u8 = 2;
const NOTE_ON_CH10: u8 = 0x99;
const NOTE_METRO_CLICK: u8 = 33; // 通常拍
const NOTE_METRO_BELL: u8 = 34; // 小節頭(高く長い)
const VELOCITY: u8 = 127; // 強弱は音色の式で付ける(調整箇所を 1 つにするため)

use appui::{Action, Gesture};

#[repr(C)]
#[derive(Clone, Copy)]
struct Event {
    ev_type: u16,
    param: u16,
    x: i16,
    y: i16,
    time_ms: u32,
}

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
const TXT_EDIT: u32 = 0xff_e0_60; // 値の変更中(点滅)
const TXT_HINT: u32 = 0x5f_74_66; // 手引き: くすんだ緑
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
const HINT_X: i32 = 8; // 停止中だけ出す手引き(D8)
const HINT_Y: i32 = STA_Y + 4;
const HDR_BPM_HIT_X: i32 = 200; // ヘッダ右の `120bpm` の長押しも BPM のシャトル(D3)
const SYM_PLAY: &[u8] = b"\xEF\x81\x8B"; // U+F04B ▶
const SYM_STOP: &[u8] = b"\xEF\x81\x8D"; // U+F04D ■

// ---- 本体: 巨大な数字(7 セグメントを fill_rect で描く。Host API に大きな文字は無い。D2)----
// BPM は左に 3 桁、拍子は右に分数(分子 / 線 / 分母、各 2 桁)。座標は docs/results/phase22b.md 0-1 の案 A
const BPM_X: i32 = 10;
const BPM_Y: i32 = 62;
const BPM_DW: i32 = 54; // 1 桁の幅
const BPM_DH: i32 = 104; // 1 桁の高さ
const BPM_T: i32 = 12; // 画の太さ
const BPM_GAP: i32 = 10;
const MET_X: i32 = 232;
const MET_NUM_Y: i32 = 60;
const MET_DEN_Y: i32 = 124;
const MET_DW: i32 = 30;
const MET_DH: i32 = 46;
const MET_T: i32 = 7;
const MET_GAP: i32 = 6;
const MET_LINE_X: i32 = 228;
const MET_LINE_Y: i32 = 114;
const MET_LINE_W: i32 = 84;
const MET_LINE_H: i32 = 4;
// 使わない画は一度も描かない(矩形のスロットを取らない)。拍子の 10 の位は「1」だけ(最大 16)、
// BPM の 100 の位は「1」「2」だけ(40〜240)。0x7f = 全部
const SEG_ONE: u8 = 0b0000110; // b c
const SEG_ONE_TWO: u8 = 0b1011111; // a b c d e g(f は使わない)
const SEG_ALL: u8 = 0b1111111;

// ---- 拍の枠(下端に 1 段。分子の数だけ並べ、全体の幅は一定。D5 とユーザーの変更)----
const BEAT_Y: i32 = 184;
const BEAT_H: i32 = 46;
const BEAT_X0: i32 = 8;
const BEAT_W: i32 = 304;
const BEAT_CELL_MAX: i32 = 72;
/// 番号を枠の中に出すのは 8 拍まで。文字のスロットも座標で引かれて解放されないので、
/// 出す位置の数(1〜8 拍で計 36)を抑える。9 拍以上は枠が細く 2 桁が入らない
const BEAT_NUM_MAX: u32 = 8;
const BEAT_NUM_Y: i32 = BEAT_Y + 15;
const BEAT_DIGIT_W: i32 = 8; // 番号の 1 桁の幅の目安(中央に置くため)

// 本体の当たり判定: 数字の左が BPM、右が拍子(y は本体の上から拍の枠の手前まで)
const BIG_Y1: i32 = 178;
const MET_HIT_X: i32 = 215;

const BPM_MIN: u32 = 40;
const BPM_MAX: u32 = 240;
const NUM_MAX: u32 = 16;
const DEN_TABLE: [u32; 4] = [2, 4, 8, 16];
/// BPM のシャトル(長押し + 左右ドラッグ)の段階表: 変位(px)→ 速さ(bpm/秒)。Sequencer と同じ
const SHUTTLE_STAGES: [(i32, u32); 3] = [(12, 1), (40, 4), (80, 12)];
/// 拍子のドラッグ: この px で 1 段(位置で決まる。左右 = 分子、上へ = 細かい音価)。Sequencer と同じ
const MET_STEP_PX: i32 = 16;
/// 変更中の点滅の周期(ui-conventions §4)
const BLINK_MS: u32 = 200;

static mut BPM: u32 = 120;
static mut NUM: u32 = 4;
static mut DEN: u32 = 4;
static mut RUNNING: bool = false;

// L2 の供給状態。OFFSET は song tick 0 に対応する playback tick
// (start の直後に取り直す。それ以外では不変)
static mut OFFSET: u32 = 0;
static mut NEXT_BEAT: u32 = 0;
// 描いてある数字と色(同じなら描き直さない)。u32::MAX = まだ描いていない
static mut SHOWN_BPM: u32 = u32::MAX;
static mut SHOWN_METER: u32 = u32::MAX;
static mut SHOWN_HINT: bool = false;
static mut LAST_BEAT_KEY: u64 = u64::MAX;
// 描いてある拍の枠(拍数と点いている拍)。u32::MAX / usize::MAX = まだ / 消灯
static mut SHOWN_BEATS: u32 = u32::MAX;
static mut SHOWN_LIT: usize = usize::MAX;

// プレフィックス受理契約(docs/hostapi.md §5)の未受理分。1 拍 = 1 イベント
const CHUNK_MAX: usize = 1;
static mut PENDING: [SeqEvent; CHUNK_MAX] = [SeqEvent::zero(); CHUNK_MAX];
static mut PENDING_LEN: usize = 0;
static mut PENDING_OFF: usize = 0;

// ---- 操作(appui のジェスチャ)----
#[derive(Clone, Copy, PartialEq, Eq)]
enum Target {
    None,
    Bpm,
    Meter,
    Play,
}
static mut GESTURE: Gesture = Gesture::new();
/// 押したときの的(押下の位置で決まり、離すまで変わらない)
static mut TARGET: Target = Target::None;
/// 長押しが成立して、値を変えている最中か(点滅させる)
static mut EDITING: bool = false;
/// シャトルの端数(mbpm)と、前回積分した時刻
static mut SHUTTLE_ACC: i32 = 0;
static mut SHUTTLE_LAST_MS: u32 = 0;
/// 拍子のドラッグの起点と、ドラッグ中の値(離したときに確定する。D11)
static mut MET_NUM0: u32 = 4;
static mut MET_DEN_IDX0: i32 = 1;
static mut EDIT_NUM: u32 = 4;
static mut EDIT_DEN: u32 = 4;

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

/// 1 拍の tick(拍 = 分母の音符)
fn beat_ticks() -> u32 {
    unsafe { PPQN * 4 / DEN }
}

/// キューを捨てる操作(start / stop)の後に呼ぶ。未受理分を破棄し、
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

/// 拍 i(song tick = i * 1 拍)のクリックイベントを組み立てる。拍子は再生中に変わらない
/// (変えたら始め直す)ので、拍の位置は song tick 0 からの掛け算で決まる。
/// 小節頭は Metronome Bell(34)、他は Metronome Click(33)。
fn build_beat(i: u32, out: &mut [SeqEvent; CHUNK_MAX]) -> usize {
    unsafe {
        let note = if i % NUM == 0 { NOTE_METRO_BELL } else { NOTE_METRO_CLICK };
        out[0] = SeqEvent {
            tick: OFFSET.wrapping_add(i.wrapping_mul(beat_ticks())),
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
        let horizon = NUM * beat_ticks() * 2; // 2 小節先まで(最大 32 拍)
        loop {
            if PENDING_OFF == PENDING_LEN {
                if hostapi_seq_filled_until() >= now_tick.wrapping_add(horizon) as i32 {
                    return;
                }
                PENDING_LEN = build_beat(NEXT_BEAT, &mut *addr_of_mut!(PENDING));
                PENDING_OFF = 0;
                NEXT_BEAT += 1;
            }
            let remain = PENDING_LEN - PENDING_OFF;
            let ptr = (addr_of!(PENDING) as *const SeqEvent).add(PENDING_OFF) as *const u8;
            let n = hostapi_seq_write(ptr, (remain * 16) as u32);
            if n <= 0 {
                return; // キュー満杯。次の tick で残りを再送する
            }
            PENDING_OFF += n as usize;
        }
    }
}

/// song tick 0 から鳴らし始める。再生中なら止めてから(= 小節をやり直す)。
/// 契約(docs/hostapi.md §4)どおり stop → clear → at_tick=0 の初期値 → start。
/// clear しないと、シャトルで先の位置に書いたテンポが次の再生で効く。
fn start_from_top() {
    unsafe {
        if RUNNING {
            hostapi_transport_stop();
        }
        hostapi_tempomap_clear();
        hostapi_tempomap_set_meter(0, NUM as i32, DEN as i32);
        hostapi_tempomap_set_tempo(0, upq_of(BPM));
        hostapi_transport_start(); // 0xFA を送出、クロック生成を開始
        RUNNING = true;
        resync();
        if let Some(p) = get_position() {
            supply(p.tick);
        }
    }
}

/// テンポ変更。停止中は初期値(at_tick=0)を書き換える。演奏中は**次の拍の頭**から
/// 新しいテンポにする(小節はやり直さない。D11)。同じ拍の間の変更は同じ at_tick の上書きなので、
/// エントリは 1 拍に 1 件しか増えない。キューのイベントは playback tick で積んであるので、
/// テンポが変わっても積み直さなくてよい。
fn apply_tempo() {
    unsafe {
        if !RUNNING {
            hostapi_tempomap_set_tempo(0, upq_of(BPM));
            return;
        }
        let Some(p) = get_position() else { return };
        let bt = beat_ticks();
        let mut at = (p.song_tick + bt - 1) / bt * bt;
        // 書くまでの間に拍の頭を過ぎると「過去の at_tick」で -1 になる。その次の拍へ
        for _ in 0..2 {
            if hostapi_tempomap_set_tempo(at as i32, upq_of(BPM)) == 0 {
                break;
            }
            at += bt;
        }
    }
}

/// 拍子の確定(ドラッグを離したとき)。演奏中は小節をやり直す(D11)
fn apply_meter(num: u32, den: u32) {
    unsafe {
        if num == NUM && den == DEN {
            return;
        }
        NUM = num;
        DEN = den;
        if RUNNING {
            start_from_top();
        } else {
            hostapi_tempomap_set_meter(0, NUM as i32, DEN as i32);
        }
    }
}

fn text(x: i32, y: i32, s: &[u8], rgb: u32) {
    unsafe { hostapi_draw_text_rgb(x, y, s.as_ptr(), s.len() as u32, rgb) };
}

/// 変更中の対象なら点滅色、そうでなければ通常色(ui-conventions §4: 値の変更中は文字色を黄と交互)
fn value_color(target: Target) -> u32 {
    unsafe {
        if EDITING && TARGET == target && (hostapi_now_ms() / BLINK_MS) % 2 == 0 {
            TXT_EDIT
        } else {
            TXT_CREAM
        }
    }
}

fn draw_status() {
    unsafe {
        // ヘッダ右は Sequencer と同じ `120bpm`(回帰が BPM を読む手がかりでもある。D7)
        let mut h = Line::new();
        h.push_u32(BPM).push(b"bpm");
        h.draw(HDR_BPM_X, TITLE_Y, value_color(Target::Bpm));
        // 手引きは停止中だけ(D8)。消すときは同じ座標に空文字
        let hint = !RUNNING;
        if hint != SHOWN_HINT {
            SHOWN_HINT = hint;
            text(HINT_X, HINT_Y, if hint { b"hold + drag" } else { b"" }, TXT_HINT);
        }
    }
    draw_big_numbers();
}

// 7 セグメントの画の並び(a = 上、b = 右上、c = 右下、d = 下、e = 左下、f = 左上、g = 中)。bit 0 = a
const SEG_DIGITS: [u8; 10] = [
    0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
    0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111,
];

/// 1 桁を 7 枚の矩形で描く。`digit` が None なら全部消す(先頭の桁の空白)。
///
/// 角を埋めた字形(横画は全幅、縦画は横画に重ねる)にするが、**スロットは (x, y) で引かれる**ので
/// 7 枚の左上が重ならないようにする: a と f、g と e は左上が同じになるので、f と e を 1px 下げる
/// (a / g が消えているときだけ 1px 欠ける。該当は「4」の左上のみ)。消す画は同じ座標に w = h = 0
/// (ui-conventions §3.5)。
fn draw_digit(x: i32, y: i32, w: i32, h: i32, t: i32, digit: Option<u32>, used: u8, rgb: u32) {
    let hv = (h - 3 * t) / 2; // 縦画 1 本ぶんの、横画に挟まれた長さ
    let segs: [(i32, i32, i32, i32); 7] = [
        (x, y, w, t),                              // a
        (x + w - t, y, t, 2 * t + hv),             // b
        (x + w - t, y + t + hv, t, h - t - hv),    // c
        (x, y + h - t, w, t),                      // d
        (x, y + t + hv + 1, t, h - t - hv - 1),    // e
        (x, y + 1, t, 2 * t + hv - 1),             // f
        (x, y + t + hv, w, t),                     // g
    ];
    let bits = digit.map_or(0, |d| SEG_DIGITS[d as usize % 10]);
    for (i, &(sx, sy, sw, sh)) in segs.iter().enumerate() {
        if used & (1 << i) == 0 {
            continue; // この桁では出ない画(スロットを取らない)
        }
        let on = bits & (1 << i) != 0;
        unsafe {
            if on {
                hostapi_fill_rect(sx, sy, sw, sh, rgb);
            } else {
                hostapi_fill_rect(sx, sy, 0, 0, rgb);
            }
        }
    }
}

/// `v` を右詰め `n` 桁で描く(上の桁の 0 は空白)。`top` は最上位の桁で使う画(下の桁は全部)
fn draw_number(x: i32, y: i32, w: i32, h: i32, t: i32, gap: i32, n: u32, v: u32, top: u8, rgb: u32) {
    let mut div = 1;
    for _ in 1..n {
        div *= 10;
    }
    for i in 0..n {
        let d = (v / div) % 10;
        let blank = d == 0 && v < div && div > 1;
        let used = if i == 0 { top } else { SEG_ALL };
        draw_digit(x + i as i32 * (w + gap), y, w, h, t, if blank { None } else { Some(d) }, used, rgb);
        div /= 10;
    }
}

/// 巨大な BPM と拍子。値が変わったときだけ描く(矩形のスロットの書き換えを減らす)
fn draw_big_numbers() {
    unsafe {
        // キャッシュの鍵は値と色(点滅は色だけが変わる)
        let c = value_color(Target::Bpm);
        let key = BPM | (c << 8);
        if SHOWN_BPM != key {
            SHOWN_BPM = key;
            draw_number(BPM_X, BPM_Y, BPM_DW, BPM_DH, BPM_T, BPM_GAP, 3, BPM, SEG_ONE_TWO, c);
        }
        // 拍子はドラッグ中なら確定前の値を出す
        let (num, den) = if EDITING && TARGET == Target::Meter { (EDIT_NUM, EDIT_DEN) } else { (NUM, DEN) };
        let c = value_color(Target::Meter);
        let key = (num * 100 + den) | (c << 8);
        if SHOWN_METER != key {
            SHOWN_METER = key;
            draw_number(MET_X, MET_NUM_Y, MET_DW, MET_DH, MET_T, MET_GAP, 2, num, SEG_ONE, c);
            hostapi_fill_rect(MET_LINE_X, MET_LINE_Y, MET_LINE_W, MET_LINE_H, c);
            draw_number(MET_X, MET_DEN_Y, MET_DW, MET_DH, MET_T, MET_GAP, 2, den, SEG_ONE, c);
        }
    }
}

/// 拍の枠 j の左端と右端(拍数 n のとき)
fn beat_cell(n: u32, j: u32) -> (i32, i32) {
    let n = n as i32;
    let gap = if n <= 8 { 4 } else { 2 };
    let cw = ((BEAT_W - gap * (n - 1)) / n).min(BEAT_CELL_MAX);
    let total = cw * n + gap * (n - 1);
    let l = BEAT_X0 + (BEAT_W - total) / 2 + j as i32 * (cw + gap);
    (l, l + cw)
}

/// 塗り重ねの層 s の起点の x。**層ごとに違う座標にする**(スロットは (x, y) で引かれる)。画面の左外
fn beat_layer_x(s: u32) -> i32 {
    -1 - s as i32
}

fn beat_color(j: u32, lit: usize) -> u32 {
    if j as usize == lit {
        if j == 0 { BEAT_ACCENT } else { BEAT_ON }
    } else {
        BEAT_OFF
    }
}

/// 枠 j の色付きの層(右端まで)を描く。層は「最後の枠から先に」重ねるので、枠 j は層 2 * (n - 1 - j)
fn draw_beat_cell(n: u32, j: u32, lit: usize) {
    let s = 2 * (n - 1 - j);
    let x = beat_layer_x(s);
    let (_, r) = beat_cell(n, j);
    unsafe { hostapi_fill_rect(x, BEAT_Y, r - x, BEAT_H, beat_color(j, lit)) };
}

/// 枠の番号(点いている枠だけ。8 拍まで)
fn draw_beat_number(n: u32, j: u32, on: bool) {
    if n > BEAT_NUM_MAX {
        return;
    }
    let (l, r) = beat_cell(n, j);
    let x = (l + r) / 2 - BEAT_DIGIT_W / 2;
    let mut t = Line::new();
    if on {
        t.push_u32(j + 1);
    }
    t.draw(x, BEAT_NUM_Y, BODY_BG);
}

/// 拍の表示。分子の数だけ枠を並べ、`lit` の枠を点ける(usize::MAX なら全部消灯)。
///
/// **枠の位置は拍子で変わるが、スロットは座標で引かれて解放されない**ので、枠ごとに矩形を置くと
/// 拍子を変えるたびにスロットが増える(全拍子で 136 か所)。そこで**左端をそろえた矩形の塗り重ね**にする:
/// 最後の枠から順に「その枠の右端までの色付きの層」「その枠の左端までの黒の層」を重ねると、
/// 上の層が左側を塗り直すので、各枠と枠の間の隙間が残る。層の起点は画面の左外に 1px ずつずらして置き、
/// **スロットは常に 32 枚(16 拍 × 2)**。重なり順は最初に描いた順(app_init で層 0 から順に作る)。
fn draw_beats(lit: usize) {
    unsafe {
        let n = NUM;
        if SHOWN_BEATS != n {
            // 並びが変わった: 全部の層を描き直す
            if SHOWN_LIT != usize::MAX && SHOWN_BEATS != u32::MAX && SHOWN_LIT < SHOWN_BEATS as usize {
                draw_beat_number(SHOWN_BEATS, SHOWN_LIT as u32, false);
            }
            for s in 0..2 * NUM_MAX {
                let x = beat_layer_x(s);
                if s >= 2 * n {
                    hostapi_fill_rect(x, BEAT_Y, 0, 0, BODY_BG); // 使わない層
                } else if s % 2 == 0 {
                    draw_beat_cell(n, n - 1 - s / 2, lit);
                } else {
                    let (l, _) = beat_cell(n, n - 1 - s / 2);
                    hostapi_fill_rect(x, BEAT_Y, l - x, BEAT_H, BODY_BG);
                }
            }
            SHOWN_BEATS = n;
            SHOWN_LIT = lit;
            if lit < n as usize {
                draw_beat_number(n, lit as u32, true);
            }
            return;
        }
        if SHOWN_LIT == lit {
            return;
        }
        // 点く枠が変わっただけ: 前の枠と今の枠の色付きの層だけ描き直す
        if SHOWN_LIT < n as usize {
            draw_beat_cell(n, SHOWN_LIT as u32, lit);
            draw_beat_number(n, SHOWN_LIT as u32, false);
        }
        if lit < n as usize {
            draw_beat_cell(n, lit as u32, lit);
            draw_beat_number(n, lit as u32, true);
        }
        SHOWN_LIT = lit;
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

fn toggle_run() {
    unsafe {
        if RUNNING {
            hostapi_transport_stop(); // 0xFC を送出、キュー破棄、クロック停止
            RUNNING = false;
            PENDING_LEN = 0;
            PENDING_OFF = 0;
            NEXT_BEAT = 0;
            draw_beats(usize::MAX);
        } else {
            start_from_top();
        }
        draw_run_button();
        draw_status();
    }
}

/// 押した位置の的
fn target_at(x: i32, y: i32) -> Target {
    if y < HDR_H {
        return if x >= HDR_BPM_HIT_X { Target::Bpm } else { Target::None };
    }
    if y < BODY_Y {
        return if x >= PLAY_HIT_X { Target::Play } else { Target::None };
    }
    if y < BIG_Y1 {
        return if x >= MET_HIT_X { Target::Meter } else { Target::Bpm };
    }
    Target::None
}

fn den_index(den: u32) -> i32 {
    DEN_TABLE.iter().position(|&d| d == den).unwrap_or(1) as i32
}

/// BPM のシャトル: 変位で決まる速さ(bpm/秒)を時間で積分する。端数は mbpm で持ち越す
fn shuttle_bpm(dx: i32, now: u32) {
    unsafe {
        let dt = now.wrapping_sub(SHUTTLE_LAST_MS).min(500) as i32;
        SHUTTLE_LAST_MS = now;
        let mut speed = 0;
        for &(px, v) in SHUTTLE_STAGES.iter() {
            if dx.abs() >= px {
                speed = v as i32;
            }
        }
        if speed == 0 {
            SHUTTLE_ACC = 0;
            return;
        }
        SHUTTLE_ACC += speed * dt * dx.signum();
        let delta = SHUTTLE_ACC / 1000;
        if delta == 0 {
            return;
        }
        SHUTTLE_ACC -= delta * 1000;
        let new_bpm = (BPM as i32 + delta).clamp(BPM_MIN as i32, BPM_MAX as i32) as u32;
        if new_bpm != BPM {
            BPM = new_bpm;
            apply_tempo();
        }
    }
}

/// 拍子のドラッグ: 起点からの変位で値が決まる(位置)。確定は離したとき
fn drag_meter(dx: i32, dy: i32) {
    unsafe {
        EDIT_NUM = (MET_NUM0 as i32 + dx / MET_STEP_PX).clamp(1, NUM_MAX as i32) as u32;
        let idx = (MET_DEN_IDX0 - dy / MET_STEP_PX).clamp(0, DEN_TABLE.len() as i32 - 1);
        EDIT_DEN = DEN_TABLE[idx as usize];
    }
}

fn end_edit() {
    unsafe {
        if EDITING && TARGET == Target::Meter {
            apply_meter(EDIT_NUM, EDIT_DEN);
            draw_beats(usize::MAX);
        }
        EDITING = false;
        TARGET = Target::None;
    }
}

fn on_action(a: Action, now: u32) {
    unsafe {
        match a {
            Action::Press { x, y } => {
                TARGET = target_at(x, y);
                EDITING = false;
            }
            // ▶ / ■ はタップ(長押しに意味は無いので、長押しのまま離しても同じ。ui-conventions §2)
            Action::Tap { .. } | Action::LongPressFired { .. } if TARGET == Target::Play => {
                TARGET = Target::None;
                toggle_run();
            }
            Action::LongPressArmed { .. } if TARGET == Target::Bpm || TARGET == Target::Meter => {
                // 成立したら点滅(値の変更に入れる合図)
                EDITING = true;
                SHUTTLE_ACC = 0;
                SHUTTLE_LAST_MS = now;
                MET_NUM0 = NUM;
                MET_DEN_IDX0 = den_index(DEN);
                EDIT_NUM = NUM;
                EDIT_DEN = DEN;
            }
            Action::Shuttle { dx, dy } if EDITING => {
                if TARGET == Target::Bpm {
                    shuttle_bpm(dx, now);
                } else {
                    drag_meter(dx, dy);
                }
            }
            Action::ShuttleEnd | Action::LongPressFired { .. } | Action::Tap { .. } | Action::Cancel => {
                end_edit();
            }
            _ => {}
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
        NUM = 4;
        DEN = 4;
        RUNNING = false;
        GESTURE = Gesture::new();
        TARGET = Target::None;
        EDITING = false;
        OFFSET = 0;
        NEXT_BEAT = 0;
        PENDING_LEN = 0;
        PENDING_OFF = 0;
        SHOWN_BPM = u32::MAX;
        SHOWN_METER = u32::MAX;
        SHOWN_HINT = false;
        LAST_BEAT_KEY = u64::MAX;
        SHOWN_BEATS = u32::MAX;
        SHOWN_LIT = usize::MAX;

        // 停止中のマップは at_tick=0 の 1 エントリ(再生のたびに clear して書き直す)
        hostapi_tempomap_clear();
        hostapi_tempomap_set_meter(0, NUM as i32, DEN as i32);
        hostapi_tempomap_set_tempo(0, upq_of(BPM));
    }
    draw_status();
    draw_beats(usize::MAX);
    draw_run_button();
    0
}

#[no_mangle]
pub extern "C" fn app_tick() {
    const EV_MAX: usize = 16;
    let mut evs = [Event { ev_type: 0, param: 0, x: 0, y: 0, time_ms: 0 }; EV_MAX];
    let n = unsafe {
        hostapi_poll_event(evs.as_mut_ptr() as *mut u8,
                           (EV_MAX * core::mem::size_of::<Event>()) as u32)
    };
    let now = unsafe { hostapi_now_ms() };
    for ev in &evs[..n.max(0) as usize] {
        // 拍子は「離した位置」で決める。実機は前に届けた位置から 8px 動いたときだけ MOVE を届けるので、
        // 最後の数 px は UP にしか入らないことがある(UP には最終座標が入る。shared/hostapi_defs.h)。
        // Gesture は UP の座標を使わないので、ここで拾う(Phase 22b ステップ 5 で実機の 32px のドラッグが 1 段足りなかった)
        unsafe {
            let g = &*addr_of!(GESTURE);
            if ev.ev_type == appui::EV_TOUCH_UP && EDITING && TARGET == Target::Meter
                && (g.armed() || g.shuttling())
            {
                if let Some((px, py)) = g.press_pos() {
                    drag_meter(ev.x as i32 - px, ev.y as i32 - py);
                }
            }
        }
        let a = unsafe {
            (*addr_of_mut!(GESTURE)).on_event(ev.ev_type, ev.x as i32, ev.y as i32, ev.time_ms)
        };
        on_action(a, now);
    }
    let a = unsafe { (*addr_of_mut!(GESTURE)).tick(now) };
    on_action(a, now);
    // 値と点滅を描く(変わっていなければ何もしない)
    draw_status();

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
        let key = (p.bar as u64) * (NUM as u64) + p.beat as u64;
        if key != LAST_BEAT_KEY {
            LAST_BEAT_KEY = key;
            draw_beats(p.beat as usize);
        }
    }
}
