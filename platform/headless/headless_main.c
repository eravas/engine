/*
 * headless_main.c - process entry point for headless applications.
 *
 *   <app> [--input FILE | --frames N] [--dump-dir DIR] [--dump-every N]
 *         [--time-step-us N]
 *
 *   --input FILE      input script (format: see headless.h)
 *   --frames N        no script: N ticks of no input, then quit (default 60)
 *   --dump-dir DIR    write presented frames as DIR/frame_NNNNNN.ppm
 *   --dump-every N    dump every N-th frame (default 1)
 *   --time-step-us N  virtual microseconds per clock query (default 1000)
 *
 * Kept apart from platform_headless.c so that tests can link the backend and
 * supply their own main().
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/headless/headless.h"

static int usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--input FILE | --frames N] [--dump-dir DIR]\n"
            "       [--dump-every N] [--time-step-us N]\n",
            argv0);
    return 2;
}

static bool parse_u64(const char *s, uint64_t *out)
{
    char *end;
    errno = 0;
    if (*s == '\0' || *s == '-') {
        return false;
    }
    unsigned long long v = strtoull(s, &end, 0);
    if (*end != '\0' || errno != 0) {
        return false;
    }
    *out = v;
    return true;
}

int main(int argc, char **argv)
{
    const char *input = NULL;
    const char *dump_dir = NULL;
    uint64_t frames = 60;
    uint64_t dump_every = 1;
    uint64_t time_step = 1000;

    for (int i = 1; i < argc; i++) {
        const char *opt = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        bool ok = val != NULL;
        if (ok && strcmp(opt, "--input") == 0) {
            input = val;
        } else if (ok && strcmp(opt, "--dump-dir") == 0) {
            dump_dir = val;
        } else if (ok && strcmp(opt, "--frames") == 0) {
            ok = parse_u64(val, &frames) && frames <= 0xFFFFFFFFu;
        } else if (ok && strcmp(opt, "--dump-every") == 0) {
            ok = parse_u64(val, &dump_every) && dump_every <= 0xFFFFFFFFu;
        } else if (ok && strcmp(opt, "--time-step-us") == 0) {
            ok = parse_u64(val, &time_step);
        } else {
            ok = false;
        }
        if (!ok) {
            return usage(argv[0]);
        }
        i++;
    }

    headless_reset();
    headless_set_time_step_us(time_step);

    if (input != NULL) {
        if (!headless_load_input_script(input)) {
            return 2;
        }
    } else {
        headless_input_run_t idle = {0, (uint32_t)frames};
        headless_set_input_script(&idle, 1);
    }
    if (!headless_set_frame_dump(dump_dir, (uint32_t)dump_every)) {
        fprintf(stderr, "headless: --dump-dir path too long\n");
        return 2;
    }

    int status = app_main();

    if (!headless_frame_dump_ok()) {
        return 1;
    }
    return status;
}
