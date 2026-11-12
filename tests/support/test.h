/*
 * test.h - minimal test runner. No dependencies beyond the C library.
 *
 * A test program is one or more functions `void name(void)` listed in a
 * table, plus TEST_MAIN:
 *
 *     static void adds(void) { CHECK(1 + 1 == 2); }
 *
 *     TEST_MAIN(TEST(adds))
 *
 * CHECK(cond)    records a failure and carries on with the test.
 * REQUIRE(cond)  records a failure and abandons the current test; use it
 *                when continuing would crash (e.g. a NULL pointer).
 * CHECK_EQ_U / CHECK_EQ_I compare integers and print both values.
 *
 * The program runs every test, prints one line per test and a summary, and
 * exits 0 only if no check failed. Arguments, if any, select tests by exact
 * name. `--list` prints the names.
 */
#ifndef TESTS_SUPPORT_TEST_H
#define TESTS_SUPPORT_TEST_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct test_case {
    const char *name;
    void (*fn)(void);
} test_case_t;

int  test_run_all(const test_case_t *cases, int count, int argc, char **argv);
void test_fail(const char *file, int line, const char *expr);
void test_fail_u(const char *file, int line, const char *expr,
                 unsigned long long got, unsigned long long want);
void test_fail_i(const char *file, int line, const char *expr, long long got,
                 long long want);

extern jmp_buf test_abort_jmp;

#define TEST(fn_name) {#fn_name, fn_name}

#define TEST_MAIN(...)                                                   \
    int main(int argc, char **argv)                                      \
    {                                                                    \
        static const test_case_t cases[] = {__VA_ARGS__};                \
        return test_run_all(cases, (int)(sizeof cases / sizeof cases[0]), \
                            argc, argv);                                 \
    }

/*
 * For test programs that run through a platform backend's own main():
 * defines app_main() instead of main(). No test selection by argument.
 */
#define TEST_APP_MAIN(...)                                                \
    int app_main(void)                                                    \
    {                                                                     \
        static const test_case_t cases[] = {__VA_ARGS__};                 \
        char  name[] = "test";                                            \
        char *argv[] = {name, NULL};                                      \
        return test_run_all(cases, (int)(sizeof cases / sizeof cases[0]), \
                            1, argv);                                     \
    }

#define CHECK(cond)                                  \
    do {                                             \
        if (!(cond)) {                               \
            test_fail(__FILE__, __LINE__, #cond);    \
        }                                            \
    } while (0)

#define REQUIRE(cond)                                \
    do {                                             \
        if (!(cond)) {                               \
            test_fail(__FILE__, __LINE__, #cond);    \
            longjmp(test_abort_jmp, 1);              \
        }                                            \
    } while (0)

#define CHECK_EQ_U(got, want)                                          \
    do {                                                               \
        unsigned long long test_g_ = (unsigned long long)(got);        \
        unsigned long long test_w_ = (unsigned long long)(want);       \
        if (test_g_ != test_w_) {                                      \
            test_fail_u(__FILE__, __LINE__, #got " == " #want, test_g_, \
                        test_w_);                                      \
        }                                                              \
    } while (0)

#define CHECK_EQ_I(got, want)                                          \
    do {                                                               \
        long long test_g_ = (long long)(got);                          \
        long long test_w_ = (long long)(want);                         \
        if (test_g_ != test_w_) {                                      \
            test_fail_i(__FILE__, __LINE__, #got " == " #want, test_g_, \
                        test_w_);                                      \
        }                                                              \
    } while (0)

#endif /* TESTS_SUPPORT_TEST_H */
