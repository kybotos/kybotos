//! 画面スタック(`docs/design/ui-conventions.md`)。
//!
//! 「HW ボタンで 1 階層戻る。最上位で押したらアプリ終了」を表す最小の器。
//! アプリは画面を `u8` の ID で表し、`app_key` の中で [`ScreenStack::pop`] を呼んで、
//! [`Pop::Exit`] なら 0 を返す(= ホストがアプリを停止する)。

/// スタックの最大深さ(Menu → 一覧 → Session 画面 → Tempo で 4)
pub const MAX_DEPTH: usize = 6;

/// [`ScreenStack::pop`] の結果
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Pop {
    /// 1 階層戻った。値は戻った先の画面 ID
    Popped(u8),
    /// 最上位だったので戻れない = アプリを終了する
    Exit,
}

#[derive(Clone, Copy)]
pub struct ScreenStack {
    items: [u8; MAX_DEPTH],
    len: u8,
}

impl ScreenStack {
    /// 最上位(レベル 0)の画面を指定して作る
    pub const fn new(root: u8) -> ScreenStack {
        let mut items = [0u8; MAX_DEPTH];
        items[0] = root;
        ScreenStack { items, len: 1 }
    }

    /// 1 階層深く進む。満杯なら false(呼び出し側の設計ミス)
    pub fn push(&mut self, screen: u8) -> bool {
        if (self.len as usize) >= MAX_DEPTH {
            return false;
        }
        self.items[self.len as usize] = screen;
        self.len += 1;
        true
    }

    /// 1 階層戻る。最上位なら [`Pop::Exit`]
    pub fn pop(&mut self) -> Pop {
        if self.len <= 1 {
            return Pop::Exit;
        }
        self.len -= 1;
        Pop::Popped(self.items[self.len as usize - 1])
    }

    /// 今いる画面
    pub fn top(&self) -> u8 {
        self.items[self.len as usize - 1]
    }

    /// 1 つ下(親)の画面。最上位なら None(Phase 19。パンくずを親で出し分けるのに使う)
    pub fn below(&self) -> Option<u8> {
        if self.len <= 1 {
            return None;
        }
        Some(self.items[self.len as usize - 2])
    }

    /// 今いる画面を差し替える(同じ階層での遷移)
    pub fn replace_top(&mut self, screen: u8) {
        self.items[self.len as usize - 1] = screen;
    }

    /// 深さ(最上位だけなら 1)
    pub fn depth(&self) -> usize {
        self.len as usize
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn root_only_pops_to_exit() {
        let mut s = ScreenStack::new(0);
        assert_eq!(s.depth(), 1);
        assert_eq!(s.top(), 0);
        assert_eq!(s.pop(), Pop::Exit);
        // Exit の後もスタックは壊れない(アプリはそのまま終了する)
        assert_eq!(s.depth(), 1);
        assert_eq!(s.top(), 0);
    }

    #[test]
    fn push_and_pop_walk_the_hierarchy() {
        let mut s = ScreenStack::new(0);
        assert!(s.push(1));
        assert!(s.push(2));
        assert!(s.push(3));
        assert_eq!(s.depth(), 4);
        assert_eq!(s.top(), 3);
        assert_eq!(s.pop(), Pop::Popped(2));
        assert_eq!(s.pop(), Pop::Popped(1));
        assert_eq!(s.pop(), Pop::Popped(0));
        assert_eq!(s.pop(), Pop::Exit);
    }

    #[test]
    fn below_names_the_parent_screen() {
        let mut s = ScreenStack::new(0);
        assert_eq!(s.below(), None);
        s.push(1);
        assert_eq!(s.below(), Some(0));
        s.push(2);
        assert_eq!(s.below(), Some(1));
        s.pop();
        assert_eq!(s.below(), Some(0));
    }

    #[test]
    fn replace_top_keeps_depth() {
        let mut s = ScreenStack::new(0);
        s.push(1);
        s.replace_top(2);
        assert_eq!(s.depth(), 2);
        assert_eq!(s.top(), 2);
        assert_eq!(s.pop(), Pop::Popped(0));
    }

    #[test]
    fn push_fails_when_full() {
        let mut s = ScreenStack::new(0);
        for i in 1..MAX_DEPTH {
            assert!(s.push(i as u8));
        }
        assert_eq!(s.depth(), MAX_DEPTH);
        assert!(!s.push(99));
        assert_eq!(s.top(), (MAX_DEPTH - 1) as u8);
    }
}
