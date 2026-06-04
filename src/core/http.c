#include <stdio.h>
#include <string.h>
#include "http.h"

/* ---- request builder ---------------------------------------------------- */

int http_build_request(char *buf, int cap, const char *method, const char *host,
                       const char *path, const char *extra_hdrs,
                       const char *body, int body_len)
{
    int n;
    if (body)
        n = snprintf(buf, (size_t)cap,
                     "%s %s HTTP/1.1\r\nHost: %s\r\n"
                     "Content-Length: %d\r\n%s\r\n",
                     method, path, host, body_len,
                     extra_hdrs ? extra_hdrs : "");
    else
        n = snprintf(buf, (size_t)cap, "%s %s HTTP/1.1\r\nHost: %s\r\n%s\r\n",
                     method, path, host, extra_hdrs ? extra_hdrs : "");
    if (n < 0 || n >= cap) return -1;
    if (body) {
        if (n + body_len > cap) return -1;
        memcpy(buf + n, body, (size_t)body_len);
        n += body_len;
    }
    return n;
}

/* ---- response parser ---------------------------------------------------- */

#define HDR_LINE_CAP 512

static int ci_eq(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

/* tiny buffered reader over the callback */
typedef struct {
    http_read_fn rd;
    void *ctx;
    char buf[1024];
    int len, pos;
    int eof;
} br_t;

static int br_fill(br_t *b)
{
    int n;
    if (b->eof) return 0;
    n = b->rd(b->ctx, b->buf, (int)sizeof b->buf);
    if (n <= 0) {
        b->eof = 1;
        b->len = b->pos = 0;
        return n;
    }
    b->len = n;
    b->pos = 0;
    return n;
}

/* read one byte; returns 1, 0 EOF, -1 err */
static int br_getc(br_t *b, char *c)
{
    if (b->pos >= b->len) {
        int n = br_fill(b);
        if (n <= 0) return n;
    }
    *c = b->buf[b->pos++];
    return 1;
}

/* read a CRLF-terminated line into dst (no CRLF). cap includes NUL slot. */
static int br_line(br_t *b, char *dst, int cap)
{
    int n = 0;
    char c;
    for (;;) {
        int r = br_getc(b, &c);
        if (r <= 0) {
            dst[n] = 0;
            return (n > 0) ? n : r;
        }
        if (c == '\r') continue;
        if (c == '\n') break;
        if (n < cap - 1) dst[n++] = c;
    }
    dst[n] = 0;
    return n;
}

/* Copy body bytes into r (respecting cap; overflow -> truncated). want>=0:
 * exactly that many (short = -1). want<0: drain to EOF (returns 0). */
static int br_body(br_t *b, http_resp *r, long want)
{
    while (want != 0) {
        int avail, take, room;
        if (b->pos >= b->len && br_fill(b) <= 0) return (want < 0) ? 0 : -1;
        avail = b->len - b->pos;
        take = (want > 0 && avail > want) ? (int)want : avail;
        room = r->body_cap - 1 - r->body_len;
        if (room >= take) {
            memcpy(r->body + r->body_len, b->buf + b->pos, (size_t)take);
            r->body_len += take;
        } else {
            if (room > 0) {
                memcpy(r->body + r->body_len, b->buf + b->pos, (size_t)room);
                r->body_len += room;
            }
            r->truncated = 1;
        }
        b->pos += take;
        if (want > 0) want -= take;
    }
    return 0;
}

/* Saturating: clamp at 0x7FFFFFFF so a hostile/huge header can't drive the
 * accumulator through signed overflow. body_cap bounds the actual copy. */
#define LEN_SAT 0x7FFFFFFFL

static long parse_dec(const char *s)
{
    unsigned long v = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    for (; *s >= '0' && *s <= '9'; s++) {
        if (v > (LEN_SAT - 9) / 10) return LEN_SAT;
        v = v * 10 + (unsigned)(*s - '0');
    }
    return (long)v;
}

static long parse_hex(const char *s)
{
    unsigned long v = 0;
    for (;; s++) {
        unsigned d;
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (unsigned)(c - 'A' + 10);
        else
            return (long)v;
        if (v > (unsigned long)LEN_SAT >> 4) return LEN_SAT;
        v = (v << 4) | d;
    }
}

int http_read_response(http_read_fn rd, void *ctx, http_resp *r)
{
    br_t br;
    char line[HDR_LINE_CAP];
    const char *p;
    int n;
    long clen = -1;
    int chunked = 0;

    br.rd = rd;
    br.ctx = ctx;
    br.len = br.pos = 0;
    br.eof = 0;
    r->status = 0;
    r->body_len = 0;
    r->truncated = 0;
    r->conn_close = 0;
    if (r->body_cap > 0) r->body[0] = 0;

    /* status line: HTTP/1.1 200 OK */
    n = br_line(&br, line, (int)sizeof line);
    if (n <= 0) return -1;
    for (p = line; *p && *p != ' '; p++) {}
    if (*p == ' ') p++;
    r->status = (int)parse_dec(p);
    if (r->status == 0) return -1;

    /* headers */
    for (;;) {
        int k;
        n = br_line(&br, line, (int)sizeof line);
        if (n < 0) return -1;
        if (n == 0) break; /* blank line */
        for (k = 0; k < n && line[k] != ':'; k++)
            ;
        if (k >= n) continue;
        p = line + k + 1;
        while (*p == ' ' || *p == '\t')
            p++;
#define HDR_IS(s) (k == (int)sizeof(s) - 1 && ci_eq(line, s, k))
#define VAL_IS(s)                                                              \
    ((int)strlen(p) == (int)sizeof(s) - 1 && ci_eq(p, s, (int)sizeof(s) - 1))
        if (HDR_IS("content-length"))
            clen = parse_dec(p);
        else if (HDR_IS("transfer-encoding") && VAL_IS("chunked"))
            chunked = 1;
        else if (HDR_IS("connection") && VAL_IS("close"))
            r->conn_close = 1;
#undef HDR_IS
#undef VAL_IS
    }

    /* body */
    if (chunked) {
        for (;;) {
            long sz;
            n = br_line(&br, line, (int)sizeof line);
            /* n==0 here is premature EOF, not the terminal chunk -- without
             * this guard a dropped connection looks like a clean end and the
             * dead keep-alive socket gets reused. */
            if (n <= 0) return -1;
            sz = parse_hex(line);
            if (sz == 0) {
                /* consume trailers until blank line */
                while (br_line(&br, line, (int)sizeof line) > 0)
                    ;
                break;
            }
            if (br_body(&br, r, sz) != 0) return -1;
            if (br_line(&br, line, (int)sizeof line) < 0)
                return -1; /* CRLF after chunk */
        }
    } else if (clen >= 0) {
        if (br_body(&br, r, clen) != 0) return -1;
    } else {
        br_body(&br, r, -1); /* no framing -> read to EOF; conn is dead */
        r->conn_close = 1;
    }
    if (r->body_cap > 0) r->body[r->body_len] = 0;
    return 0;
}

/* ---- chunked request-body writer ------------------------------------- */

#define CW_RESV 8 /* room for hex length + CRLF before the data */

void http_cw_init(http_cw *w, http_write_fn wr, void *ctx)
{
    w->wr = wr;
    w->ctx = ctx;
    w->n = 0;
    w->err = 0;
    w->total = 0;
}

static int cw_flush(http_cw *w)
{
    int p = CW_RESV;
    unsigned v = (unsigned)w->n;
    if (w->err || w->n == 0) return w->err;
    w->buf[CW_RESV + w->n] = '\r';
    w->buf[CW_RESV + w->n + 1] = '\n';
    w->buf[--p] = '\n';
    w->buf[--p] = '\r';
    do {
        w->buf[--p] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v);
    if (w->wr(w->ctx, w->buf + p, CW_RESV - p + w->n + 2) < 0) w->err = -1;
    w->total += w->n;
    w->n = 0;
    return w->err;
}

int http_cw_put(void *ctx, const char *s, int len)
{
    http_cw *w = (http_cw *)ctx;
    if (w->err) return -1;
    while (len > 0) {
        int room = HTTP_CW_CAP - w->n;
        int take = len < room ? len : room;
        memcpy(w->buf + CW_RESV + w->n, s, (size_t)take);
        w->n += take;
        s += take;
        len -= take;
        if (w->n == HTTP_CW_CAP && cw_flush(w)) return -1;
    }
    return 0;
}

int http_cw_end(http_cw *w)
{
    cw_flush(w);
    if (!w->err && w->wr(w->ctx, "0\r\n\r\n", 5) < 0) w->err = -1;
    return w->err;
}
