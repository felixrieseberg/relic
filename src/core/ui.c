#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ui.h"
#include "util.h"
#include "../plat/plat.h"

int g_verbose = 0;

static void err_stderr(const char *line)
{
    fputs(line, stderr);
    fputc('\n', stderr);
}
static void (*g_err_sink)(const char *) = err_stderr;

void errf_set_sink(void (*s)(const char *)) { g_err_sink = s ? s : err_stderr; }

void errf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    /* errf() carries network bytes (proxy replies, unparseable response
     * bodies) as well as internal messages; this is the choke point. */
    tty_sanitize(buf, (int)strlen(buf));
    g_err_sink(buf);
}

void vtrace(const char *fmt, ...)
{
    va_list ap;
    if (!g_verbose) return;
    fputs(". ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

void vdump(const char *label, const char *buf, int len)
{
    int i, col;
    if (g_verbose < 2) return;
    fprintf(stderr, ". %s (%d bytes):\n.   ", label, len);
    for (i = 0, col = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\r') continue;
        if (c == '\n' || col >= 76) {
            fputs("\n.   ", stderr);
            col = 0;
            if (c == '\n') continue;
        }
        fputc((c >= 32 && c < 127) ? c : '.', stderr);
        col++;
    }
    fputc('\n', stderr);
    fflush(stderr);
}

static unsigned g_spin_i;
static char g_spin_lbl[16];
static int g_spin_w; /* visual columns last drawn, for blank-out */
static unsigned long g_spin_t0;

static void sink_stderr(const char *label, unsigned secs)
{
    if (secs)
        g_spin_w = fprintf(stderr, "\r  %s %s... (%us)   ",
                           plat_spinner(g_spin_i), label, secs);
    else
        g_spin_w =
            fprintf(stderr, "\r  %s %s...   ", plat_spinner(g_spin_i), label);
    fflush(stderr);
}

static void (*g_spin_sink)(const char *, unsigned) = sink_stderr;

void spin_set_sink(void (*s)(const char *, unsigned))
{
    g_spin_sink = s ? s : sink_stderr;
    g_spin_w = 0;
}

void spin(const char *label)
{
    if (g_verbose) return;
    if (label) str_set(g_spin_lbl, (int)sizeof g_spin_lbl, label);
    if (!g_spin_t0) g_spin_t0 = plat_time_unix();
    g_spin_i++;
    g_spin_sink(g_spin_lbl, (unsigned)(plat_time_unix() - g_spin_t0));
}

void spin_clear(void)
{
    g_spin_t0 = 0;
    g_spin_lbl[0] = 0;
    if (!g_spin_w) return;
    fprintf(stderr, "\r%*s\r", g_spin_w, "");
    fflush(stderr);
    g_spin_w = 0;
}
