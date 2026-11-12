/*
 * platform_headless.c - deterministic backend with no window or devices.
 * See headless.h for the behaviour of each platform.h function here.
 *
 * All state is static; the backend itself never calls malloc.
 */
#include "platform/headless/headless.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TIME_STEP_US 1000u
#define DUMP_DIR_MAX 512u

typedef struct storage_slot {
    bool     written;
    uint32_t size;
    uint8_t  data[PLATFORM_STORAGE_SLOT_MAX];
} storage_slot_t;

static struct {
    /* time */
    uint64_t now_us;
    uint64_t step_us;

    /* input */
    headless_input_run_t runs[HEADLESS_MAX_INPUT_RUNS];
    uint32_t run_count;
    uint32_t run_index;   /* current run */
    uint32_t run_consumed; /* polls served from the current run */
    bool     quit_latched; /* a scripted entry has raised Quit */
    uint64_t poll_count;

    /* display */
    uint8_t  pixels[PLATFORM_FB_SIZE];
    uint32_t palette[PLATFORM_PALETTE_SIZE];
    uint64_t frame_count;
    char     dump_dir[DUMP_DIR_MAX];
    uint32_t dump_every;
    bool     dump_failed;

    /* audio */
    platform_audio_fn audio_fn;
    void             *audio_user;

    /* storage */
    storage_slot_t slots[PLATFORM_STORAGE_SLOTS];
} g = {.step_us = DEFAULT_TIME_STEP_US};

/* ---- control ----------------------------------------------------------- */

void headless_reset(void)
{
    memset(&g, 0, sizeof g);
    g.step_us = DEFAULT_TIME_STEP_US;
}

void headless_set_time_us(uint64_t now_us)
{
    if (now_us > g.now_us) {
        g.now_us = now_us;
    }
}

void headless_set_time_step_us(uint64_t step_us) { g.step_us = step_us; }

void headless_advance_time_us(uint64_t delta_us) { g.now_us += delta_us; }

static void clear_script(void)
{
    g.run_count = 0;
    g.run_index = 0;
    g.run_consumed = 0;
    g.quit_latched = false;
    g.poll_count = 0;
}

bool headless_set_input_script(const headless_input_run_t *runs,
                               uint32_t run_count)
{
    clear_script();
    if (run_count > HEADLESS_MAX_INPUT_RUNS) {
        return false;
    }
    for (uint32_t i = 0; i < run_count; i++) {
        if ((runs[i].buttons & ~PLATFORM_BTN_VALID) != 0) {
            clear_script();
            return false;
        }
        if (runs[i].count != 0) {
            g.runs[g.run_count++] = runs[i];
        }
    }
    return true;
}

/* Parse one unsigned 32-bit number in C notation; advances *s past it. */
static bool parse_u32(char **s, uint32_t *out)
{
    char *end;
    errno = 0;
    if (**s == '-') {
        return false;
    }
    unsigned long long v = strtoull(*s, &end, 0);
    if (end == *s || errno != 0 || v > 0xFFFFFFFFull) {
        return false;
    }
    *s = end;
    *out = (uint32_t)v;
    return true;
}

static char *skip_space(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return s;
}

bool headless_load_input_script(const char *path)
{
    clear_script();

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "headless: cannot open input script '%s'\n", path);
        return false;
    }

    char line[256];
    unsigned line_no = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof line, f) != NULL) {
        line_no++;
        if (strchr(line, '\n') == NULL && !feof(f)) {
            fprintf(stderr, "headless: %s:%u: line too long\n", path, line_no);
            ok = false;
            break;
        }
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *s = skip_space(line);
        if (*s == '\0') {
            continue;
        }

        headless_input_run_t run = {0, 1};
        ok = parse_u32(&s, &run.buttons);
        s = skip_space(s);
        if (ok && *s != '\0') {
            ok = parse_u32(&s, &run.count);
            s = skip_space(s);
        }
        if (ok && *s != '\0') {
            ok = false;
        }
        if (!ok) {
            fprintf(stderr, "headless: %s:%u: expected '<buttons> [<count>]'\n",
                    path, line_no);
            break;
        }
        if ((run.buttons & ~PLATFORM_BTN_VALID) != 0) {
            fprintf(stderr, "headless: %s:%u: reserved button bits in 0x%08lx\n",
                    path, line_no, (unsigned long)run.buttons);
            ok = false;
            break;
        }
        if (run.count == 0) {
            continue;
        }
        if (g.run_count == HEADLESS_MAX_INPUT_RUNS) {
            fprintf(stderr, "headless: %s:%u: more than %u runs\n", path,
                    line_no, HEADLESS_MAX_INPUT_RUNS);
            ok = false;
            break;
        }
        g.runs[g.run_count++] = run;
    }
    if (ferror(f)) {
        fprintf(stderr, "headless: error reading '%s'\n", path);
        ok = false;
    }
    fclose(f);

    if (!ok) {
        clear_script();
    }
    return ok;
}

uint64_t headless_poll_count(void) { return g.poll_count; }

bool headless_set_frame_dump(const char *dir, uint32_t every)
{
    g.dump_every = 0;
    g.dump_dir[0] = '\0';
    if (dir == NULL || every == 0) {
        return true;
    }
    size_t len = strlen(dir);
    if (len >= DUMP_DIR_MAX) {
        return false;
    }
    memcpy(g.dump_dir, dir, len + 1);
    g.dump_every = every;
    return true;
}

uint64_t headless_frame_count(void) { return g.frame_count; }

void headless_last_frame(const uint8_t **pixels, const uint32_t **palette)
{
    bool have = g.frame_count != 0;
    *pixels = have ? g.pixels : NULL;
    *palette = have ? g.palette : NULL;
}

bool headless_write_ppm(const char *path, const uint8_t *pixels,
                        const uint32_t *palette)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    bool ok = fprintf(f, "P6\n%d %d\n255\n", PLATFORM_FB_WIDTH,
                      PLATFORM_FB_HEIGHT) > 0;

    uint8_t row[PLATFORM_FB_WIDTH * 3];
    for (int y = 0; ok && y < PLATFORM_FB_HEIGHT; y++) {
        const uint8_t *src = pixels + (size_t)y * PLATFORM_FB_WIDTH;
        for (int x = 0; x < PLATFORM_FB_WIDTH; x++) {
            uint32_t rgb = palette[src[x]];
            row[x * 3 + 0] = (uint8_t)(rgb >> 16);
            row[x * 3 + 1] = (uint8_t)(rgb >> 8);
            row[x * 3 + 2] = (uint8_t)rgb;
        }
        ok = fwrite(row, 1, sizeof row, f) == sizeof row;
    }
    if (fclose(f) != 0) {
        ok = false;
    }
    return ok;
}

bool headless_frame_dump_ok(void) { return !g.dump_failed; }

bool headless_audio_pump(int16_t *frames, uint32_t frame_count)
{
    if (g.audio_fn == NULL || frame_count == 0) {
        return false;
    }
    g.audio_fn(g.audio_user, frames, frame_count);
    return true;
}

/* ---- platform.h -------------------------------------------------------- */

void platform_present(const uint8_t *pixels, const uint32_t *palette)
{
    memcpy(g.pixels, pixels, sizeof g.pixels);
    memcpy(g.palette, palette, sizeof g.palette);

    uint64_t index = g.frame_count++;
    if (g.dump_every != 0 && index % g.dump_every == 0) {
        char path[DUMP_DIR_MAX + 32];
        snprintf(path, sizeof path, "%s/frame_%06llu.ppm", g.dump_dir,
                 (unsigned long long)index);
        if (!headless_write_ppm(path, g.pixels, g.palette)) {
            if (!g.dump_failed) {
                fprintf(stderr, "headless: cannot write '%s'\n", path);
            }
            g.dump_failed = true;
        }
    }
}

void platform_poll_input(platform_input_t *out)
{
    g.poll_count++;
    if (g.run_index >= g.run_count) {
        out->buttons = PLATFORM_BTN_QUIT;
        return;
    }
    const headless_input_run_t *run = &g.runs[g.run_index];
    if (run->buttons & PLATFORM_BTN_QUIT) {
        g.quit_latched = true;
    }
    out->buttons = run->buttons | (g.quit_latched ? PLATFORM_BTN_QUIT : 0);
    if (++g.run_consumed == run->count) {
        g.run_index++;
        g.run_consumed = 0;
    }
}

uint64_t platform_time_us(void)
{
    uint64_t now = g.now_us;
    g.now_us += g.step_us;
    return now;
}

bool platform_audio_set_callback(platform_audio_fn fn, void *user)
{
    g.audio_fn = fn;
    g.audio_user = fn != NULL ? user : NULL;
    return true;
}

int32_t platform_storage_read(uint32_t slot, void *buf, uint32_t capacity)
{
    if (slot >= PLATFORM_STORAGE_SLOTS) {
        return PLATFORM_STORAGE_ERROR;
    }
    const storage_slot_t *s = &g.slots[slot];
    if (!s->written) {
        return PLATFORM_STORAGE_EMPTY;
    }
    if (s->size > capacity) {
        return PLATFORM_STORAGE_ERROR;
    }
    if (s->size != 0) {
        memcpy(buf, s->data, s->size);
    }
    return (int32_t)s->size;
}

bool platform_storage_write(uint32_t slot, const void *data, uint32_t size)
{
    if (slot >= PLATFORM_STORAGE_SLOTS || size > PLATFORM_STORAGE_SLOT_MAX) {
        return false;
    }
    storage_slot_t *s = &g.slots[slot];
    if (size != 0) {
        memcpy(s->data, data, size);
    }
    s->size = size;
    s->written = true;
    return true;
}
