/*
 * test_headless.c - the headless backend against the platform.h contract
 * and its own documented behaviour (platform/headless/headless.h).
 *
 * TEST_TMP_DIR is an existing scratch directory supplied by the build.
 */
#include <stdio.h>
#include <string.h>

#include "kernel/platform.h"
#include "platform/headless/headless.h"
#include "tests/support/test.h"

/* ---- time ---- */

static void time_starts_at_zero_and_steps_per_query(void)
{
    headless_reset();
    CHECK_EQ_U(platform_time_us(), 0);
    CHECK_EQ_U(platform_time_us(), 1000);
    CHECK_EQ_U(platform_time_us(), 2000);
}

static void time_step_is_configurable_and_can_freeze(void)
{
    headless_reset();
    headless_set_time_step_us(0);
    CHECK_EQ_U(platform_time_us(), 0);
    CHECK_EQ_U(platform_time_us(), 0);
    headless_advance_time_us(33334);
    CHECK_EQ_U(platform_time_us(), 33334);
    headless_set_time_step_us(7);
    CHECK_EQ_U(platform_time_us(), 33334);
    CHECK_EQ_U(platform_time_us(), 33341);
}

static void time_never_goes_backwards(void)
{
    headless_reset();
    headless_set_time_step_us(0);
    headless_set_time_us(5000);
    headless_set_time_us(100); /* ignored */
    CHECK_EQ_U(platform_time_us(), 5000);
}

/* ---- input ---- */

static uint32_t poll(void)
{
    platform_input_t in = {0xDEADBEEFu};
    platform_poll_input(&in);
    return in.buttons;
}

static void input_without_script_is_quit(void)
{
    headless_reset();
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT);
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT);
}

static void input_script_plays_one_entry_per_poll_then_quits(void)
{
    static const headless_input_run_t runs[] = {
        {PLATFORM_BTN_A, 2},
        {0, 0}, /* skipped */
        {PLATFORM_BTN_UP | PLATFORM_BTN_B, 1},
        {0, 1},
    };
    headless_reset();
    REQUIRE(headless_set_input_script(runs, 4));
    CHECK_EQ_U(headless_poll_count(), 0);
    CHECK_EQ_U(poll(), PLATFORM_BTN_A);
    CHECK_EQ_U(poll(), PLATFORM_BTN_A);
    CHECK_EQ_U(poll(), PLATFORM_BTN_UP | PLATFORM_BTN_B);
    CHECK_EQ_U(poll(), 0);
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT);
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT);
    CHECK_EQ_U(headless_poll_count(), 6);
}

static void input_script_is_independent_of_time(void)
{
    static const headless_input_run_t runs[] = {{1, 1}, {2, 1}, {4, 1}};
    headless_reset();
    REQUIRE(headless_set_input_script(runs, 3));
    CHECK_EQ_U(poll(), 1);
    headless_advance_time_us(10u * 1000u * 1000u);
    (void)platform_time_us();
    CHECK_EQ_U(poll(), 2);
    CHECK_EQ_U(poll(), 4);
}

static void input_script_too_long_is_rejected(void)
{
    static headless_input_run_t runs[HEADLESS_MAX_INPUT_RUNS + 1];
    for (uint32_t i = 0; i <= HEADLESS_MAX_INPUT_RUNS; i++) {
        runs[i].buttons = 1;
        runs[i].count = 1;
    }
    headless_reset();
    CHECK(!headless_set_input_script(runs, HEADLESS_MAX_INPUT_RUNS + 1));
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT);
    CHECK(headless_set_input_script(runs, HEADLESS_MAX_INPUT_RUNS));
    CHECK_EQ_U(poll(), 1);
}

static bool write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return false;
    }
    bool ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}

static void input_script_file_is_parsed(void)
{
    const char *path = TEST_TMP_DIR "/script_ok.txt";
    REQUIRE(write_text(path, "# comment line\n"
                             "\n"
                             "0x10 2   # A held for two ticks\n"
                             "   17\n"
                             "0 0      # zero count: skipped\n"
                             "0x80000000\n"));
    headless_reset();
    REQUIRE(headless_load_input_script(path));
    CHECK_EQ_U(poll(), 0x10);
    CHECK_EQ_U(poll(), 0x10);
    CHECK_EQ_U(poll(), 17);
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT); /* scripted quit */
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT); /* exhausted */
}

static void input_script_file_errors_leave_no_script(void)
{
    static const char *const bad[] = {
        "1 2 3\n",        /* trailing token */
        "up\n",           /* not a number */
        "-1\n",           /* negative */
        "0x100000000\n",  /* does not fit 32 bits */
        "1\n1 x\n",       /* error after a good line */
    };
    const char *path = TEST_TMP_DIR "/script_bad.txt";
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        REQUIRE(write_text(path, bad[i]));
        headless_reset();
        CHECK(!headless_load_input_script(path));
        CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT);
    }
    headless_reset();
    CHECK(!headless_load_input_script(TEST_TMP_DIR "/does_not_exist.txt"));
}

/* ---- display ---- */

static uint8_t  s_pixels[PLATFORM_FB_SIZE];
static uint32_t s_palette[PLATFORM_PALETTE_SIZE];

static void fill_frame(uint8_t salt)
{
    for (uint32_t i = 0; i < PLATFORM_FB_SIZE; i++) {
        s_pixels[i] = (uint8_t)(i * 7u + salt);
    }
    for (uint32_t i = 0; i < PLATFORM_PALETTE_SIZE; i++) {
        /* Top byte set on purpose: backends must ignore it. */
        s_palette[i] = 0xFF000000u | (i << 16) | ((255u - i) << 8) | (i ^ 0x55u);
    }
}

static void present_copies_the_frame(void)
{
    const uint8_t *px;
    const uint32_t *pal;
    headless_reset();
    headless_last_frame(&px, &pal);
    CHECK(px == NULL && pal == NULL);
    CHECK_EQ_U(headless_frame_count(), 0);

    fill_frame(1);
    platform_present(s_pixels, s_palette);
    CHECK_EQ_U(headless_frame_count(), 1);

    /* The caller may scribble on its buffers straight away. */
    static uint8_t  want_px[PLATFORM_FB_SIZE];
    static uint32_t want_pal[PLATFORM_PALETTE_SIZE];
    memcpy(want_px, s_pixels, sizeof want_px);
    memcpy(want_pal, s_palette, sizeof want_pal);
    memset(s_pixels, 0, sizeof s_pixels);
    memset(s_palette, 0, sizeof s_palette);

    headless_last_frame(&px, &pal);
    REQUIRE(px != NULL && pal != NULL);
    CHECK(px != s_pixels && pal != s_palette);
    CHECK(memcmp(px, want_px, sizeof want_px) == 0);
    CHECK(memcmp(pal, want_pal, sizeof want_pal) == 0);
}

/* Read a whole file; returns its size or -1. */
static long slurp(const char *path, uint8_t *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t n = fread(buf, 1, cap, f);
    bool at_end = fgetc(f) == EOF;
    fclose(f);
    return at_end ? (long)n : -1;
}

static void check_ppm_matches(const char *path, const uint8_t *px,
                              const uint32_t *pal)
{
    static uint8_t file[PLATFORM_FB_SIZE * 3 + 64];
    static const char header[] = "P6\n320 240\n255\n";
    const size_t header_len = sizeof header - 1;

    long n = slurp(path, file, sizeof file);
    REQUIRE(n == (long)(header_len + PLATFORM_FB_SIZE * 3));
    REQUIRE(memcmp(file, header, header_len) == 0);

    uint32_t mismatches = 0;
    for (uint32_t i = 0; i < PLATFORM_FB_SIZE; i++) {
        const uint8_t *rgb = file + header_len + i * 3u;
        uint32_t c = pal[px[i]];
        if (rgb[0] != ((c >> 16) & 0xFFu) || rgb[1] != ((c >> 8) & 0xFFu) ||
            rgb[2] != (c & 0xFFu)) {
            mismatches++;
        }
    }
    CHECK_EQ_U(mismatches, 0);
}

static void ppm_dump_writes_selected_frames(void)
{
    headless_reset();
    remove(TEST_TMP_DIR "/frame_000000.ppm");
    remove(TEST_TMP_DIR "/frame_000001.ppm");
    remove(TEST_TMP_DIR "/frame_000002.ppm");
    REQUIRE(headless_set_frame_dump(TEST_TMP_DIR, 2));

    static uint8_t  px2[PLATFORM_FB_SIZE];
    static uint32_t pal2[PLATFORM_PALETTE_SIZE];
    for (uint8_t i = 0; i < 3; i++) {
        fill_frame(i);
        platform_present(s_pixels, s_palette);
        if (i == 2) {
            memcpy(px2, s_pixels, sizeof px2);
            memcpy(pal2, s_palette, sizeof pal2);
        }
    }
    CHECK(headless_frame_dump_ok());

    FILE *f0 = fopen(TEST_TMP_DIR "/frame_000000.ppm", "rb");
    FILE *f1 = fopen(TEST_TMP_DIR "/frame_000001.ppm", "rb");
    CHECK(f0 != NULL);
    CHECK(f1 == NULL); /* every second frame only */
    if (f0 != NULL) {
        fclose(f0);
    }
    if (f1 != NULL) {
        fclose(f1);
    }
    check_ppm_matches(TEST_TMP_DIR "/frame_000002.ppm", px2, pal2);
}

static void ppm_dump_failure_is_reported_not_fatal(void)
{
    headless_reset();
    REQUIRE(headless_set_frame_dump(TEST_TMP_DIR "/no/such/dir", 1));
    fill_frame(0);
    platform_present(s_pixels, s_palette);
    CHECK(!headless_frame_dump_ok());
    CHECK_EQ_U(headless_frame_count(), 1);
    headless_reset();
    CHECK(headless_frame_dump_ok());
}

/* ---- audio ---- */

typedef struct audio_probe {
    uint32_t calls;
    uint32_t frames;
} audio_probe_t;

static void probe_audio(void *user, int16_t *frames, uint32_t frame_count)
{
    audio_probe_t *p = user;
    p->calls++;
    p->frames += frame_count;
    for (uint32_t i = 0; i < frame_count; i++) {
        frames[i] = (int16_t)(100 + (int)i);
    }
}

static void audio_callback_is_driven_only_by_pump(void)
{
    int16_t buf[8] = {0};
    audio_probe_t probe = {0, 0};
    headless_reset();

    CHECK(!headless_audio_pump(buf, 8)); /* nothing registered */
    CHECK(platform_audio_set_callback(probe_audio, &probe));
    CHECK_EQ_U(probe.calls, 0);

    /* Nothing else in the backend may invoke it. */
    fill_frame(0);
    platform_present(s_pixels, s_palette);
    (void)platform_time_us();
    (void)poll();
    CHECK_EQ_U(probe.calls, 0);

    CHECK(headless_audio_pump(buf, 8));
    CHECK_EQ_U(probe.calls, 1);
    CHECK_EQ_U(probe.frames, 8);
    CHECK_EQ_I(buf[0], 100);
    CHECK_EQ_I(buf[7], 107);
    CHECK(!headless_audio_pump(buf, 0));

    CHECK(platform_audio_set_callback(NULL, NULL));
    CHECK(!headless_audio_pump(buf, 8));
    CHECK_EQ_U(probe.calls, 1);
}

/* ---- storage ---- */

static void storage_slots_start_empty(void)
{
    uint8_t buf[4];
    headless_reset();
    for (uint32_t slot = 0; slot < PLATFORM_STORAGE_SLOTS; slot++) {
        CHECK_EQ_I(platform_storage_read(slot, buf, sizeof buf),
                   PLATFORM_STORAGE_EMPTY);
    }
}

static void storage_round_trips_and_replaces(void)
{
    static const uint8_t a[] = {1, 2, 3, 4, 5};
    static const uint8_t b[] = {9, 8};
    uint8_t buf[8];
    headless_reset();

    CHECK(platform_storage_write(3, a, sizeof a));
    memset(buf, 0xEE, sizeof buf);
    CHECK_EQ_I(platform_storage_read(3, buf, sizeof buf), 5);
    CHECK(memcmp(buf, a, sizeof a) == 0);
    CHECK_EQ_U(buf[5], 0xEE); /* nothing written past the blob */

    CHECK(platform_storage_write(3, b, sizeof b));
    CHECK_EQ_I(platform_storage_read(3, buf, sizeof buf), 2);
    CHECK(memcmp(buf, b, sizeof b) == 0);

    /* Other slots are unaffected. */
    CHECK_EQ_I(platform_storage_read(2, buf, sizeof buf),
               PLATFORM_STORAGE_EMPTY);
}

static void storage_empty_blob_is_not_an_empty_slot(void)
{
    headless_reset();
    CHECK(platform_storage_write(0, NULL, 0));
    CHECK_EQ_I(platform_storage_read(0, NULL, 0), 0);
}

static void storage_rejects_bad_arguments(void)
{
    static uint8_t big[PLATFORM_STORAGE_SLOT_MAX + 1];
    static const uint8_t a[] = {1, 2, 3, 4};
    uint8_t buf[8];
    headless_reset();

    CHECK(!platform_storage_write(PLATFORM_STORAGE_SLOTS, a, sizeof a));
    CHECK_EQ_I(platform_storage_read(PLATFORM_STORAGE_SLOTS, buf, sizeof buf),
               PLATFORM_STORAGE_ERROR);

    /* Too large: rejected, previous contents intact. */
    CHECK(platform_storage_write(1, a, sizeof a));
    CHECK(!platform_storage_write(1, big, sizeof big));
    CHECK_EQ_I(platform_storage_read(1, buf, sizeof buf), 4);
    CHECK(memcmp(buf, a, sizeof a) == 0);

    /* Buffer too small is an error, not a truncated read. */
    CHECK_EQ_I(platform_storage_read(1, buf, 3), PLATFORM_STORAGE_ERROR);
}

static void storage_holds_a_full_slot(void)
{
    static uint8_t out[PLATFORM_STORAGE_SLOT_MAX];
    static uint8_t in[PLATFORM_STORAGE_SLOT_MAX];
    for (uint32_t i = 0; i < PLATFORM_STORAGE_SLOT_MAX; i++) {
        out[i] = (uint8_t)(i * 31u + (i >> 8));
    }
    headless_reset();
    CHECK(platform_storage_write(7, out, sizeof out));
    CHECK_EQ_I(platform_storage_read(7, in, sizeof in),
               (int32_t)PLATFORM_STORAGE_SLOT_MAX);
    CHECK(memcmp(in, out, sizeof out) == 0);
}

static void reset_clears_everything(void)
{
    static const headless_input_run_t runs[] = {{1, 5}};
    static const uint8_t a[] = {1};
    audio_probe_t probe = {0, 0};
    int16_t buf[2];
    uint8_t byte;

    headless_reset();
    headless_set_input_script(runs, 1);
    (void)platform_time_us();
    fill_frame(0);
    platform_present(s_pixels, s_palette);
    platform_storage_write(0, a, 1);
    platform_audio_set_callback(probe_audio, &probe);

    headless_reset();
    CHECK_EQ_U(platform_time_us(), 0);
    CHECK_EQ_U(poll(), PLATFORM_BTN_QUIT);
    CHECK_EQ_U(headless_frame_count(), 0);
    CHECK_EQ_I(platform_storage_read(0, &byte, 1), PLATFORM_STORAGE_EMPTY);
    CHECK(!headless_audio_pump(buf, 2));
}

TEST_MAIN(
    TEST(time_starts_at_zero_and_steps_per_query),
    TEST(time_step_is_configurable_and_can_freeze),
    TEST(time_never_goes_backwards),
    TEST(input_without_script_is_quit),
    TEST(input_script_plays_one_entry_per_poll_then_quits),
    TEST(input_script_is_independent_of_time),
    TEST(input_script_too_long_is_rejected),
    TEST(input_script_file_is_parsed),
    TEST(input_script_file_errors_leave_no_script),
    TEST(present_copies_the_frame),
    TEST(ppm_dump_writes_selected_frames),
    TEST(ppm_dump_failure_is_reported_not_fatal),
    TEST(audio_callback_is_driven_only_by_pump),
    TEST(storage_slots_start_empty),
    TEST(storage_round_trips_and_replaces),
    TEST(storage_empty_blob_is_not_an_empty_slot),
    TEST(storage_rejects_bad_arguments),
    TEST(storage_holds_a_full_slot),
    TEST(reset_clears_everything))
