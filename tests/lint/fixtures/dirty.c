/* Linter fixture: exactly 13 findings, one per marked line. */
#include <stdio.h>          /* 1: system header */
#include <SDL3/SDL.h>       /* 2: system header */
#  include  <stdlib.h>      /* 3: spacing does not hide it */
#include "platform/headless/headless.h" /* 4: not an engine header */
#include "pico/stdlib.h"    /* 5: not an engine header */

static float half = 1;      /* 6 */
static double whole = 2;    /* 7 */

static void leak(void)
{
    void *p = malloc(16);   /* 8 */
    p = realloc(p, 32);     /* 9 */
    free(p); /* 10 */ /* float: still a comment */
}

static int scaled = 3 * 1.5;   /* 11: literal with a point */
static int big = 1e3;          /* 12: literal with an exponent */
static int small = .5 * 4;     /* 13: literal starting with a point */
