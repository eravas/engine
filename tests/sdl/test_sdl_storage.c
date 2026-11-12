/*
 * test_sdl_storage.c - the SDL backend's storage against platform.h.
 *
 * Runs through the SDL backend's own main() (on dummy video and audio
 * drivers), so it uses app_main() and only platform.h, plus stdio to look
 * at the files the backend leaves behind.
 *
 * The build runs it several times with different --storage-dir arguments
 * and tells it what to expect through the STORAGE_MODE environment
 * variable:
 *
 *   (unset)  TEST_STORAGE_DIR, emptied beforehand except that slot5.bin
 *            exists as a directory: a slot that is present but unreadable.
 *   broken   a directory that cannot exist or cannot be named: every read
 *            must be an error, never "empty", and every write must fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/platform.h"
#include "tests/support/test.h"

static bool broken_mode(void)
{
    const char *mode = getenv("STORAGE_MODE");
    return mode != NULL && strcmp(mode, "broken") == 0;
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    }
    return f != NULL;
}

static void untouched_slots_read_empty(void)
{
    uint8_t buf[4];
    if (broken_mode()) {
        return;
    }
    CHECK_EQ_I(platform_storage_read(6, buf, sizeof buf),
               PLATFORM_STORAGE_EMPTY);
    CHECK_EQ_I(platform_storage_read(7, buf, sizeof buf),
               PLATFORM_STORAGE_EMPTY);
}

static void round_trips_and_replaces(void)
{
    static const uint8_t a[] = {1, 2, 3, 4, 5};
    static const uint8_t b[] = {9, 8};
    uint8_t buf[8];
    if (broken_mode()) {
        return;
    }
    CHECK(platform_storage_write(1, a, sizeof a));
    memset(buf, 0xEE, sizeof buf);
    CHECK_EQ_I(platform_storage_read(1, buf, sizeof buf), 5);
    CHECK(memcmp(buf, a, sizeof a) == 0);
    CHECK_EQ_U(buf[5], 0xEE);

    CHECK(platform_storage_write(1, b, sizeof b));
    CHECK_EQ_I(platform_storage_read(1, buf, sizeof buf), 2);
    CHECK(memcmp(buf, b, sizeof b) == 0);

    /* The temporary file was renamed into place, not left behind. */
    CHECK(file_exists(TEST_STORAGE_DIR "/slot1.bin"));
    CHECK(!file_exists(TEST_STORAGE_DIR "/slot1.tmp"));
}

static void empty_blob_is_not_an_empty_slot(void)
{
    if (broken_mode()) {
        return;
    }
    CHECK(platform_storage_write(2, NULL, 0));
    CHECK_EQ_I(platform_storage_read(2, NULL, 0), 0);
}

static void bad_arguments_are_rejected(void)
{
    static uint8_t big[PLATFORM_STORAGE_SLOT_MAX + 1];
    static const uint8_t a[] = {1, 2, 3, 4};
    uint8_t buf[8];

    /* True in every mode. */
    CHECK(!platform_storage_write(PLATFORM_STORAGE_SLOTS, a, sizeof a));
    CHECK_EQ_I(platform_storage_read(PLATFORM_STORAGE_SLOTS, buf, sizeof buf),
               PLATFORM_STORAGE_ERROR);
    if (broken_mode()) {
        return;
    }
    CHECK(platform_storage_write(3, a, sizeof a));
    CHECK(!platform_storage_write(3, big, sizeof big));
    CHECK_EQ_I(platform_storage_read(3, buf, sizeof buf), 4);
    CHECK(memcmp(buf, a, sizeof a) == 0);
    CHECK_EQ_I(platform_storage_read(3, buf, 3), PLATFORM_STORAGE_ERROR);
}

static void full_slot_round_trips(void)
{
    static uint8_t out[PLATFORM_STORAGE_SLOT_MAX];
    static uint8_t in[PLATFORM_STORAGE_SLOT_MAX];
    if (broken_mode()) {
        return;
    }
    for (uint32_t i = 0; i < PLATFORM_STORAGE_SLOT_MAX; i++) {
        out[i] = (uint8_t)(i * 31u + (i >> 8));
    }
    CHECK(platform_storage_write(4, out, sizeof out));
    CHECK_EQ_I(platform_storage_read(4, in, sizeof in),
               (int32_t)PLATFORM_STORAGE_SLOT_MAX);
    CHECK(memcmp(in, out, sizeof out) == 0);
}

static void present_but_unreadable_slot_is_an_error(void)
{
    /* slot5.bin is a directory: it exists, but it is not a readable blob. */
    static const uint8_t a[] = {1};
    uint8_t buf[4];
    if (broken_mode()) {
        return;
    }
    CHECK_EQ_I(platform_storage_read(5, buf, sizeof buf),
               PLATFORM_STORAGE_ERROR);
    /* It cannot be replaced either, and the attempt leaves nothing behind. */
    CHECK(!platform_storage_write(5, a, sizeof a));
    CHECK(!file_exists(TEST_STORAGE_DIR "/slot5.tmp"));
}

static void unusable_directory_is_an_error_not_empty(void)
{
    static const uint8_t a[] = {1, 2, 3};
    uint8_t buf[4];
    if (!broken_mode()) {
        return;
    }
    for (uint32_t slot = 0; slot < PLATFORM_STORAGE_SLOTS; slot++) {
        CHECK_EQ_I(platform_storage_read(slot, buf, sizeof buf),
                   PLATFORM_STORAGE_ERROR);
        CHECK(!platform_storage_write(slot, a, sizeof a));
        CHECK_EQ_I(platform_storage_read(slot, buf, sizeof buf),
                   PLATFORM_STORAGE_ERROR);
    }
}

TEST_APP_MAIN(
    TEST(untouched_slots_read_empty),
    TEST(round_trips_and_replaces),
    TEST(empty_blob_is_not_an_empty_slot),
    TEST(bad_arguments_are_rejected),
    TEST(full_slot_round_trips),
    TEST(present_but_unreadable_slot_is_an_error),
    TEST(unusable_directory_is_an_error_not_empty))
