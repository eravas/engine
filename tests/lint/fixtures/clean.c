/* Linter fixture: nothing here may be reported.
 * Words in comments are ignored: float double malloc free <stdio.h>. */
#include <stdint.h>
#include <string.h>

#include "kernel/platform.h"

// float in a line comment, and a continued one \
   double malloc
static const char *const text = "float \" double malloc(1) free";
static const char quote = '"'; /* a quote character must not open a string */
static const char escaped = '\'';

/* Identifiers that merely contain a banned word are fine. */
static int32_t freelist_head, floating, mallocs, double_buffer;
static int32_t ratio = 6 / 2; /* a division, not a comment */

#define STRINGIFY(x) #x /* '#' that is not at the start of a line */

/* None of these is a floating-point literal. */
struct pair { int32_t a, b; };
static struct pair origin = {.a = 0x1e5, .b = 0xE};
static int32_t member(const struct pair *p) { return p->a + origin.b; }
static void variadic(int32_t n, ...);
static const char *const version = "1.5e3";
