/*
 * test_determinism.c - self-test of the determinism harness.
 *
 * There is no simulation yet, so the subject here is a toy: a few integers
 * stirred by the tick number and the input mask. It stands in for "any
 * integer-only state machine" and proves three things about the harness:
 * it reports equal hashes for equal runs, it notices when anything changes,
 * and the integer arithmetic it exercises hashes to the same constant on
 * every compiler and CPU this is built for.
 *
 * When real systems exist, add subjects for them here in the same shape.
 */
#include "tests/support/determinism.h"
#include "tests/support/test.h"

typedef struct toy {
    uint32_t lcg;
    uint32_t presses; /* rising edges of A */
    uint32_t prev_buttons;
    int32_t  x;       /* 16.16 fixed point, pushed around by the d-pad */
    uint64_t ticks_seen;
} toy_t;

static void toy_init(void *state)
{
    toy_t *t = state;
    t->lcg = 12345u;
}

static void toy_tick(void *state, uint64_t tick, const platform_input_t *in)
{
    toy_t *t = state;
    uint32_t pressed = in->buttons & ~t->prev_buttons;

    t->lcg = t->lcg * 1664525u + 1013904223u + (uint32_t)tick;
    t->lcg ^= in->buttons;
    if (pressed & PLATFORM_BTN_A) {
        t->presses++;
    }
    if (in->buttons & PLATFORM_BTN_RIGHT) {
        t->x += 0x18000; /* +1.5 */
    }
    if (in->buttons & PLATFORM_BTN_LEFT) {
        /* Right shift of a negative value is the case worth pinning down:
         * it is arithmetic on every target we build for. */
        t->x = (t->x - 0x28000) >> 1;
    }
    t->prev_buttons = in->buttons;
    t->ticks_seen++;
}

static toy_t s_toy;
static const det_subject_t s_subject = {&s_toy, sizeof s_toy, toy_init,
                                        toy_tick};

static const headless_input_run_t s_runs[] = {
    {0, 10},
    {PLATFORM_BTN_RIGHT, 30},
    {PLATFORM_BTN_RIGHT | PLATFORM_BTN_A, 1},
    {PLATFORM_BTN_RIGHT, 4},
    {PLATFORM_BTN_A, 2},
    {PLATFORM_BTN_LEFT, 50},
    {0, 3},
};
static const det_script_t s_script = {s_runs, 7};
#define SCRIPT_TICKS 100u

/*
 * Recorded on x86-64/gcc. Any target that disagrees is not bit-identical.
 * The state is hashed as raw bytes, so this value is for little-endian
 * targets, which is every target the engine has.
 */
#define TOY_GOLDEN_HASH 0xa74b1560cb2fc9aeull

static void same_script_same_hash(void)
{
    uint64_t hash = 0;
    CHECK(det_check(&s_subject, &s_script, SCRIPT_TICKS, &hash));
    CHECK(hash != DET_HASH_SEED);
    CHECK_EQ_U(s_toy.ticks_seen, SCRIPT_TICKS);
    CHECK_EQ_U(s_toy.presses, 2);
}

static void hash_matches_recorded_value(void)
{
    CHECK_EQ_U(det_run(&s_subject, &s_script, SCRIPT_TICKS), TOY_GOLDEN_HASH);
}

static void different_tick_count_changes_hash(void)
{
    uint64_t a = det_run(&s_subject, &s_script, SCRIPT_TICKS);
    uint64_t b = det_run(&s_subject, &s_script, SCRIPT_TICKS - 1);
    CHECK(a != b);
}

static void one_changed_input_changes_hash(void)
{
    headless_input_run_t runs[7];
    for (int i = 0; i < 7; i++) {
        runs[i] = s_runs[i];
    }
    runs[3].buttons |= PLATFORM_BTN_B; /* one extra button for four ticks */
    det_script_t script = {runs, 7};

    uint64_t a = det_run(&s_subject, &s_script, SCRIPT_TICKS);
    uint64_t b = det_run(&s_subject, &script, SCRIPT_TICKS);
    CHECK(a != b);
}

static void run_length_encoding_does_not_matter(void)
{
    /* The same per-tick inputs, split into different runs. */
    static const headless_input_run_t split[] = {
        {0, 4},
        {0, 6},
        {PLATFORM_BTN_RIGHT, 29},
        {PLATFORM_BTN_RIGHT, 1},
        {PLATFORM_BTN_RIGHT | PLATFORM_BTN_A, 1},
        {PLATFORM_BTN_RIGHT, 4},
        {PLATFORM_BTN_A, 1},
        {PLATFORM_BTN_A, 1},
        {PLATFORM_BTN_LEFT, 50},
        {0, 3},
    };
    det_script_t script = {split, 10};
    CHECK_EQ_U(det_run(&s_subject, &script, SCRIPT_TICKS),
               det_run(&s_subject, &s_script, SCRIPT_TICKS));
}

static void clock_does_not_influence_the_run(void)
{
    /* The harness must never read time: after a run the virtual clock has
     * not been queried, so it still reads zero. */
    uint64_t a = det_run(&s_subject, &s_script, SCRIPT_TICKS);
    CHECK_EQ_U(platform_time_us(), 0); /* harness made no clock queries */
    uint64_t b = det_run(&s_subject, &s_script, SCRIPT_TICKS);
    CHECK_EQ_U(a, b);
}

/* A subject that is *not* deterministic, to prove det_check can fail. */
static uint32_t s_hidden_counter;

static void leaky_tick(void *state, uint64_t tick, const platform_input_t *in)
{
    (void)tick;
    (void)in;
    uint32_t *v = state;
    *v += ++s_hidden_counter; /* state outside the hashed block */
}

static void hidden_state_is_detected(void)
{
    uint32_t value;
    det_subject_t leaky = {&value, sizeof value, NULL, leaky_tick};
    s_hidden_counter = 0;
    CHECK(!det_check(&leaky, &s_script, 10, NULL));
}

static void hash_is_fnv1a_64(void)
{
    /* Published FNV-1a 64-bit test vectors. */
    CHECK_EQ_U(det_hash("", 0, DET_HASH_SEED), 0xcbf29ce484222325ull);
    CHECK_EQ_U(det_hash("a", 1, DET_HASH_SEED), 0xaf63dc4c8601ec8cull);
    CHECK_EQ_U(det_hash("foobar", 6, DET_HASH_SEED), 0x85944171f73967e8ull);
    /* Chaining equals hashing the concatenation. */
    CHECK_EQ_U(det_hash("bar", 3, det_hash("foo", 3, DET_HASH_SEED)),
               0x85944171f73967e8ull);
}

TEST_MAIN(
    TEST(hash_is_fnv1a_64),
    TEST(same_script_same_hash),
    TEST(hash_matches_recorded_value),
    TEST(different_tick_count_changes_hash),
    TEST(one_changed_input_changes_hash),
    TEST(run_length_encoding_does_not_matter),
    TEST(clock_does_not_influence_the_run),
    TEST(hidden_state_is_detected))
