# ファームウェアに埋め込み、初回に SD の /sdcard/apps へ配置(seed)するアプリ。
# launcher は seed の表(ビルド時に生成する embedded_apps.c)を回すだけで、アプリ名を知らない。
set(WASM_APPS_DIR "${COMPONENT_DIR}/../../../wasm-apps")
set(KYBOTOS_BUILTIN_APPS
    "${WASM_APPS_DIR}/mp3player/mp3player.wasm"
    "${WASM_APPS_DIR}/metronome/metronome.wasm"
)

# この repo の外にあるアプリを一緒に埋め込む口。`.wasm` の絶対パスを `;` 区切りで渡す:
#   idf.py -B <別のビルドディレクトリ> -DKYBOTOS_EXTRA_APPS=/path/to/foo.wasm build
# キャッシュ変数なのでビルドディレクトリに残る。**src/build を使い回さず、ビルドディレクトリを分けること**
# (分けないと、以後この repo だけのビルドにも外のアプリが入る)。
set(KYBOTOS_EXTRA_APPS "" CACHE STRING "Extra .wasm apps to embed and seed (absolute paths, ;-separated)")

# 検査用・診断用のアプリ(wasm-apps/dev/。Phase 22)。**既定のファームには入れない**(ランチャーに実用アプリだけを出す)。
# 回帰に使うファームは ON でビルドする: idf.py -DKYBOTOS_DEV_APPS=ON build(キャッシュに残る。docs/workflow.md §3.4)
option(KYBOTOS_DEV_APPS "Embed the test and diagnostic apps in wasm-apps/dev" OFF)
set(KYBOTOS_DEV_APP_LIST
    # Host API の検査(回帰に使う。Phase 22 で seq_smoke を吸収した)
    "${WASM_APPS_DIR}/dev/hostapi_check/hostapi_check.wasm"
    # MIDI ループバックの診断(Phase 9b / 14。計測器)
    "${WASM_APPS_DIR}/dev/midi_loopback/midi_loopback.wasm"
    # 内蔵音源ポートの検証用(Phase 21)
    "${WASM_APPS_DIR}/dev/synth_probe/synth_probe.wasm"
)
if(KYBOTOS_DEV_APPS)
    list(APPEND KYBOTOS_BUILTIN_APPS ${KYBOTOS_DEV_APP_LIST})
endif()

set(_apps ${KYBOTOS_BUILTIN_APPS} ${KYBOTOS_EXTRA_APPS})
set(_gen "${CMAKE_CURRENT_BINARY_DIR}/embedded_apps.c")
# 依存関係を集める先行評価(CMAKE_BUILD_EARLY_EXPANSION)では、ファイルの確認も生成もしない
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
set(_seen "")
set(_decls "")
set(_rows "")
foreach(_app IN LISTS _apps)
    if(NOT EXISTS "${_app}")
        message(FATAL_ERROR "wasm app not found: ${_app}")
    endif()
    get_filename_component(_name "${_app}" NAME)
    # EMBED_FILES のシンボル名と中間ファイル(build/<name>.S)はファイル名だけで決まるので、同名は埋め込めない
    if("${_name}" IN_LIST _seen)
        message(FATAL_ERROR "duplicate wasm app name: ${_name} (${_app})")
    endif()
    list(APPEND _seen "${_name}")
    string(MAKE_C_IDENTIFIER "${_name}" _sym)
    string(APPEND _decls "extern const uint8_t _binary_${_sym}_start[];\nextern const uint8_t _binary_${_sym}_end[];\n")
    string(APPEND _rows "    { \"${_name}\", _binary_${_sym}_start, _binary_${_sym}_end },\n")
endforeach()

# 中身が変わったときだけ書き換える(毎回書くと、そのたびに再コンパイルになる)
file(WRITE "${_gen}.tmp"
    "// 生成ファイル(src/components/wasm_runtime/CMakeLists.txt)。編集しない\n"
    "#include \"embedded_apps.h\"\n\n${_decls}\n"
    "const EmbeddedApp kEmbeddedApps[] = {\n${_rows}};\n"
    "const size_t kEmbeddedAppCount = sizeof(kEmbeddedApps) / sizeof(kEmbeddedApps[0]);\n")
configure_file("${_gen}.tmp" "${_gen}" COPYONLY)
endif()

idf_component_register(
    SRCS "wasm_runtime.cpp" "hostapi.cpp" "launcher.cpp" "screensaver.cpp"
         "${_gen}"
         "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/master_ui.c"
         "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/splash_logo.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        ${_apps}
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/test.mp3"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/tune_down.mp3"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/tune_duo.mp3"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared"
    PRIV_REQUIRES esp_timer audio midi seq storage display
)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
