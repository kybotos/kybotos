# Linux ホスト(ランチャー付き)

実機と同一の `.wasm` を同一のホスト API(`shared/hostapi_defs.h`)で動かす
検証用ホスト。ランタイムは実機と同じ WAMR 2.4.0(fast interpreter、
libc builtin のみ)を FetchContent で取得してビルドする。

依存: cmake (>=3.16), gcc, libsdl2-dev
任意: libsdl2-ttf-dev(あればシステムフォントでアンチエイリアス描画。
無ければ font8x8 ビットマップにフォールバック。`KYBOTOS_FONT` 環境変数で
フォントファイルを指定可能)

```
cd hosts/linux
cmake -B build
cmake --build build -j

# ランチャーモード(既定: ../../wasm-apps をスキャン。サブディレクトリ 1 段も検索)
./build/kybotos_host
./build/kybotos_host <appsディレクトリ>
./build/kybotos_host ../../wasm-apps/dev     # 検査用・診断用アプリ(1 段下の dev/ は既定の一覧に出ない)

# 単発実行モード(メニューなし。CI スモーク用)
./build/kybotos_host ../../wasm-apps/metronome/metronome.wasm

# コマンドの入口つき(Phase 22。回帰と scripts/ui-linux.sh が使う。語彙は実機のシリアルコンソールと同じ)
mkfifo /tmp/kb.fifo
KYBOTOS_CMD_FIFO=/tmp/kb.fifo ./build/kybotos_host ../../wasm-apps/metronome/metronome.wasm &
echo "tap 281 202" > /tmp/kb.fifo; echo texts > /tmp/kb.fifo; echo stop > /tmp/kb.fifo   # 応答は stdout の "CMD: ..."
```

- 描画: SDL2 ウィンドウ(実機と同じランドスケープ 320x240 の 2 倍拡大)
  + font8x8(public domain)
- 音: SDL audio に実機と同じ生成 PCM(1kHz 減衰サイン 30ms)
- 操作: メニュー行をクリックで起動 / **ESC でメニューに戻る**
  (実機の power_key 短押し相当)/ メニューで ESC またはウィンドウクローズで終了
- アプリのライフサイクルは実機と同一(load → app_init → 100ms tick →
  任意の app_exit → 破棄。ランタイムは常駐)
