// USB Serial/JTAG 上のコマンドコンソール(Phase 12 作業 3)。
//
// 目的は実機回帰の自動化で、ランチャーのタッチ・電源キーという人手前提の経路を
// 置き換えるものではない(既存の操作系はそのまま残る)。
//
// 経路について: このボードの /dev/ttyACM0 は ESP32-S3 内蔵の USB Serial/JTAG で、
// ESP-IDF のコンソール設定では primary=UART0 / secondary=USB Serial/JTAG になって
// いる。secondary console は出力専用なので stdin には何も届かない。そこで
// コンソール設定には触れず、USJ ドライバを直接入れて read する。実測(Phase 12)で
//   - ドライバ導入後もログ出力は影響を受けない
//   - `idf.py monitor` 経由でホストから送った文字がそのまま届く(行末は CR)
//   - 38 文字 x 5 行を待ちなしで送ってもバイト欠落なし
// を確認済み。
//
// 応答は printf ではなく ESP_LOG で出す。printf(stdout)は primary console
// = UART0 に出てしまい、USB 側には現れないため。
//
// タスク優先度は 2(audio_player の 3 より低い。MP3 再生と共存する常駐タスクの
// 教訓 P10-1)。スタックは静的確保(恒久物をヒープから取ると最大連続ブロックを
// 分断する。教訓 6B/7B-fix)。ロックは LVGL のもの(launcher_show と同じ流儀)
// だけで、L0 ディスパッチャの portMUX とは共有しない。

#include "serial_cmd.hpp"

#include "sdkconfig.h"

#if CONFIG_KYBOTOS_SERIAL_CMD

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "hostapi.hpp"
#include "launcher.hpp"
#include "touch.hpp"
#include "wasm_runtime.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <unistd.h>

namespace {

// 応答行のタグ。回帰スクリプトはこのタグで行を拾う。
const char* TAG = "KBCMD";

constexpr int kStackWords = 3072;
constexpr size_t kLineMax = 96;
constexpr size_t kRxChunk = 64;

StaticTask_t s_tcb;
StackType_t s_stack[kStackWords];
char s_line[kLineMax];
size_t s_line_len = 0;

void cmd_heap()
{
    // Phase 15: internal だけでなく PSRAM も出す。linear memory は PSRAM から
    // 取られるので、internal だけ見ていると PSRAM のリークに気づけない。
    constexpr uint32_t kInt = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG, "heap free %u largest %u min %u",
             (unsigned)heap_caps_get_free_size(kInt),
             (unsigned)heap_caps_get_largest_free_block(kInt),
             (unsigned)heap_caps_get_minimum_free_size(kInt));
    ESP_LOGI(TAG, "heap free_int=%u largest_int=%u min_int=%u"
                  " free_psram=%u largest_psram=%u min_psram=%u",
             (unsigned)heap_caps_get_free_size(kInt),
             (unsigned)heap_caps_get_largest_free_block(kInt),
             (unsigned)heap_caps_get_minimum_free_size(kInt),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

void cmd_ls()
{
    DIR* dir = opendir(wasmrt::kAppsDir);
    if (!dir) {
        ESP_LOGI(TAG, "ls err apps dir not found");
        return;
    }
    int count = 0;
    while (dirent* ent = readdir(dir)) {
        const size_t len = strlen(ent->d_name);
        if (len < 6 || strcasecmp(ent->d_name + len - 5, ".wasm") != 0) continue;
        ESP_LOGI(TAG, "app %s", ent->d_name);
        count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "ls done %d", count);
}

void cmd_run(const char* name)
{
    const char* err = "unknown";
    char path[96];
    if (wasmrt::launcher_launch_by_name(name, &err, path, sizeof(path))) {
        ESP_LOGI(TAG, "run ok %s", path);
    } else {
        ESP_LOGI(TAG, "run err %s", err);
    }
}

void cmd_stop()
{
    if (!wasmrt::app_is_running()) {
        ESP_LOGI(TAG, "stop idle");
        return;
    }
    wasmrt::app_request_stop();
    ESP_LOGI(TAG, "stop ok");
}

// ---- Phase 22: UI の注入と画面の文字 ------------------------------------------
//
// 回帰のシナリオ(scripts/regress-scenario.sh)が使う。座標はアプリの論理座標(320x240)。
// 注入は touch の indev_read_cb の段で行うので、指と同じ経路を通る(touch.hpp)。
// 押している時間はこのタスクが vTaskDelay で刻み、終わってから応答する
// (シナリオは応答を待って次の手順へ進むだけでよい)。

constexpr int kTapMs = 80;        // LVGL の読み取り周期(既定 30ms)の数回ぶん
constexpr int kDragSteps = 8;     // scripts/ui-linux.sh の drag と同じ形
constexpr int kDragStepMs = 40;

// 空白区切りの整数を n 個読む。足りなければ false
bool parse_ints(const char* arg, int* out, int n)
{
    const char* p = arg ? arg : "";
    for (int i = 0; i < n; i++) {
        char* end = nullptr;
        const long v = strtol(p, &end, 10);
        if (end == p) return false;
        out[i] = (int)v;
        p = end;
    }
    return true;
}

void cmd_tap(const char* arg)
{
    int v[2];
    if (!parse_ints(arg, v, 2)) { ESP_LOGI(TAG, "tap err usage: tap X Y"); return; }
    touch_inject::press((int16_t)v[0], (int16_t)v[1]);
    vTaskDelay(pdMS_TO_TICKS(kTapMs));
    touch_inject::release();
    vTaskDelay(pdMS_TO_TICKS(kTapMs));
    ESP_LOGI(TAG, "tap done %d %d", v[0], v[1]);
}

void cmd_hold(const char* arg)
{
    int v[3];
    if (!parse_ints(arg, v, 3) || v[2] < 0) { ESP_LOGI(TAG, "hold err usage: hold X Y MS"); return; }
    touch_inject::press((int16_t)v[0], (int16_t)v[1]);
    vTaskDelay(pdMS_TO_TICKS(v[2] > kTapMs ? v[2] : kTapMs));
    touch_inject::release();
    vTaskDelay(pdMS_TO_TICKS(kTapMs));
    ESP_LOGI(TAG, "hold done %d %d %d", v[0], v[1], v[2]);
}

void cmd_drag(const char* arg)
{
    int v[5];
    if (!parse_ints(arg, v, 5) || v[4] < 0) {
        ESP_LOGI(TAG, "drag err usage: drag X Y DX DY MS");
        return;
    }
    touch_inject::press((int16_t)v[0], (int16_t)v[1]);
    vTaskDelay(pdMS_TO_TICKS(v[4] > kTapMs ? v[4] : kTapMs));
    for (int k = 1; k <= kDragSteps; k++) {
        touch_inject::press((int16_t)(v[0] + v[2] * k / kDragSteps),
                            (int16_t)(v[1] + v[3] * k / kDragSteps));
        vTaskDelay(pdMS_TO_TICKS(kDragStepMs));
    }
    touch_inject::release();
    vTaskDelay(pdMS_TO_TICKS(kTapMs));
    ESP_LOGI(TAG, "drag done %d %d %d %d %d", v[0], v[1], v[2], v[3], v[4]);
}

// 電源キーと同じ入口(短押し = 戻る、長押し = 強制ホーム)
void cmd_key(const char* arg)
{
    const bool back = arg && strcmp(arg, "back") == 0;
    const bool home = arg && strcmp(arg, "home") == 0;
    if (!back && !home) { ESP_LOGI(TAG, "key err usage: key back|home"); return; }
    if (!wasmrt::app_is_running()) { ESP_LOGI(TAG, "key idle"); return; }
    if (back) wasmrt::app_request_key_back();
    else      wasmrt::app_request_force_home();
    ESP_LOGI(TAG, "key ok %s", arg);
}

// 1 スロット = 1 行。非 ASCII と '\' は \xNN にしてログを ASCII に保つ
void emit_text(int32_t x, int32_t y, uint32_t rgb, const char* text)
{
    char esc[4 * 64 + 1];
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p && o + 4 < sizeof(esc); p++) {
        if (*p >= 0x20 && *p < 0x7f && *p != '\\') {
            esc[o++] = (char)*p;
        } else {
            o += snprintf(esc + o, sizeof(esc) - o, "\\x%02x", *p);
        }
    }
    esc[o] = 0;
    ESP_LOGI(TAG, "text %d %d %06x %s", (int)x, (int)y, (unsigned)(rgb & 0xffffff), esc);
}

void cmd_texts()
{
    const int n = wasmrt::hostapi_dump_texts(emit_text);
    if (n < 0) ESP_LOGI(TAG, "texts idle");
    else       ESP_LOGI(TAG, "texts done %d", n);
}

// SD のアプリを消す(埋め込みから外したアプリの .wasm は SD に残るため。Phase 22)
void cmd_rm(const char* name)
{
    if (!name || !*name) { ESP_LOGI(TAG, "rm err usage: rm <app>"); return; }
    if (strchr(name, '/') || strstr(name, "..")) { ESP_LOGI(TAG, "rm err bad name"); return; }
    if (wasmrt::app_is_running()) { ESP_LOGI(TAG, "rm err app is running"); return; }
    char path[96];
    const size_t len = strlen(name);
    const bool ext = len > 5 && strcasecmp(name + len - 5, ".wasm") == 0;
    snprintf(path, sizeof(path), ext ? "%s/%s" : "%s/%s.wasm", wasmrt::kAppsDir, name);
    if (unlink(path) != 0) { ESP_LOGI(TAG, "rm err no such app %s", path); return; }
    wasmrt::launcher_show(nullptr);  // メニューの一覧を読み直す
    ESP_LOGI(TAG, "rm ok %s", path);
}

void dispatch(char* line)
{
    // 前後の空白を落とす
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
    if (n == 0) return;

    // 最初の語がコマンド、残りが引数
    char* arg = strchr(line, ' ');
    if (arg) {
        *arg++ = 0;
        while (*arg == ' ') arg++;
    }

    if (strcmp(line, "ping") == 0)      ESP_LOGI(TAG, "pong");
    else if (strcmp(line, "heap") == 0) cmd_heap();
    else if (strcmp(line, "ls") == 0)   cmd_ls();
    else if (strcmp(line, "stop") == 0) cmd_stop();
    else if (strcmp(line, "run") == 0)  cmd_run(arg ? arg : "");
    else if (strcmp(line, "tap") == 0)  cmd_tap(arg);
    else if (strcmp(line, "hold") == 0) cmd_hold(arg);
    else if (strcmp(line, "drag") == 0) cmd_drag(arg);
    else if (strcmp(line, "key") == 0)  cmd_key(arg);
    else if (strcmp(line, "texts") == 0) cmd_texts();
    else if (strcmp(line, "rm") == 0)   cmd_rm(arg);
    else ESP_LOGI(TAG, "err unknown command '%s'", line);
}

void console_task(void*)
{
    static uint8_t rx[kRxChunk];
    for (;;) {
        const int n = usb_serial_jtag_read_bytes(rx, sizeof(rx), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; i++) {
            const char c = (char)rx[i];
            if (c == '\r' || c == '\n') {
                s_line[s_line_len] = 0;
                dispatch(s_line);
                s_line_len = 0;
            } else if (s_line_len + 1 < sizeof(s_line)) {
                s_line[s_line_len++] = c;
            } else {
                // 行が長すぎる。捨てて次の行末まで読み飛ばす
                s_line_len = 0;
                ESP_LOGI(TAG, "err line too long");
            }
        }
    }
}

} // namespace

namespace serialcmd {

void Init()
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    const esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s", esp_err_to_name(err));
        return;
    }
    xTaskCreateStatic(console_task, "serial_cmd", kStackWords, nullptr, 2, s_stack, &s_tcb);
    ESP_LOGI(TAG, "ready");
}

} // namespace serialcmd

#else // !CONFIG_KYBOTOS_SERIAL_CMD

namespace serialcmd {
void Init() {}
} // namespace serialcmd

#endif
