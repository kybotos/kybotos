//! タップ / 長押し / スワイプの判定(`docs/design/ui-conventions.md`)。
//!
//! 入力は `hostapi_poll_event` のイベント(DOWN / MOVE / UP)と `hostapi_now_ms`。
//! `app_tick` は 100ms 周期なので、長押しの成立はイベントではなく [`Gesture::tick`]
//! で検出する(指を止めたままなら MOVE は来ないため)。
//!
//! **意味づけはアプリの仕事**。ここは「何が起きたか」だけを返す。たとえば
//! 長押しに意味が無い場面では、アプリが [`Action::LongPressFired`] をタップと
//! 同じに扱い、点滅も出さなければよい。

/// `shared/hostapi_defs.h` の `HOSTAPI_EV_*`(この crate はホストに依存しないので写しを持つ)
pub const EV_TOUCH_DOWN: u16 = 1;
pub const EV_TOUCH_UP: u16 = 2;
pub const EV_TOUCH_MOVE: u16 = 3;

/// タップと認める最大移動量(論理 px)
pub const TAP_MAX_MOVE: i32 = 12;
/// 長押しの成立時間(ms)
pub const LONG_PRESS_MS: u32 = 600;
/// スワイプ(スクロール)と認める最小移動量(論理 px)
pub const SWIPE_MIN_MOVE: i32 = 24;

/// 判定の結果。1 イベント / 1 tick につき 1 つ返る
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Action {
    /// 何も確定していない
    None,
    /// 押された(押下時の状態を控えるための通知。スクロールの基準など)
    Press { x: i32, y: i32 },
    /// タップ(移動が小さいまま離した)
    Tap { x: i32, y: i32 },
    /// 長押しが成立した(**まだ実行しない**。点滅を始める合図)
    LongPressArmed { x: i32, y: i32 },
    /// 長押しのまま離した(ここで実行する)
    LongPressFired { x: i32, y: i32 },
    /// 縦スワイプ中。`dy` は**押下位置からの累積移動量**(下が正)
    Scroll { dy: i32 },
    /// 長押しが成立したあとのドラッグ。`dx` / `dy` は**押下位置からの累積移動量**
    /// (右・下が正)。指を止めていても毎 tick 返るので、**変位を「速さ」としても
    /// 「位置」としても**扱える(速さ = BPM / 位置 = 拍子。Phase 18b → 18d で 2 軸に)
    Shuttle { dx: i32, dy: i32 },
    /// シャトルを離した(値の変更を確定する)
    ShuttleEnd,
    /// **横スワイプ**(アプリが [`Gesture::allow_hswipe`] を立てている間だけ起きる。Phase 21a)。
    /// **ページ送り**に使う: `dx` は押下位置からの累積移動量(右が正)だが、
    /// **1 回の押下につき 1 度しか返らない**ので、アプリは向きだけ見ればよい。
    /// 立てていない画面では従来どおり [`Action::Cancel`] になり、**何も起きない**
    HSwipe { dx: i32 },
    /// **ドラッグ中**(アプリが [`Gesture::allow_drag`] を立てている間だけ起きる。Phase 19b)。
    /// `dx` / `dy` は**押下位置からの累積移動量**(右・下が正)。
    /// スワイプ(スクロール)と違って**横方向でも取り消されない**ので、要素を掴んで運べる
    Drag { dx: i32, dy: i32 },
    /// ドラッグを離した(移動を確定する)
    DragEnd,
    /// 長押し / タップが取り消された(スワイプに移行、または横方向へ動いた)
    Cancel,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum State {
    Idle,
    /// 押下中。まだタップにも長押しにもスワイプにもなっていない
    Pressed,
    /// 長押しが成立した(離せば実行)
    Armed,
    /// 縦スワイプ中
    Scrolling,
    /// 長押しの後のドラッグ中(シャトル)
    Shuttling,
    /// 要素を掴んで運んでいる(`allow_drag` のとき。Phase 19b)
    Dragging,
    /// この押下ではもう何も起こさない(横スワイプ)
    Dead,
}

/// 押下 1 回ぶんの状態機械。アプリが 1 つ持てばよい(シングルタッチ)
#[derive(Clone, Copy)]
pub struct Gesture {
    state: State,
    x: i32,
    y: i32,
    t: u32,
    /// まだタップ / 長押しになりうるか(少しでも大きく動いたら false)
    tap_ok: bool,
    /// シャトル中の累積移動量(右・下が正)。指が止まっていても tick で返すため保持する
    dx: i32,
    dy: i32,
    /// 押下からのドラッグを許すか(編集モードなど。Phase 19b)
    drag_ok: bool,
    /// 横スワイプ(ページ送り)を許すか(Phase 21a)
    hswipe_ok: bool,
}

impl Default for Gesture {
    fn default() -> Self {
        Self::new()
    }
}

/// `u32` の時刻は wrap しうるので、差を符号付きで見る
fn elapsed(now: u32, since: u32) -> i32 {
    now.wrapping_sub(since) as i32
}

impl Gesture {
    pub const fn new() -> Gesture {
        Gesture {
            state: State::Idle,
            x: 0,
            y: 0,
            t: 0,
            tap_ok: true,
            dx: 0,
            dy: 0,
            drag_ok: false,
            hswipe_ok: false,
        }
    }

    /// 長押しが成立していて、まだ離していないか(点滅させるかの判断に使う)
    pub fn armed(&self) -> bool {
        matches!(self.state, State::Armed)
    }

    /// 押下中の座標(押した位置)。離していれば `None`
    pub fn press_pos(&self) -> Option<(i32, i32)> {
        match self.state {
            State::Idle => None,
            _ => Some((self.x, self.y)),
        }
    }

    /// **押下からのドラッグを許すか**(Phase 19b)。アプリが編集モードの間だけ立てる。
    /// **false のときの判定は一切変わらない**(既存アプリは影響を受けない)
    pub fn allow_drag(&mut self, allow: bool) {
        self.drag_ok = allow;
    }

    /// **横スワイプ(ページ送り)を許すか**(Phase 21a)。アプリが必要な画面でだけ立てる。
    /// **false のときの判定は一切変わらない**(既存アプリは影響を受けない)。
    /// 縦が優先という既存規則も変わらない(`dy` のほうが大きければ縦スワイプになる)
    pub fn allow_hswipe(&mut self, allow: bool) {
        self.hswipe_ok = allow;
    }

    /// 要素を掴んで運んでいる最中か
    pub fn dragging(&self) -> bool {
        matches!(self.state, State::Dragging)
    }

    /// 縦スワイプ中か
    pub fn scrolling(&self) -> bool {
        matches!(self.state, State::Scrolling)
    }

    /// シャトル中か(長押しの点滅を続けるかの判断に使う)
    pub fn shuttling(&self) -> bool {
        matches!(self.state, State::Shuttling)
    }

    /// 入力イベントを 1 件食わせる
    pub fn on_event(&mut self, ev_type: u16, x: i32, y: i32, time_ms: u32) -> Action {
        match ev_type {
            EV_TOUCH_DOWN => {
                self.state = State::Pressed;
                self.x = x;
                self.y = y;
                self.t = time_ms;
                self.tap_ok = true;
                self.dx = 0;
                self.dy = 0;
                Action::Press { x, y }
            }
            EV_TOUCH_MOVE => self.on_move(x, y),
            EV_TOUCH_UP => self.on_up(),
            _ => Action::None, // 未知の type は無視する(ABI の契約)
        }
    }

    fn on_move(&mut self, x: i32, y: i32) -> Action {
        let dx = x - self.x;
        let dy = y - self.y;
        match self.state {
            State::Idle | State::Dead => Action::None,
            State::Scrolling => Action::Scroll { dy },
            State::Dragging => {
                self.dx = dx;
                self.dy = dy;
                Action::Drag { dx, dy }
            }
            State::Shuttling => {
                self.dx = dx;
                self.dy = dy;
                Action::Shuttle { dx, dy }
            }
            // 長押しが成立してから動かしたらシャトル(値の連続変更)に移る。
            // 小さな揺れでは移らないので、長押しのまま離す操作は壊れない
            State::Armed if dx.abs() > TAP_MAX_MOVE || dy.abs() > TAP_MAX_MOVE => {
                self.state = State::Shuttling;
                self.dx = dx;
                self.dy = dy;
                Action::Shuttle { dx, dy }
            }
            // ドラッグが許されているなら、縦横どちらでも掴んで運ぶ方に倒す(Phase 19b)
            State::Pressed | State::Armed
                if self.drag_ok
                    && (dx.abs() >= SWIPE_MIN_MOVE || dy.abs() >= SWIPE_MIN_MOVE) =>
            {
                self.state = State::Dragging;
                self.tap_ok = false;
                self.dx = dx;
                self.dy = dy;
                Action::Drag { dx, dy }
            }
            State::Pressed | State::Armed => {
                if dy.abs() >= SWIPE_MIN_MOVE && dy.abs() > dx.abs() {
                    // 縦スワイプへ移行(長押しが成立していたら取り消される)
                    self.state = State::Scrolling;
                    self.tap_ok = false;
                    Action::Scroll { dy }
                } else if dx.abs() >= SWIPE_MIN_MOVE {
                    // 横スワイプ。**許している画面だけ**ページ送りとして 1 度だけ返す
                    // (Phase 21a)。許していなければ従来どおり、タップ / 長押しを
                    // 取り消して何も起こさない
                    self.state = State::Dead;
                    self.tap_ok = false;
                    if self.hswipe_ok {
                        Action::HSwipe { dx }
                    } else {
                        Action::Cancel
                    }
                } else if dx.abs() > TAP_MAX_MOVE || dy.abs() > TAP_MAX_MOVE {
                    // デッドゾーン(タップには大きすぎ、スワイプには足りない)。
                    // **ここで押下を終わらせない**。MOVE は刻んで届くので、
                    // 終わらせるとスワイプ閾値に届く前に打ち切られる(Phase 18a で踏んだ)
                    let announce = self.tap_ok || self.state == State::Armed;
                    self.state = State::Pressed;
                    self.tap_ok = false;
                    if announce {
                        Action::Cancel
                    } else {
                        Action::None
                    }
                } else {
                    Action::None
                }
            }
        }
    }

    fn on_up(&mut self) -> Action {
        let (x, y) = (self.x, self.y);
        let state = self.state;
        self.state = State::Idle;
        match state {
            State::Armed => Action::LongPressFired { x, y },
            State::Shuttling => Action::ShuttleEnd,
            State::Dragging => Action::DragEnd,
            State::Pressed if self.tap_ok => Action::Tap { x, y },
            _ => Action::None,
        }
    }

    /// 毎 `app_tick` に呼ぶ。長押しの成立と、シャトルの継続はここで返す
    pub fn tick(&mut self, now_ms: u32) -> Action {
        if self.state == State::Shuttling {
            // 指が止まっていても変位を返し続ける(変位 = 速さ として使えるように)
            return Action::Shuttle { dx: self.dx, dy: self.dy };
        }
        if self.state == State::Pressed
            && self.tap_ok
            && elapsed(now_ms, self.t) >= LONG_PRESS_MS as i32
        {
            self.state = State::Armed;
            return Action::LongPressArmed { x: self.x, y: self.y };
        }
        Action::None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn down(g: &mut Gesture, x: i32, y: i32, t: u32) -> Action {
        g.on_event(EV_TOUCH_DOWN, x, y, t)
    }
    fn mv(g: &mut Gesture, x: i32, y: i32, t: u32) -> Action {
        g.on_event(EV_TOUCH_MOVE, x, y, t)
    }
    fn up(g: &mut Gesture, x: i32, y: i32, t: u32) -> Action {
        g.on_event(EV_TOUCH_UP, x, y, t)
    }

    #[test]
    fn tap_is_reported_on_release() {
        let mut g = Gesture::new();
        assert_eq!(down(&mut g, 10, 20, 100), Action::Press { x: 10, y: 20 });
        assert_eq!(g.tick(150), Action::None);
        assert_eq!(up(&mut g, 11, 21, 200), Action::Tap { x: 10, y: 20 });
        assert!(g.press_pos().is_none());
    }

    #[test]
    fn small_move_still_taps() {
        let mut g = Gesture::new();
        down(&mut g, 10, 20, 0);
        assert_eq!(mv(&mut g, 18, 28, 50), Action::None); // 8px は許容内
        assert_eq!(up(&mut g, 18, 28, 100), Action::Tap { x: 10, y: 20 });
    }

    #[test]
    fn long_press_arms_then_fires_on_release() {
        let mut g = Gesture::new();
        down(&mut g, 10, 20, 1000);
        assert_eq!(g.tick(1000 + LONG_PRESS_MS - 1), Action::None);
        assert_eq!(g.tick(1000 + LONG_PRESS_MS), Action::LongPressArmed { x: 10, y: 20 });
        assert!(g.armed());
        assert_eq!(g.tick(1000 + LONG_PRESS_MS + 100), Action::None); // 成立は 1 回だけ
        assert_eq!(up(&mut g, 10, 20, 2000), Action::LongPressFired { x: 10, y: 20 });
        assert!(!g.armed());
    }

    /// Phase 18b で意味が変わった: 長押しが成立した後のドラッグは**スクロールではなくシャトル**。
    /// (成立した時点でジェスチャはその要素のものになる。18a では取り消してスクロールにしていた)
    #[test]
    fn drag_after_long_press_is_a_shuttle_not_a_scroll() {
        let mut g = Gesture::new();
        down(&mut g, 10, 100, 0);
        assert_eq!(g.tick(LONG_PRESS_MS), Action::LongPressArmed { x: 10, y: 100 });
        assert_eq!(
            mv(&mut g, 10, 100 + SWIPE_MIN_MOVE, 700),
            Action::Shuttle { dx: 0, dy: SWIPE_MIN_MOVE }
        );
        assert!(!g.armed());
        assert!(g.shuttling());
        assert!(!g.scrolling());
        assert_eq!(up(&mut g, 10, 124, 800), Action::ShuttleEnd);
    }

    #[test]
    fn long_press_does_not_arm_after_dead_zone_move() {
        let mut g = Gesture::new();
        down(&mut g, 10, 100, 0);
        assert_eq!(mv(&mut g, 10, 100 + TAP_MAX_MOVE + 1, 100), Action::Cancel);
        assert_eq!(mv(&mut g, 10, 100 + TAP_MAX_MOVE + 2, 150), Action::None); // 通知は 1 回だけ
        assert_eq!(g.tick(LONG_PRESS_MS), Action::None); // もう成立しない
        assert_eq!(up(&mut g, 10, 113, 700), Action::None); // タップにもならない
    }

    /// MOVE は刻んで届く。デッドゾーンを通過してからスワイプ閾値に達する経路
    /// (実際の指・xdotool の連続 mousemove はこの形になる)
    #[test]
    fn incremental_moves_still_become_a_swipe() {
        let mut g = Gesture::new();
        down(&mut g, 150, 180, 0);
        assert_eq!(mv(&mut g, 150, 160, 100), Action::Cancel); // 20px = デッドゾーン
        assert_eq!(mv(&mut g, 150, 140, 200), Action::Scroll { dy: -40 });
        assert!(g.scrolling());
        assert_eq!(mv(&mut g, 150, 60, 300), Action::Scroll { dy: -120 });
        assert_eq!(up(&mut g, 150, 60, 400), Action::None);
    }

    #[test]
    fn vertical_swipe_reports_cumulative_dy() {
        let mut g = Gesture::new();
        down(&mut g, 100, 200, 0);
        assert_eq!(mv(&mut g, 100, 200 - SWIPE_MIN_MOVE, 50), Action::Scroll { dy: -SWIPE_MIN_MOVE });
        assert_eq!(mv(&mut g, 100, 150, 100), Action::Scroll { dy: -50 });
        assert_eq!(mv(&mut g, 104, 120, 150), Action::Scroll { dy: -80 }); // 横のぶれは無視
        assert_eq!(up(&mut g, 104, 120, 200), Action::None);
    }

    #[test]
    fn horizontal_swipe_is_ignored_but_cancels() {
        let mut g = Gesture::new();
        down(&mut g, 100, 200, 0);
        assert_eq!(mv(&mut g, 100 + SWIPE_MIN_MOVE, 202, 50), Action::Cancel);
        assert_eq!(mv(&mut g, 100 + SWIPE_MIN_MOVE * 2, 202, 100), Action::None);
        assert_eq!(up(&mut g, 148, 202, 150), Action::None);
    }

    #[test]
    fn diagonal_prefers_the_dominant_axis() {
        let mut g = Gesture::new();
        down(&mut g, 100, 200, 0);
        // 縦のほうが大きいので縦スクロール
        assert_eq!(mv(&mut g, 110, 240, 50), Action::Scroll { dy: 40 });
        assert!(g.scrolling());
    }

    #[test]
    fn long_press_then_drag_becomes_a_shuttle() {
        let mut g = Gesture::new();
        down(&mut g, 260, 13, 0);
        assert_eq!(g.tick(LONG_PRESS_MS), Action::LongPressArmed { x: 260, y: 13 });
        // 小さな揺れではシャトルに移らない(長押しのまま)
        assert_eq!(mv(&mut g, 266, 13, 650), Action::None);
        assert!(g.armed());
        // 大きく動かすとシャトルへ
        assert_eq!(mv(&mut g, 260 + 30, 15, 700), Action::Shuttle { dx: 30, dy: 2 });
        assert!(g.shuttling());
        assert!(!g.armed());
        // 指を止めていても tick が変位を返し続ける(変位 = 速さ)
        assert_eq!(g.tick(800), Action::Shuttle { dx: 30, dy: 2 });
        assert_eq!(mv(&mut g, 260 - 50, 13, 900), Action::Shuttle { dx: -50, dy: 0 });
        assert_eq!(g.tick(1000), Action::Shuttle { dx: -50, dy: 0 });
        assert_eq!(up(&mut g, 210, 13, 1100), Action::ShuttleEnd);
        assert!(!g.shuttling());
        assert_eq!(g.tick(1200), Action::None);
    }

    /// Phase 18d: 上下も使う(拍子の分母)。両軸が独立に届く
    #[test]
    fn a_shuttle_reports_both_axes() {
        let mut g = Gesture::new();
        down(&mut g, 150, 100, 0);
        assert_eq!(g.tick(LONG_PRESS_MS), Action::LongPressArmed { x: 150, y: 100 });
        assert_eq!(mv(&mut g, 150 + 32, 100 - 48, 700), Action::Shuttle { dx: 32, dy: -48 });
        assert_eq!(g.tick(800), Action::Shuttle { dx: 32, dy: -48 });
        assert_eq!(mv(&mut g, 150 + 32, 100 + 16, 900), Action::Shuttle { dx: 32, dy: 16 });
        assert_eq!(up(&mut g, 182, 116, 1000), Action::ShuttleEnd);
    }

    #[test]
    fn a_swipe_without_long_press_is_not_a_shuttle() {
        let mut g = Gesture::new();
        down(&mut g, 150, 100, 0);
        assert_eq!(mv(&mut g, 150 + 40, 100, 100), Action::Cancel); // 横スワイプは無視のまま
        assert_eq!(g.tick(LONG_PRESS_MS), Action::None);
        assert_eq!(up(&mut g, 190, 100, 700), Action::None);
    }

    #[test]
    fn orphan_move_and_up_are_ignored() {
        let mut g = Gesture::new();
        assert_eq!(mv(&mut g, 10, 10, 0), Action::None);
        assert_eq!(up(&mut g, 10, 10, 10), Action::None);
        assert_eq!(g.tick(10_000), Action::None);
    }

    #[test]
    fn unknown_event_type_is_ignored() {
        let mut g = Gesture::new();
        down(&mut g, 10, 20, 0);
        assert_eq!(g.on_event(999, 0, 0, 10), Action::None);
        assert_eq!(up(&mut g, 10, 20, 50), Action::Tap { x: 10, y: 20 });
    }

    #[test]
    fn time_wraparound_is_handled() {
        let mut g = Gesture::new();
        let t0 = u32::MAX - 100;
        down(&mut g, 10, 20, t0);
        assert_eq!(g.tick(t0.wrapping_add(LONG_PRESS_MS - 1)), Action::None);
        assert_eq!(
            g.tick(t0.wrapping_add(LONG_PRESS_MS)),
            Action::LongPressArmed { x: 10, y: 20 }
        );
    }

    // ---- Phase 19b: 掴んで運ぶドラッグ ----

    #[test]
    fn drag_is_off_by_default_and_behaves_as_before() {
        let mut g = Gesture::new();
        g.on_event(EV_TOUCH_DOWN, 100, 100, 0);
        // 横へ大きく動かしても、従来どおり取り消されるだけ(横スワイプは v1 未使用)
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 140, 100, 50), Action::Cancel);
        assert_eq!(g.on_event(EV_TOUCH_UP, 140, 100, 60), Action::None);
    }

    #[test]
    fn drag_reports_both_axes_and_ends_on_release() {
        let mut g = Gesture::new();
        g.allow_drag(true);
        g.on_event(EV_TOUCH_DOWN, 100, 100, 0);
        // 閾値までは何も起きない
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 108, 100, 20), Action::None);
        // 横へ超えたらドラッグ(取り消されない)
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 130, 106, 40), Action::Drag { dx: 30, dy: 6 });
        assert!(g.dragging());
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 90, 150, 60), Action::Drag { dx: -10, dy: 50 });
        assert_eq!(g.on_event(EV_TOUCH_UP, 90, 150, 80), Action::DragEnd);
        assert!(!g.dragging());
    }

    #[test]
    fn drag_takes_precedence_over_scroll_while_allowed() {
        let mut g = Gesture::new();
        g.allow_drag(true);
        g.on_event(EV_TOUCH_DOWN, 100, 100, 0);
        // 縦でもスクロールではなくドラッグになる
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 100, 130, 40), Action::Drag { dx: 0, dy: 30 });
        assert!(!g.scrolling());
    }

    #[test]
    fn drag_can_be_turned_off_again() {
        let mut g = Gesture::new();
        g.allow_drag(true);
        g.allow_drag(false);
        g.on_event(EV_TOUCH_DOWN, 100, 100, 0);
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 100, 130, 40), Action::Scroll { dy: 30 });
    }

    #[test]
    fn a_tap_still_works_while_drag_is_allowed() {
        let mut g = Gesture::new();
        g.allow_drag(true);
        g.on_event(EV_TOUCH_DOWN, 50, 60, 0);
        assert_eq!(g.on_event(EV_TOUCH_UP, 50, 60, 100), Action::Tap { x: 50, y: 60 });
    }

    // ---- 横スワイプ(Phase 21a)----

    #[test]
    fn horizontal_swipe_is_ignored_by_default() {
        // **既存アプリの挙動が変わらないこと**が本 crate の約束
        let mut g = Gesture::new();
        g.on_event(EV_TOUCH_DOWN, 100, 100, 0);
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 140, 100, 50), Action::Cancel);
        assert_eq!(g.on_event(EV_TOUCH_UP, 140, 100, 100), Action::None);
    }

    #[test]
    fn horizontal_swipe_fires_once_when_allowed() {
        let mut g = Gesture::new();
        g.allow_hswipe(true);
        g.on_event(EV_TOUCH_DOWN, 100, 100, 0);
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 140, 100, 50), Action::HSwipe { dx: 40 });
        // 押下 1 回につき 1 度だけ(そのあとは何も起きない = ページが飛び続けない)
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 180, 100, 60), Action::None);
        assert_eq!(g.on_event(EV_TOUCH_UP, 180, 100, 100), Action::None);
    }

    #[test]
    fn horizontal_swipe_reports_its_direction() {
        let mut g = Gesture::new();
        g.allow_hswipe(true);
        g.on_event(EV_TOUCH_DOWN, 200, 100, 0);
        match g.on_event(EV_TOUCH_MOVE, 160, 100, 50) {
            Action::HSwipe { dx } => assert!(dx < 0),
            a => panic!("expected HSwipe, got {a:?}"),
        }
    }

    #[test]
    fn vertical_still_wins_over_horizontal() {
        // 縦優先という既存規則は変わらない
        let mut g = Gesture::new();
        g.allow_hswipe(true);
        g.on_event(EV_TOUCH_DOWN, 100, 100, 0);
        assert_eq!(g.on_event(EV_TOUCH_MOVE, 130, 140, 50), Action::Scroll { dy: 40 });
    }

    #[test]
    fn horizontal_swipe_does_not_break_tap_or_long_press() {
        let mut g = Gesture::new();
        g.allow_hswipe(true);
        g.on_event(EV_TOUCH_DOWN, 50, 60, 0);
        assert_eq!(g.on_event(EV_TOUCH_UP, 50, 60, 100), Action::Tap { x: 50, y: 60 });
        g.on_event(EV_TOUCH_DOWN, 50, 60, 200);
        assert_eq!(g.tick(200 + LONG_PRESS_MS), Action::LongPressArmed { x: 50, y: 60 });
        assert_eq!(g.on_event(EV_TOUCH_UP, 50, 60, 900), Action::LongPressFired { x: 50, y: 60 });
    }

}
