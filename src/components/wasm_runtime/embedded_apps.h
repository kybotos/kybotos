// ファームウェアに埋め込んだアプリの表。中身(embedded_apps.c)は CMakeLists.txt がビルド時に生成する
// (この repo のサンプルアプリ + KYBOTOS_EXTRA_APPS)。launcher が初回に SD へ配置(seed)する。
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;      // SD に置くファイル名(例 "metronome.wasm")
    const uint8_t* start;
    const uint8_t* end;
} EmbeddedApp;

extern const EmbeddedApp kEmbeddedApps[];
extern const size_t kEmbeddedAppCount;

#ifdef __cplusplus
}
#endif
