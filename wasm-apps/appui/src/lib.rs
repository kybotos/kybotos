// Kybotos の共通 UI 部品(Phase 18a)。Host API に依存しない。
//
// - 操作規約: docs/design/ui-conventions.md
// - 記録: docs/results/phase16-21-platform.md §4(Phase 18a)
// - theme: サンプルアプリが共有する画面の骨格・配色・記号の定数(Phase 22c)
//
// 描画も Host API 呼び出しも持たない。入力イベント列から「タップ / 長押し /
// スワイプ」を判定し、画面スタックを管理するだけ(判定はホスト側に置かない。
// ホスト差を作らないため。docs/architecture.md §11-11)。
#![cfg_attr(not(any(test, feature = "std")), no_std)]

pub mod gesture;
pub mod stack;
pub mod theme;

pub use gesture::*;
pub use stack::*;
