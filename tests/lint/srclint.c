/*
 * srclint.c - enforces the engine-code rules that the compiler cannot.
 *
 *   srclint FILE...
 *
 * For each file (C source or header belonging to portable engine code):
 *   - no `float` or `double`, and no floating-point literals (1.5, 1e3);
 *   - no malloc, calloc, realloc or free;
 *   - `#include <...>` only from a short list of freestanding headers plus
 *     <string.h>, which keeps SDL, the OS, stdio and chip SDKs out;
 *   - `#include "..."` only of kernel/, sim/ or game/ headers.
 *
 * Comments, string literals and character literals are ignored, so prose
 * may mention any of these words. Exit status is 1 if anything is found.
 *
 * This is a host tool, so it may use stdio.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *const k_banned_words[] = {
    "float", "double", "malloc", "calloc", "realloc", "free",
};

static const char *const k_system_headers[] = {
    "stdalign.h", "stdbool.h", "stddef.h", "stdint.h",
    "stdnoreturn.h", "limits.h", "string.h",
};

static const char *const k_local_prefixes[] = {"kernel/", "sim/", "game/"};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int s_findings;

static void report(const char *file, int line, const char *what,
                   const char *detail)
{
    printf("%s:%d: %s '%s'\n", file, line, what, detail);
    s_findings++;
}

static void check_word(const char *file, int line, const char *word)
{
    /* A decimal number with an exponent, such as 1e3. Hex is exempt. */
    if (isdigit((unsigned char)word[0]) &&
        !(word[0] == '0' && (word[1] == 'x' || word[1] == 'X')) &&
        strpbrk(word, "eE") != NULL) {
        report(file, line, "floating-point literal in engine code:", word);
        return;
    }
    for (size_t i = 0; i < COUNT(k_banned_words); i++) {
        if (strcmp(word, k_banned_words[i]) == 0) {
            report(file, line, "forbidden in engine code:", word);
        }
    }
}

static void check_include(const char *file, int line, char open,
                          const char *name)
{
    if (open == '<') {
        for (size_t i = 0; i < COUNT(k_system_headers); i++) {
            if (strcmp(name, k_system_headers[i]) == 0) {
                return;
            }
        }
        report(file, line, "system header not allowed in engine code:", name);
    } else {
        for (size_t i = 0; i < COUNT(k_local_prefixes); i++) {
            size_t n = strlen(k_local_prefixes[i]);
            if (strncmp(name, k_local_prefixes[i], n) == 0) {
                return;
            }
        }
        report(file, line, "engine code may only include engine headers:",
               name);
    }
}

/*
 * A small lexer: enough C to tell code from comments and literals, to pick
 * out identifiers, and to recognise #include lines.
 */
static void scan(const char *file, FILE *f)
{
    enum { CODE, LINE_COMMENT, BLOCK_COMMENT, STRING, CHARLIT } state = CODE;
    char word[64];
    size_t word_len = 0;
    bool word_too_long = false;
    bool line_start = true;      /* only whitespace seen on this line */
    bool directive = false;      /* just saw '#' at line start */
    bool include_pending = false; /* saw `#include`, expecting < or " */
    int line = 1;
    int c;
    int prev = 0;

    while ((c = fgetc(f)) != EOF) {
        switch (state) {
        case LINE_COMMENT:
            if (c == '\n' && prev != '\\') {
                state = CODE;
            }
            break;

        case BLOCK_COMMENT:
            if (c == '/' && prev == '*') {
                state = CODE;
                c = ' '; /* so that the '/' cannot start another comment */
            }
            break;

        case STRING:
        case CHARLIT:
            if (c == '\\') {
                int next = fgetc(f); /* skip the escaped character */
                if (next == '\n') {
                    line++;
                }
            } else if ((state == STRING && c == '"') ||
                       (state == CHARLIT && c == '\'') || c == '\n') {
                state = CODE;
            }
            break;

        case CODE:
            if (isalnum(c) || c == '_') {
                if (word_len + 1 < sizeof word) {
                    word[word_len++] = (char)c;
                } else {
                    word_too_long = true;
                }
                break;
            }
            /* A word has just ended. */
            if (c == '.') {
                /* "1." and "1.5" (number then dot) or ".5" (dot then digit)
                 * are floating literals; "a.b" and "..." are not. */
                int next = fgetc(f);
                if (next != EOF) {
                    ungetc(next, f);
                }
                bool after_number =
                    word_len != 0 && isdigit((unsigned char)word[0]);
                if (after_number || (word_len == 0 && isdigit(next))) {
                    report(file, line, "floating-point literal in engine code:",
                           after_number ? "<number>." : ".<digit>");
                    word_len = 0; /* the number has been reported once */
                }
            }
            if (word_len != 0) {
                word[word_len] = '\0';
                if (directive) {
                    include_pending = strcmp(word, "include") == 0;
                    directive = false;
                } else if (!word_too_long) {
                    check_word(file, line, word);
                }
                word_len = 0;
                word_too_long = false;
                line_start = false;
            }

            if (include_pending && (c == '<' || c == '"')) {
                char name[128];
                size_t n = 0;
                char close = c == '<' ? '>' : '"';
                int d;
                while ((d = fgetc(f)) != EOF && d != close && d != '\n') {
                    if (n + 1 < sizeof name) {
                        name[n++] = (char)d;
                    }
                }
                name[n] = '\0';
                check_include(file, line, (char)c, name);
                include_pending = false;
                if (d == '\n') {
                    line++;
                    line_start = true;
                }
                c = ' ';
                break;
            }

            if (c == '/' && prev == '/') {
                state = LINE_COMMENT;
            } else if (c == '*' && prev == '/') {
                state = BLOCK_COMMENT;
                c = ' '; /* so that "/" "*" "/" is not read as closed */
            } else if (c == '"') {
                state = STRING;
                include_pending = false;
            } else if (c == '\'') {
                state = CHARLIT;
            } else if (c == '#' && line_start) {
                directive = true;
            } else if (!isspace(c)) {
                if (c != '/') {
                    include_pending = false;
                }
                line_start = false;
                directive = false;
            }
            break;
        }

        if (c == '\n') {
            line++;
            line_start = true;
            directive = false;
            include_pending = false;
        }
        prev = c;
    }

    if (word_len != 0 && !word_too_long && !directive) {
        word[word_len] = '\0';
        check_word(file, line, word);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s FILE...\n", argv[0]);
        return 2;
    }
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (f == NULL) {
            fprintf(stderr, "srclint: cannot open '%s'\n", argv[i]);
            return 2;
        }
        scan(argv[i], f);
        fclose(f);
    }
    if (s_findings != 0) {
        printf("srclint: %d finding(s)\n", s_findings);
        return 1;
    }
    printf("srclint: %d file(s) clean\n", argc - 1);
    return 0;
}
