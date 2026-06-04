/* Tiny C89 test harness. One header, no malloc, no deps.
 *
 *   #include "t.h"
 *   int main(void) {
 *       t_group("util");
 *       t_int(sb_udec(...), 0,   "sb_udec: zero");
 *       t_str(buf, "0",          "sb_udec: zero -> \"0\"");
 *       t_ok(off == 1,           "sb_udec: advances offset");
 *       return t_done();
 *   }
 *
 * Output (one line per check, green/red on a tty):
 *
 *   -- util ------------------------------------------------
 *   ok   util   sb_udec: zero
 *   FAIL util   sb_udec: zero -> "0"          (test_util.c:12)
 *          got  = "00"
 *          want = "0"
 *   -- 1 ok, 1 FAILED --
 *
 * Checks continue past failures; t_done() returns 1 if any failed. */
#ifndef TEST_T_H
#define TEST_T_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Colour only when stdout is a tty; fall back to "off" on platforms without
 * isatty so the harness stays freestanding-friendly. */
#if defined(_WIN32)
#include <io.h>
#define t__isatty(fd) _isatty(fd)
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#define t__isatty(fd) isatty(fd)
#else
#define t__isatty(fd) 0
#endif

static int t__pass, t__fail, t__tap = -1, t__color = -1;
static const char *t__suite = "";

static int t__is_tap(void)
{
    if (t__tap < 0) t__tap = getenv("T_TAP") != NULL;
    return t__tap;
}
static const char *t__c(const char *code)
{
    if (t__color < 0) t__color = !t__is_tap() && t__isatty(1);
    return t__color ? code : "";
}
#define T__GRN t__c("\033[32m")
#define T__RED t__c("\033[31;1m")
#define T__DIM t__c("\033[2m")
#define T__RST t__c("\033[0m")

static void t__ok(const char *name)
{
    t__pass++;
    if (t__is_tap())
        printf("ok %d - %s: %s\n", t__pass + t__fail, t__suite, name);
    else
        printf("%sok%s   %-6s %s\n", T__GRN, T__RST, t__suite, name);
}
static void t__bad(const char *name, const char *file, int line)
{
    t__fail++;
    if (t__is_tap()) {
        printf("not ok %d - %s: %s\n", t__pass + t__fail, t__suite, name);
        printf("#   at %s:%d\n", file, line);
        return;
    }
    printf("%sFAIL%s %-6s %s   %s(%s:%d)%s\n", T__RED, T__RST, t__suite, name,
           T__DIM, file, line, T__RST);
}
#define T__DIAG (t__is_tap() ? "#  " : "       ")
/* Print a string with control chars rendered \n \t \xNN, capped. */
static void t__qstr(const char *s)
{
    int i;
    if (!s) {
        printf("(null)");
        return;
    }
    putchar('"');
    for (i = 0; s[i] && i < 200; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n')
            printf("\\n");
        else if (c == '\t')
            printf("\\t");
        else if (c == '\r')
            printf("\\r");
        else if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c < 32 || c >= 127)
            printf("\\x%02x", c);
        else
            putchar(c);
    }
    putchar('"');
    if (s[i]) printf("...");
}

static void t_group(const char *name)
{
    int d = 54 - (int)strlen(name);
    t__suite = name;
    if (t__is_tap()) {
        printf("# %s\n", name);
        return;
    }
    printf("%s-- %s %.*s%s\n", T__DIM, name, d < 0 ? 0 : d,
           "------------------------------------------------------", T__RST);
}

#define t_ok(cond, name)                                                       \
    do {                                                                       \
        if (cond)                                                              \
            t__ok(name);                                                       \
        else                                                                   \
            t__bad(name, __FILE__, __LINE__);                                  \
    } while (0)

#define t_int(got, want, name)                                                 \
    do {                                                                       \
        long t__g = (long)(got), t__w = (long)(want);                          \
        if (t__g == t__w)                                                      \
            t__ok(name);                                                       \
        else {                                                                 \
            t__bad(name, __FILE__, __LINE__);                                  \
            printf("%sgot  = %ld\n%swant = %ld\n", T__DIAG, t__g, T__DIAG,     \
                   t__w);                                                      \
        }                                                                      \
    } while (0)

#define t_str(got, want, name)                                                 \
    do {                                                                       \
        const char *t__g = (got), *t__w = (want);                              \
        if (t__g && t__w && strcmp(t__g, t__w) == 0)                           \
            t__ok(name);                                                       \
        else {                                                                 \
            t__bad(name, __FILE__, __LINE__);                                  \
            printf("%sgot  = ", T__DIAG);                                      \
            t__qstr(t__g);                                                     \
            printf("\n%swant = ", T__DIAG);                                    \
            t__qstr(t__w);                                                     \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define t_has(hay, needle, name)                                               \
    do {                                                                       \
        const char *t__h = (hay), *t__n = (needle);                            \
        if (t__h && strstr(t__h, t__n))                                        \
            t__ok(name);                                                       \
        else {                                                                 \
            t__bad(name, __FILE__, __LINE__);                                  \
            printf("%sneedle = ", T__DIAG);                                    \
            t__qstr(t__n);                                                     \
            printf("\n%sin     = ", T__DIAG);                                  \
            t__qstr(t__h);                                                     \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define t_mem(got, want, len, name)                                            \
    do {                                                                       \
        if (memcmp((got), (want), (size_t)(len)) == 0)                         \
            t__ok(name);                                                       \
        else {                                                                 \
            t__bad(name, __FILE__, __LINE__);                                  \
            printf("%s(%d-byte memcmp differs)\n", T__DIAG, (int)(len));       \
        }                                                                      \
    } while (0)

/* Shared memory-sink for callback writers (http_cw, conv_send_request,
 * json_escape_to). All of those treat <0 as error, >=0 as ok. */
typedef struct {
    char *p;
    int n, cap;
} t_buf;
#define t_buf_init(b, a) ((b).p = (a), (b).cap = (int)sizeof(a), (b).n = 0)
static int t_buf_wr(void *ctx, const char *s, int len)
{
    t_buf *b = (t_buf *)ctx;
    if (b->n + len > b->cap) return -1;
    memcpy(b->p + b->n, s, (size_t)len);
    b->n += len;
    return 0;
}

static int t_done(void)
{
    /* Reference helpers so -Wunused-function stays quiet even if a test
     * file doesn't use every check macro. */
    (void)t__qstr;
    (void)t_buf_wr;
    (void)t_group;
    if (t__is_tap()) {
        printf("1..%d\n", t__pass + t__fail);
        return t__fail ? 1 : 0;
    }
    if (t__fail)
        printf("%s-- %d ok, %d FAILED --%s\n", T__RED, t__pass, t__fail,
               T__RST);
    else
        printf("%s-- %d ok --%s\n", T__GRN, t__pass, T__RST);
    return t__fail ? 1 : 0;
}

#endif
