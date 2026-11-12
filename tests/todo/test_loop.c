/*
 * test_loop.c - executable specification of kernel/loop.h.
 *
 * Label: todo. This does not link until kernel/loop.c exists, and CI does
 * not fail on it.
 *
 * The clock tests are pure arithmetic. The loop tests run against the
 * headless backend, whose virtual clock and input script make every
 * platform call observable (see platform/headless/headless.h).
 */
#include <string.h>

#include "kernel/loop.h"
#include "platform/headless/headless.h"
#include "tests/support/test.h"

#define SECOND_US 1000000ull

/* ======================================================================
 * loop_clock
 * ====================================================================== */

static void clock_first_advance_only_sets_the_epoch(void)
{
    loop_clock_t c;
    loop_clock_init(&c);
    CHECK_EQ_U(loop_clock_advance(&c, 123456789), 0);
    CHECK_EQ_U(loop_clock_dropped(&c), 0);

    /* Even a huge first timestamp yields no ticks. */
    loop_clock_init(&c);
    CHECK_EQ_U(loop_clock_advance(&c, 400ull * 24 * 3600 * SECOND_US), 0);
    CHECK_EQ_U(loop_clock_dropped(&c), 0);
}

static void clock_tick_boundary_is_exact(void)
{
    /* One tick is 33333.33.. us: not due at +33333, due at +33334. */
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, 1000);
    CHECK_EQ_U(loop_clock_advance(&c, 1000 + 33333), 0);
    CHECK_EQ_U(loop_clock_advance(&c, 1000 + 33334), 1);
    CHECK_EQ_U(loop_clock_advance(&c, 1000 + 66666), 0);
    CHECK_EQ_U(loop_clock_advance(&c, 1000 + 66667), 1);
    CHECK_EQ_U(loop_clock_advance(&c, 1000 + 99999), 0);
    CHECK_EQ_U(loop_clock_advance(&c, 1000 + 100000), 1);
}

static void clock_same_time_twice_yields_nothing(void)
{
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, 0);
    CHECK_EQ_U(loop_clock_advance(&c, 100000), 3);
    CHECK_EQ_U(loop_clock_advance(&c, 100000), 0);
}

static void clock_one_second_is_thirty_ticks_at_any_sampling(void)
{
    static const uint64_t steps[] = {1, 7, 100, 999, 1000, 16667, 33333,
                                     33334, 50000, 100000};
    for (size_t s = 0; s < sizeof steps / sizeof steps[0]; s++) {
        loop_clock_t c;
        loop_clock_init(&c);
        loop_clock_advance(&c, 0);
        uint64_t total = 0;
        for (uint64_t t = steps[s]; t < SECOND_US; t += steps[s]) {
            total += loop_clock_advance(&c, t);
        }
        total += loop_clock_advance(&c, SECOND_US);
        CHECK_EQ_U(total, LOOP_TICK_HZ);
        CHECK_EQ_U(loop_clock_dropped(&c), 0);
    }
}

static void clock_does_not_drift_over_an_hour(void)
{
    /* Irregular sampling, epoch not at zero. Adding a rounded tick length
     * each tick would be off by dozens of ticks here. */
    const uint64_t t0 = 987654321ull;
    const uint64_t hour = 3600ull * SECOND_US;
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, t0);

    uint64_t total = 0;
    uint64_t t = 0;
    uint32_t lcg = 42;
    while (t < hour) {
        lcg = lcg * 1664525u + 1013904223u;
        t += 1 + (lcg >> 16) % 60000u; /* 1 us .. 60 ms, under the limit */
        if (t > hour) {
            t = hour;
        }
        total += loop_clock_advance(&c, t0 + t);
        /* Exactness holds at every sample, not just at the end. */
        if (total != t * LOOP_TICK_HZ / SECOND_US) {
            CHECK_EQ_U(total, t * LOOP_TICK_HZ / SECOND_US);
            return;
        }
    }
    CHECK_EQ_U(total, 3600u * LOOP_TICK_HZ);
    CHECK_EQ_U(loop_clock_dropped(&c), 0);
}

static void clock_catch_up_is_limited_and_counts_drops(void)
{
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, 0);

    /* Exactly at the limit: nothing dropped. */
    CHECK_EQ_U(loop_clock_advance(&c, 133334), LOOP_MAX_CATCHUP_TICKS);
    CHECK_EQ_U(loop_clock_dropped(&c), 0);

    /* One second later 30 are due; 4 are reported, 26 dropped. */
    CHECK_EQ_U(loop_clock_advance(&c, 133334 + SECOND_US),
               LOOP_MAX_CATCHUP_TICKS);
    CHECK_EQ_U(loop_clock_dropped(&c), 30 - LOOP_MAX_CATCHUP_TICKS);

    /* The backlog is gone, not deferred. */
    CHECK_EQ_U(loop_clock_advance(&c, 133334 + SECOND_US), 0);
}

static void clock_keeps_tick_phase_after_a_drop(void)
{
    /* Ticks fall on multiples of 1/30 s from the epoch before and after. */
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, 0);

    /* 10.5 ticks elapse: 4 run, 6 are dropped, half a tick is kept. */
    CHECK_EQ_U(loop_clock_advance(&c, 350000), LOOP_MAX_CATCHUP_TICKS);
    CHECK_EQ_U(loop_clock_dropped(&c), 6);

    /* The next boundary is at 11 ticks = 366666.67 us. */
    CHECK_EQ_U(loop_clock_advance(&c, 366666), 0);
    CHECK_EQ_U(loop_clock_advance(&c, 366667), 1);
}

static void clock_ignores_time_running_backwards(void)
{
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, 1000000);
    CHECK_EQ_U(loop_clock_advance(&c, 1100000), 3);

    CHECK_EQ_U(loop_clock_advance(&c, 500000), 0);  /* earlier: ignored */
    CHECK_EQ_U(loop_clock_advance(&c, 1100000), 0); /* no time has passed */
    CHECK_EQ_U(loop_clock_advance(&c, 1200000), 3);
    CHECK_EQ_U(loop_clock_dropped(&c), 0);
}

static void clock_survives_a_year_long_gap(void)
{
    const uint64_t year = 365ull * 24 * 3600 * SECOND_US;
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, 5);
    CHECK_EQ_U(loop_clock_advance(&c, 5 + year), LOOP_MAX_CATCHUP_TICKS);
    CHECK_EQ_U(loop_clock_dropped(&c),
               year / SECOND_US * LOOP_TICK_HZ - LOOP_MAX_CATCHUP_TICKS);
    /* And it is back in step straight away. */
    CHECK_EQ_U(loop_clock_advance(&c, 5 + year + 33333), 0);
    CHECK_EQ_U(loop_clock_advance(&c, 5 + year + 33334), 1);
}

static void clock_init_clears_everything(void)
{
    loop_clock_t c;
    loop_clock_init(&c);
    loop_clock_advance(&c, 0);
    loop_clock_advance(&c, 10 * SECOND_US);
    CHECK(loop_clock_dropped(&c) != 0);

    loop_clock_init(&c);
    CHECK_EQ_U(loop_clock_dropped(&c), 0);
    CHECK_EQ_U(loop_clock_advance(&c, 20 * SECOND_US), 0); /* new epoch */
}

/* ======================================================================
 * loop
 * ====================================================================== */

#define LOG_MAX 512

typedef enum { EV_SIM, EV_RENDER } event_kind_t;

typedef struct event {
    event_kind_t kind;
    uint64_t     tick;          /* sim: tick argument; render: in->tick */
    uint32_t     buttons;       /* sim only */
    uint64_t     handoff_tick;  /* sim: out->tick on entry */
    uint32_t     payload;       /* render: value read through in->data */
    uint64_t     polls_before;  /* headless_poll_count() on entry */
} event_t;

typedef struct recorder {
    event_t  log[LOG_MAX];
    uint32_t count;
    uint64_t stop_at_tick;   /* sim_tick returns false on this tick */
    bool     stop_on_quit;
    uint32_t payload;        /* storage the hand-off points at */
} recorder_t;

static recorder_t s_rec;

static bool rec_sim(void *ctx, uint64_t tick, const platform_input_t *input,
                    handoff_t *out)
{
    recorder_t *r = ctx;
    if (r->count < LOG_MAX) {
        event_t *e = &r->log[r->count++];
        e->kind = EV_SIM;
        e->tick = tick;
        e->buttons = input->buttons;
        e->handoff_tick = out->tick;
        e->polls_before = headless_poll_count();
    }
    r->payload = (uint32_t)tick * 10u + 7u;
    out->data = &r->payload;
    out->data_size = sizeof r->payload;

    if (r->stop_on_quit && (input->buttons & PLATFORM_BTN_QUIT)) {
        return false;
    }
    return tick != r->stop_at_tick;
}

static void rec_render(void *ctx, const handoff_t *in)
{
    recorder_t *r = ctx;
    if (r->count < LOG_MAX) {
        event_t *e = &r->log[r->count++];
        e->kind = EV_RENDER;
        e->tick = in->tick;
        e->payload = 0;
        if (in->data != NULL && in->data_size == sizeof(uint32_t)) {
            memcpy(&e->payload, in->data, sizeof e->payload);
        }
        e->polls_before = headless_poll_count();
    }
}

/* Fresh backend with a frozen clock at 0, fresh recorder, fresh loop. */
static void setup(loop_t *l, const headless_input_run_t *runs, uint32_t n)
{
    headless_reset();
    headless_set_time_step_us(0);
    REQUIRE(headless_set_input_script(runs, n));

    memset(&s_rec, 0, sizeof s_rec);
    s_rec.stop_at_tick = UINT64_MAX;

    loop_tasks_t tasks = {&s_rec, rec_sim, rec_render};
    loop_init(l, &tasks);
}

static const headless_input_run_t k_counting[] = {
    {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1},
};

static void init_calls_nothing(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    CHECK_EQ_U(s_rec.count, 0);
    CHECK_EQ_U(headless_poll_count(), 0);
    CHECK_EQ_U(loop_tick_count(&l), 0);
    /* The clock was not read: with a 1000 us step it would have moved. */
    headless_set_time_step_us(1000);
    loop_tasks_t tasks = {&s_rec, rec_sim, rec_render};
    loop_init(&l, &tasks);
    CHECK_EQ_U(platform_time_us(), 0);
}

static void step_with_no_tick_due_calls_nothing(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    CHECK(loop_step(&l)); /* first step only primes the clock */
    headless_advance_time_us(33333);
    CHECK(loop_step(&l)); /* one microsecond short of a tick */
    CHECK_EQ_U(s_rec.count, 0);
    CHECK_EQ_U(headless_poll_count(), 0);
    CHECK_EQ_U(loop_tick_count(&l), 0);
}

static void step_reads_the_clock_exactly_once(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    headless_set_time_step_us(100); /* each query moves time by 100 us */
    CHECK(loop_step(&l));
    CHECK(loop_step(&l));
    CHECK(loop_step(&l));
    headless_set_time_step_us(0);
    CHECK_EQ_U(platform_time_us(), 300);
}

static void one_due_tick_runs_sim_then_render(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    loop_step(&l);
    headless_advance_time_us(33334);
    CHECK(loop_step(&l));

    REQUIRE(s_rec.count == 2);
    CHECK(s_rec.log[0].kind == EV_SIM);
    CHECK_EQ_U(s_rec.log[0].tick, 0);
    CHECK_EQ_U(s_rec.log[0].buttons, 1);
    CHECK_EQ_U(s_rec.log[0].handoff_tick, 0);
    CHECK_EQ_U(s_rec.log[0].polls_before, 1); /* polled before the tick */

    CHECK(s_rec.log[1].kind == EV_RENDER);
    CHECK_EQ_U(s_rec.log[1].tick, 0);
    CHECK_EQ_U(s_rec.log[1].payload, 7); /* what tick 0 handed off */

    CHECK_EQ_U(headless_poll_count(), 1);
    CHECK_EQ_U(loop_tick_count(&l), 1);
}

static void several_due_ticks_run_in_order_then_one_render(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    loop_step(&l);
    headless_advance_time_us(100000); /* exactly three ticks */
    CHECK(loop_step(&l));

    REQUIRE(s_rec.count == 4);
    for (uint32_t i = 0; i < 3; i++) {
        CHECK(s_rec.log[i].kind == EV_SIM);
        CHECK_EQ_U(s_rec.log[i].tick, i);
        CHECK_EQ_U(s_rec.log[i].handoff_tick, i);
        /* one poll per tick, taken immediately before that tick */
        CHECK_EQ_U(s_rec.log[i].polls_before, i + 1);
        CHECK_EQ_U(s_rec.log[i].buttons, i + 1);
    }
    CHECK(s_rec.log[3].kind == EV_RENDER);
    CHECK_EQ_U(s_rec.log[3].tick, 2);       /* hand-off of the last tick */
    CHECK_EQ_U(s_rec.log[3].payload, 27);
    CHECK_EQ_U(headless_poll_count(), 3);
}

static void tick_numbers_continue_across_steps(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    loop_step(&l);
    headless_advance_time_us(33334);
    loop_step(&l);                    /* tick 0 */
    headless_advance_time_us(16667);
    loop_step(&l);                    /* nothing due */
    headless_advance_time_us(50000);
    loop_step(&l);                    /* ticks 1 and 2 (t = 100001) */

    REQUIRE(s_rec.count == 5);
    CHECK(s_rec.log[2].kind == EV_SIM);
    CHECK_EQ_U(s_rec.log[2].tick, 1);
    CHECK_EQ_U(s_rec.log[2].buttons, 2);
    CHECK(s_rec.log[3].kind == EV_SIM);
    CHECK_EQ_U(s_rec.log[3].tick, 2);
    CHECK_EQ_U(s_rec.log[3].buttons, 3);
    CHECK(s_rec.log[4].kind == EV_RENDER);
    CHECK_EQ_U(s_rec.log[4].tick, 2);
    CHECK_EQ_U(loop_tick_count(&l), 3);
}

static void dropped_ticks_consume_neither_numbers_nor_input(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    loop_step(&l);
    headless_advance_time_us(SECOND_US); /* 30 due, limit applies */
    CHECK(loop_step(&l));

    REQUIRE(s_rec.count == LOOP_MAX_CATCHUP_TICKS + 1);
    for (uint32_t i = 0; i < LOOP_MAX_CATCHUP_TICKS; i++) {
        CHECK_EQ_U(s_rec.log[i].tick, i);
        CHECK_EQ_U(s_rec.log[i].buttons, i + 1);
    }
    CHECK_EQ_U(headless_poll_count(), LOOP_MAX_CATCHUP_TICKS);

    /* The next tick carries on with the next number and script entry. */
    headless_advance_time_us(33334);
    CHECK(loop_step(&l));
    REQUIRE(s_rec.count == LOOP_MAX_CATCHUP_TICKS + 3);
    const event_t *e = &s_rec.log[LOOP_MAX_CATCHUP_TICKS + 1];
    CHECK(e->kind == EV_SIM);
    CHECK_EQ_U(e->tick, LOOP_MAX_CATCHUP_TICKS);
    CHECK_EQ_U(e->buttons, LOOP_MAX_CATCHUP_TICKS + 1);
}

static void stop_request_ends_the_step_but_still_renders(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    s_rec.stop_at_tick = 1;
    loop_step(&l);
    headless_advance_time_us(100000); /* three due; the second one stops */
    CHECK(!loop_step(&l));

    REQUIRE(s_rec.count == 3);
    CHECK_EQ_U(s_rec.log[0].tick, 0);
    CHECK_EQ_U(s_rec.log[1].tick, 1);
    CHECK(s_rec.log[2].kind == EV_RENDER);
    CHECK_EQ_U(s_rec.log[2].tick, 1);
    CHECK_EQ_U(headless_poll_count(), 2); /* third tick never polled */
    CHECK_EQ_U(loop_tick_count(&l), 2);
}

static void a_stopped_loop_does_nothing(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    s_rec.stop_at_tick = 0;
    loop_step(&l);
    headless_advance_time_us(33334);
    CHECK(!loop_step(&l));
    uint32_t events = s_rec.count;
    uint64_t polls = headless_poll_count();

    headless_set_time_step_us(1000);
    headless_advance_time_us(SECOND_US);
    uint64_t before = SECOND_US + 33334;
    CHECK(!loop_step(&l));
    CHECK(!loop_step(&l));
    CHECK_EQ_U(s_rec.count, events);
    CHECK_EQ_U(headless_poll_count(), polls);
    headless_set_time_step_us(0);
    CHECK_EQ_U(platform_time_us(), before); /* clock not even read */
}

static void run_returns_when_the_script_ends(void)
{
    loop_t l;
    setup(&l, k_counting, 8);
    s_rec.stop_on_quit = true;
    headless_set_time_step_us(1000); /* time moves as the loop polls it */
    loop_run(&l);

    /* Eight scripted ticks, then the tick that sees Quit. */
    CHECK_EQ_U(loop_tick_count(&l), 9);
    CHECK_EQ_U(headless_poll_count(), 9);

    uint32_t sims = 0;
    uint32_t renders = 0;
    for (uint32_t i = 0; i < s_rec.count; i++) {
        if (s_rec.log[i].kind == EV_SIM) {
            CHECK_EQ_U(s_rec.log[i].tick, sims);
            CHECK_EQ_U(s_rec.log[i].buttons,
                       sims < 8 ? sims + 1 : PLATFORM_BTN_QUIT);
            sims++;
        } else {
            renders++;
        }
    }
    CHECK_EQ_U(sims, 9);
    CHECK_EQ_U(renders, 9); /* 1 ms steps: never two ticks in one step */
    REQUIRE(s_rec.count != 0);
    CHECK(s_rec.log[s_rec.count - 1].kind == EV_RENDER);
    CHECK_EQ_U(s_rec.log[s_rec.count - 1].tick, 8);
}

/* The (tick, input) sequence seen by sim_tick, reduced to one number. */
static uint64_t sim_trace(uint64_t time_step_us)
{
    static const headless_input_run_t runs[] = {
        {0, 5}, {PLATFORM_BTN_A, 3}, {PLATFORM_BTN_LEFT, 40},
        {PLATFORM_BTN_A | PLATFORM_BTN_UP, 1}, {0, 20},
    };
    loop_t l;
    setup(&l, runs, 5);
    s_rec.stop_on_quit = true;
    headless_set_time_step_us(time_step_us);
    loop_run(&l);

    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < s_rec.count; i++) {
        if (s_rec.log[i].kind == EV_SIM) {
            h = (h ^ s_rec.log[i].tick) * 1099511628211ull;
            h = (h ^ s_rec.log[i].buttons) * 1099511628211ull;
        }
    }
    return h;
}

static void sim_sees_the_same_run_at_any_frame_rate(void)
{
    /* From 1000 clock polls per tick, through two ticks per step, to a
     * host so slow that most clock ticks are dropped: the simulation must
     * be handed the identical sequence of (tick, input). */
    uint64_t reference = sim_trace(1000);
    CHECK_EQ_U(sim_trace(33), reference);
    CHECK_EQ_U(sim_trace(7919), reference);
    CHECK_EQ_U(sim_trace(33334), reference);
    CHECK_EQ_U(sim_trace(66667), reference);
    CHECK_EQ_U(sim_trace(500000), reference);
}

TEST_MAIN(
    TEST(clock_first_advance_only_sets_the_epoch),
    TEST(clock_tick_boundary_is_exact),
    TEST(clock_same_time_twice_yields_nothing),
    TEST(clock_one_second_is_thirty_ticks_at_any_sampling),
    TEST(clock_does_not_drift_over_an_hour),
    TEST(clock_catch_up_is_limited_and_counts_drops),
    TEST(clock_keeps_tick_phase_after_a_drop),
    TEST(clock_ignores_time_running_backwards),
    TEST(clock_survives_a_year_long_gap),
    TEST(clock_init_clears_everything),
    TEST(init_calls_nothing),
    TEST(step_with_no_tick_due_calls_nothing),
    TEST(step_reads_the_clock_exactly_once),
    TEST(one_due_tick_runs_sim_then_render),
    TEST(several_due_ticks_run_in_order_then_one_render),
    TEST(tick_numbers_continue_across_steps),
    TEST(dropped_ticks_consume_neither_numbers_nor_input),
    TEST(stop_request_ends_the_step_but_still_renders),
    TEST(a_stopped_loop_does_nothing),
    TEST(run_returns_when_the_script_ends),
    TEST(sim_sees_the_same_run_at_any_frame_rate))
