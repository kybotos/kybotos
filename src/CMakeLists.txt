
cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME "kybotos")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Phase 24: board selection. -DKYBOTOS_BOARD=<board> picks src/boards/<board>/sdkconfig.defaults
# (applied after the common sdkconfig.defaults). sdkconfig is generated from the defaults only when
# it does not exist yet, so each board needs its own build directory and sdkconfig
# (scripts/fw.sh does that; the default board keeps src/build and src/sdkconfig).
set(KYBOTOS_BOARD "waveshare_lcd28" CACHE STRING "Target board (src/boards/<board>)")
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${KYBOTOS_BOARD}/sdkconfig.defaults")
    message(FATAL_ERROR "Unknown KYBOTOS_BOARD '${KYBOTOS_BOARD}' (no src/boards/${KYBOTOS_BOARD}/sdkconfig.defaults)")
endif()
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${KYBOTOS_BOARD}/sdkconfig.defaults")
idf_build_set_property(COMPILE_DEFINITIONS "LV_COLOR_16_SWAP=1" APPEND)
project(${PROJECT_NAME})

# The board in sdkconfig must be the one asked for (an sdkconfig made for another board keeps its
# choice, because the defaults are not applied again).
if(NOT CONFIG_KYBOTOS_BOARD_NAME STREQUAL KYBOTOS_BOARD)
    message(FATAL_ERROR "sdkconfig is for board '${CONFIG_KYBOTOS_BOARD_NAME}' but KYBOTOS_BOARD is "
        "'${KYBOTOS_BOARD}'. Use a separate build directory and sdkconfig per board (scripts/fw.sh).")
endif()
message(STATUS "Kybotos board: ${KYBOTOS_BOARD}")

# WAMR 2.4.0 は WAMR_ENABLE_MEMORY_PROFILING=y のとき RECORD_STACK_USAGE が
# -Werror=dangling-pointer に引っかかる(意図的にローカル変数のアドレスで
# スタック使用量を記録する実装)。当該コンポーネントのみ警告を抑制する。
idf_component_get_property(wamr_lib espressif__wasm-micro-runtime COMPONENT_LIB)
if(wamr_lib)
    target_compile_options(${wamr_lib} PRIVATE -Wno-dangling-pointer)
endif()
