#include "tests/support/test.h"

#include <stdio.h>
#include <string.h>

jmp_buf test_abort_jmp;

static int s_failures_in_test;

void test_fail(const char *file, int line, const char *expr)
{
    s_failures_in_test++;
    printf("    %s:%d: CHECK failed: %s\n", file, line, expr);
}

void test_fail_u(const char *file, int line, const char *expr,
                 unsigned long long got, unsigned long long want)
{
    s_failures_in_test++;
    printf("    %s:%d: CHECK failed: %s (got %llu, want %llu)\n", file, line,
           expr, got, want);
}

void test_fail_i(const char *file, int line, const char *expr, long long got,
                 long long want)
{
    s_failures_in_test++;
    printf("    %s:%d: CHECK failed: %s (got %lld, want %lld)\n", file, line,
           expr, got, want);
}

static bool selected(const char *name, int argc, char **argv)
{
    if (argc <= 1) {
        return true;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

int test_run_all(const test_case_t *cases, int count, int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (int i = 0; i < count; i++) {
            printf("%s\n", cases[i].name);
        }
        return 0;
    }

    int ran = 0;
    int failed = 0;
    for (int i = 0; i < count; i++) {
        if (!selected(cases[i].name, argc, argv)) {
            continue;
        }
        ran++;
        s_failures_in_test = 0;
        if (setjmp(test_abort_jmp) == 0) {
            cases[i].fn();
        }
        if (s_failures_in_test != 0) {
            failed++;
            printf("FAIL  %s\n", cases[i].name);
        } else {
            printf("ok    %s\n", cases[i].name);
        }
        fflush(stdout);
    }

    printf("%d run, %d failed\n", ran, failed);
    if (ran == 0) {
        printf("no test matched\n");
        return 1;
    }
    return failed != 0 ? 1 : 0;
}
