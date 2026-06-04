#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "scroll.h"
#include "util.h"
#include "../plat/plat.h"

#ifndef SCROLL_CAP
#define SCROLL_CAP 512
#endif
#ifndef SCROLL_COLS
#define SCROLL_COLS 80
#endif
#if SCROLL_COLS > 255
#error "SCROLL_COLS > 255 overflows g_len[] (unsigned char)"
#endif

/* Default storage is static (no-malloc baseline). scroll_init() can point
 * g_ring/g_len at a single heap block when the user configures more lines. */
static char g_ring0[SCROLL_CAP * SCROLL_COLS];
static unsigned char g_len0[SCROLL_CAP];
static char *g_ring = g_ring0;
static unsigned char *g_len = g_len0;
static int g_cap = SCROLL_CAP;
static int g_head; /* index of oldest line */
static int g_cnt;  /* lines stored, <= g_cap */
static int g_cur;  /* length of the in-progress (not yet newline'd) line */

int scroll_lines(void) { return g_cnt + (g_cur ? 1 : 0); }
int scroll_capacity(void) { return g_cap; }

void scroll_init(int lines)
{
    if (lines < 16) lines = 16;
    if (lines > 32767) lines = 32767;
    g_head = g_cnt = g_cur = 0;
    if (lines <= SCROLL_CAP) {
        g_ring = g_ring0;
        g_len = g_len0;
        g_cap = lines;
        return;
    }
    g_ring = malloc((size_t)lines * (SCROLL_COLS + 1));
    if (!g_ring) {
        g_ring = g_ring0;
        g_len = g_len0;
        g_cap = SCROLL_CAP;
        return;
    }
    g_len = (unsigned char *)(g_ring + (size_t)lines * SCROLL_COLS);
    g_cap = lines;
}

static int idx(int i) { return (g_head + i) % g_cap; }
static char *row(int i) { return g_ring + (size_t)i * SCROLL_COLS; }

/* Row currently being written. When full, the "next" slot is g_head -- which
 * push_line then advances past, so we write into the slot about to recycle. */
static char *cur_row(void) { return row(idx(g_cnt < g_cap ? g_cnt : 0)); }

static void push_line(void)
{
    int w;
    if (g_cnt < g_cap) {
        w = idx(g_cnt);
        g_cnt++;
    } else {
        w = g_head;
        g_head = (g_head + 1) % g_cap;
    }
    g_len[w] = (unsigned char)g_cur;
    g_cur = 0;
}

void scroll_capture(const char *s, int len)
{
    char *p = cur_row();
    int i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\r') continue;
        if (c == '\n' || g_cur >= SCROLL_COLS) {
            char carry[3];
            int nc = 0;
            if (c != '\n') {
                /* Don't split a UTF-8 sequence across rows: if the row tail
                 * is an incomplete lead+continuations, carry it forward. */
                int k = g_cur;
                while (nc < 3 && k > 0
                       && ((unsigned char)p[k - 1] & 0xC0) == 0x80) {
                    k--;
                    nc++;
                }
                if (k > 0 && nc < 3
                    && ((unsigned char)p[k - 1] & 0xC0) == 0xC0) {
                    nc++;
                    memcpy(carry, p + k - 1, (size_t)nc);
                    g_cur = k - 1;
                } else
                    nc = 0;
            }
            push_line();
            p = cur_row();
            if (nc) {
                memcpy(p, carry, (size_t)nc);
                g_cur = nc;
            }
            if (c == '\n') continue;
        }
        p[g_cur++] = c;
    }
}

static void sink_stdout(const char *s, int len)
{
    fwrite(s, 1, (size_t)len, stdout);
}
static void (*g_sink)(const char *, int) = sink_stdout;

void scroll_set_sink(void (*w)(const char *, int))
{
    g_sink = w ? w : sink_stdout;
}

void scroll_out(const char *s, int len)
{
    /* Everything that reaches the terminal via this path may originate from
     * the model or from tool output. Strip C0 control bytes (ESC, CSI, OSC
     * introducers) so that text can't reposition the cursor, retitle the
     * window, or repaint the permission prompt. plat_con_attr() is the only
     * sanctioned way to style output. */
    char buf[256];
    while (len > 0) {
        int n = len < (int)sizeof buf ? len : (int)sizeof buf;
        memcpy(buf, s, (size_t)n);
        tty_sanitize(buf, n);
        g_sink(buf, n);
        scroll_capture(buf, n);
        s += n;
        len -= n;
    }
}

void scroll_outz(const char *s) { scroll_out(s, (int)strlen(s)); }

void scroll_printf(const char *fmt, ...)
{
    char buf[512];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    scroll_out(buf, n);
}

/* Map a viewer key to a new top-line. Returns -1 to quit. */
static int nav(int k, int top, int page, int total)
{
    if (k == 0 || k == 'q' || k == 'Q' || k == 27 || k == '\r' || k == '\n')
        return -1;
    switch (k) {
    case PLAT_KEY_UP:
    case 'k': top--; break;
    case PLAT_KEY_DOWN:
    case 'j': top++; break;
    case PLAT_KEY_PGUP:
    case 'b': top -= page; break;
    case PLAT_KEY_PGDN:
    case ' ': top += page; break;
    case PLAT_KEY_HOME:
    case 'g': top = 0; break;
    case PLAT_KEY_END:
    case 'G': top = total; break; /* clamped below */
    default: break;
    }
    if (top + page > total) top = total - page;
    if (top < 0) top = 0;
    return top;
}

static void draw(int top, int rows, int cols)
{
    int r, total = g_cnt;
    plat_con_clear();
    for (r = 0; r < rows - 1; r++) {
        int li = top + r;
        if (li >= 0 && li < total) {
            int w = idx(li);
            int n = g_len[w];
            if (n > cols) {
                n = cols;
                /* Don't emit a torn UTF-8 sequence at the right edge. */
                while (n > 0 && ((unsigned char)row(w)[n] & 0xC0) == 0x80)
                    n--;
            }
            fwrite(row(w), 1, (size_t)n, stdout);
        }
        fputc('\n', stdout);
    }
    printf("-- %d-%d/%d -- Up/Dn PgUp/PgDn Home/End  q ",
           top + 1 < 1 ? 1 : top + 1,
           top + rows - 1 > total ? total : top + rows - 1, total);
    fflush(stdout);
}

int scroll_view(void)
{
    int rows = 25, cols = 80, page, top, total = g_cnt;
    if (!plat_con_size(&rows, &cols) || !plat_con_raw(1)) return 0;
    if (rows < 4) rows = 25;
    if (cols < 1) cols = 80;
    page = rows - 1;
    top = total - page;
    if (top < 0) top = 0;
    do {
        draw(top, rows, cols);
    } while ((top = nav(plat_getkey(), top, page, total)) >= 0);
    /* Restore: redraw the tail so it looks like we never left. */
    top = total - page;
    if (top < 0) top = 0;
    draw(top, rows, cols);
    fputc('\n', stdout);
    plat_con_raw(0);
    return 1;
}

/* --- one-shot pager over an external buffer -------------------------- */
#define PAGER_MAXL 1024
static int g_pl[PAGER_MAXL + 1];

static void pager_draw(const char *buf, int top, int nl, int rows, int cols)
{
    int r;
    plat_con_clear();
    for (r = 0; r < rows - 1; r++) {
        int li = top + r;
        if (li < nl) {
            int s = g_pl[li], e = g_pl[li + 1];
            while (e > s && (buf[e - 1] == '\n' || buf[e - 1] == '\r'))
                e--;
            if (e - s > cols) e = s + cols;
            fwrite(buf + s, 1, (size_t)(e - s), stdout);
        }
        fputc('\n', stdout);
    }
    printf("-- %d-%d/%d -- j/k Space/b g/G  q ", top + 1,
           top + rows - 1 > nl ? nl : top + rows - 1, nl);
    fflush(stdout);
}

int scroll_pager(const char *buf, int len)
{
    int rows = 25, cols = 80, page, top = 0, nl = 0, i, col = 0, tail;
    if (!plat_con_size(&rows, &cols) || !plat_con_raw(1)) return 0;
    if (rows < 4) rows = 25;
    if (cols < 1) cols = 80;
    g_pl[0] = 0;
    for (i = 0; i < len && nl < PAGER_MAXL - 1; i++) {
        if (buf[i] == '\n') {
            g_pl[++nl] = i + 1;
            col = 0;
        } else if (buf[i] != '\r') {
            if (++col >= cols) {
                g_pl[++nl] = i + 1;
                col = 0;
            }
        }
    }
    g_pl[++nl] = len;
    page = rows - 1;
    do {
        pager_draw(buf, top, nl, rows, cols);
    } while ((top = nav(plat_getkey(), top, page, nl)) >= 0);
    plat_con_raw(0);
    tail = g_cnt - page;
    if (tail < 0) tail = 0;
    draw(tail, rows, cols);
    fputc('\n', stdout);
    return 1;
}
