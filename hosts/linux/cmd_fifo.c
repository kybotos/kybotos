/* コマンドの入口(Phase 22)。語彙と応答は cmd_fifo.h、実機側は src/main/serial_cmd.cpp */
#include "cmd_fifo.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL.h>

#include "hostapi_sdl.h"

#define TAP_MS 80        /* 実機と同じ */
#define DRAG_STEPS 8
#define DRAG_STEP_MS 40
#define LINE_MAX 128
#define MAX_STEPS (DRAG_STEPS + 4)

typedef enum { STEP_PRESS, STEP_MOVE, STEP_RELEASE, STEP_DONE } step_kind_t;

typedef struct {
    uint32_t due_ms; /* 実行する時刻(SDL_GetTicks) */
    step_kind_t kind;
    int x, y;
} Step;

static int s_fd = -1;
static char s_line[LINE_MAX];
static size_t s_line_len = 0;
static bool s_line_overflow = false;

/* 進行中の操作。s_step_count > 0 の間は次の行を読まない */
static Step s_steps[MAX_STEPS];
static int s_step_count = 0;
static int s_step_next = 0;
static char s_done_msg[LINE_MAX];

static void reply(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("CMD: ", stdout);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
    fflush(stdout); /* 回帰はログファイルを読むので、溜めると待ちがずれる */
}

void cmd_fifo_open(void)
{
    const char* path = getenv("KYBOTOS_CMD_FIFO");
    if (!path || !*path) return;
    /* 書き手がいなくても開けるように非ブロッキング。書き手が閉じると read は 0 を返すだけ */
    s_fd = open(path, O_RDONLY | O_NONBLOCK);
    if (s_fd < 0) {
        fprintf(stderr, "cmd fifo: cannot open %s: %s\n", path, strerror(errno));
        return;
    }
    /* 入口を使うときは stdout を行バッファにする(`app started` などもすぐログファイルに出る) */
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("cmd fifo: %s\n", path);
}

void cmd_fifo_close(void)
{
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
}

/* 空白区切りの整数を n 個読む。足りなければ false */
static bool parse_ints(const char* arg, int* out, int n)
{
    const char* p = arg ? arg : "";
    for (int i = 0; i < n; i++) {
        char* end = NULL;
        const long v = strtol(p, &end, 10);
        if (end == p) return false;
        out[i] = (int)v;
        p = end;
    }
    return true;
}

static void add_step(uint32_t due, step_kind_t kind, int x, int y)
{
    if (s_step_count < MAX_STEPS) s_steps[s_step_count++] = (Step){due, kind, x, y};
}

static void emit_text(int x, int y, uint32_t rgb888, const char* text)
{
    /* 実機と同じく、非 ASCII と '\' は \xNN にする */
    char esc[4 * 64 + 1];
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p && o + 4 < sizeof(esc); p++) {
        if (*p >= 0x20 && *p < 0x7f && *p != '\\') {
            esc[o++] = (char)*p;
        } else {
            o += (size_t)snprintf(esc + o, sizeof(esc) - o, "\\x%02x", *p);
        }
    }
    esc[o] = '\0';
    reply("text %d %d %06x %s", x, y, (unsigned)(rgb888 & 0xffffff), esc);
}

/* 1 行を解釈する。操作を積んだら s_step_count > 0 になる */
static cmd_action_t dispatch(char* line, bool app_running)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t' || line[n - 1] == '\r')) line[--n] = '\0';
    if (n == 0) return CMD_ACTION_NONE;
    char* arg = strchr(line, ' ');
    if (arg) {
        *arg++ = '\0';
        while (*arg == ' ') arg++;
    }

    const uint32_t now = SDL_GetTicks();
    int v[5];
    if (strcmp(line, "ping") == 0) {
        reply("pong");
    } else if (strcmp(line, "tap") == 0 || strcmp(line, "hold") == 0 || strcmp(line, "drag") == 0) {
        const bool tap = line[0] == 't', hold = line[0] == 'h';
        const int argc = tap ? 2 : hold ? 3 : 5;
        if (!parse_ints(arg, v, argc) || (!tap && v[argc - 1] < 0)) {
            reply("%s err usage", line);
        } else if (!app_running) {
            reply("%s idle", line);
        } else {
            const uint32_t press_ms = tap ? TAP_MS : (uint32_t)(v[argc - 1] > TAP_MS ? v[argc - 1] : TAP_MS);
            uint32_t t = now;
            add_step(t, STEP_PRESS, v[0], v[1]);
            t += press_ms;
            if (!tap && !hold) {
                for (int k = 1; k <= DRAG_STEPS; k++) {
                    add_step(t, STEP_MOVE, v[0] + v[2] * k / DRAG_STEPS, v[1] + v[3] * k / DRAG_STEPS);
                    t += DRAG_STEP_MS;
                }
            }
            const int ex = tap || hold ? v[0] : v[0] + v[2];
            const int ey = tap || hold ? v[1] : v[1] + v[3];
            add_step(t, STEP_RELEASE, ex, ey);
            add_step(t + TAP_MS, STEP_DONE, 0, 0);
            if (tap)       snprintf(s_done_msg, sizeof(s_done_msg), "tap done %d %d", v[0], v[1]);
            else if (hold) snprintf(s_done_msg, sizeof(s_done_msg), "hold done %d %d %d", v[0], v[1], v[2]);
            else snprintf(s_done_msg, sizeof(s_done_msg), "drag done %d %d %d %d %d", v[0], v[1], v[2], v[3], v[4]);
        }
    } else if (strcmp(line, "key") == 0) {
        if (!arg || strcmp(arg, "back") != 0) {
            reply("key err usage: key back");
        } else if (!app_running) {
            reply("key idle");
        } else {
            reply("key ok back");
            return CMD_ACTION_KEY_BACK;
        }
    } else if (strcmp(line, "texts") == 0) {
        if (!app_running) {
            reply("texts idle");
        } else {
            reply("texts done %d", host_sdl_dump_texts(emit_text));
        }
    } else if (strcmp(line, "stop") == 0) {
        if (!app_running) {
            reply("stop idle");
        } else {
            reply("stop ok");
            return CMD_ACTION_STOP;
        }
    } else {
        reply("err unknown command '%s'", line);
    }
    return CMD_ACTION_NONE;
}

/* 進行中の操作を、期限の来た段まで進める */
static void run_steps(bool app_running)
{
    if (!app_running) { /* 途中でアプリが止まった */
        reply("%.*s err app stopped", (int)strcspn(s_done_msg, " "), s_done_msg);
        s_step_count = s_step_next = 0;
        return;
    }
    const uint32_t now = SDL_GetTicks();
    while (s_step_next < s_step_count && (int32_t)(now - s_steps[s_step_next].due_ms) >= 0) {
        const Step* st = &s_steps[s_step_next++];
        switch (st->kind) {
        case STEP_PRESS:   host_sdl_push_touch(true, st->x, st->y); break;
        case STEP_MOVE:    host_sdl_push_touch_move(st->x, st->y); break;
        case STEP_RELEASE: host_sdl_push_touch(false, st->x, st->y); break;
        case STEP_DONE:    reply("%s", s_done_msg); break;
        }
    }
    if (s_step_next >= s_step_count) s_step_count = s_step_next = 0;
}

cmd_action_t cmd_fifo_poll(bool app_running)
{
    if (s_fd < 0) return CMD_ACTION_NONE;
    if (s_step_count > 0) {
        run_steps(app_running);
        return CMD_ACTION_NONE;
    }
    /* 届いている行を順に解釈する。操作(tap / hold / drag)を積んだら、終わるまで次の行を読まない */
    for (;;) {
        char c;
        const ssize_t r = read(s_fd, &c, 1);
        if (r <= 0) return CMD_ACTION_NONE;
        if (c == '\n') {
            s_line[s_line_len] = '\0';
            const bool overflow = s_line_overflow;
            s_line_len = 0;
            s_line_overflow = false;
            if (overflow) {
                reply("err line too long");
                continue;
            }
            const cmd_action_t a = dispatch(s_line, app_running);
            if (a != CMD_ACTION_NONE || s_step_count > 0) {
                if (s_step_count > 0) run_steps(app_running);
                return a;
            }
            continue;
        }
        if (s_line_len + 1 < sizeof(s_line)) s_line[s_line_len++] = c;
        else s_line_overflow = true;
    }
}
