#include "logview.h"
#include "textfb.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

enum { LINE_CAP = 256 };
static char g_line[LOG_LINES][LINE_CAP];
static char g_tag[LOG_LINES];
static char g_cont[LOG_LINES];       /* continuation row: same colour, no gutter glyph */
static int  g_head, g_n;
static int  g_cols = LINE_CAP - 1;   /* wrap width; set from layout */

void log_set_cols(int cols) {
    if (cols < 8) cols = 8;
    if (cols > LINE_CAP - 1) cols = LINE_CAP - 1;
    g_cols = cols;
}

static void push_row(char tag, int cont, const char *s, int n) {
    int i = g_head; g_head = (g_head + 1) % LOG_LINES;
    if (g_n < LOG_LINES) g_n++;
    g_tag[i] = tag; g_cont[i] = (char)cont;
    if (n > LINE_CAP - 1) n = LINE_CAP - 1;
    memcpy(g_line[i], s, n); g_line[i][n] = 0;
}

/* Hard-break on '\n', soft-wrap each segment at g_cols (prefer last space). */
void log_push(char tag, const char *s) {
    int first = 1;
    while (*s) {
        int seg = 0;
        while (s[seg] && s[seg] != '\n') seg++;
        const char *p = s;
        int rem = seg;
        if (rem == 0)                         /* blank line */
            push_row(tag, !first, "", 0);
        while (rem > 0) {
            int take = rem > g_cols ? g_cols : rem;
            if (rem > g_cols) {               /* soft wrap: back up to a space */
                int sp = take;
                while (sp > 0 && p[sp] != ' ') sp--;
                if (sp > 0) take = sp;
            }
            push_row(tag, !first, p, take);
            first = 0;
            p += take; rem -= take;
            while (rem > 0 && *p == ' ') { p++; rem--; }   /* eat wrap point */
        }
        first = 0;
        s += seg;
        if (*s == '\n') s++;
    }
    if (first) push_row(tag, 0, "", 0);       /* empty input -> one blank row */
}

void log_pushf(char tag, const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    log_push(tag, buf);
}

static uint32_t tag_fg(char t) {
    switch (t) {
        case '>': return 0x2b2b2b;   /* you */
        case '<': return 0x3a5a8a;   /* model */
        case ':': return 0x777777;   /* tool */
        default:  return 0x999999;
    }
}

void log_paint(struct fb *F, int x, int y, int w, int h) {
    (void)w;
    int rows = h / TEXT_CH;
    int show = g_n < rows ? g_n : rows;
    int start = (g_head - show + LOG_LINES) % LOG_LINES;
    for (int r = 0; r < show; r++) {
        int i = (start + r) % LOG_LINES;
        int yy = y + r * TEXT_CH;
        char pre[3] = { g_cont[i] ? ' ' : g_tag[i], ' ', 0 };
        text_draw(F, x, yy, pre, tag_fg(g_tag[i]), 0xf6f5f1);
        text_draw(F, x + 2 * TEXT_CW, yy, g_line[i], tag_fg(g_tag[i]), 0xf6f5f1);
    }
}
