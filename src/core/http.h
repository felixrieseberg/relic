/* Minimal HTTP/1.1 client: build POST, parse response.
 * No malloc, no platform headers. Transport is a caller callback.
 */
#ifndef CORE_HTTP_H
#define CORE_HTTP_H

#include <stddef.h>

/* Read callback: fill buf with up to len bytes from the wire.
 * Return >0 bytes, 0 = EOF, <0 = error. */
typedef int (*http_read_fn)(void *ctx, char *buf, int len);

typedef struct {
    int status; /* e.g. 200, 401; 0 if parse failed */
    char *body; /* caller-provided buffer */
    int body_cap;
    int body_len;   /* bytes written into body (NUL-terminated too) */
    int truncated;  /* 1 if real body exceeded body_cap */
    int conn_close; /* 1 if server sent Connection: close (or no framing) */
} http_resp;

/* Build a request into buf. Returns bytes written, or -1 on overflow.
 * extra_hdrs: optional "Name: value\r\n..." block (may be NULL/empty).
 * For GET, pass body=NULL/body_len=0 (no Content-Length emitted). */
int http_build_request(char *buf, int cap, const char *method, const char *host,
                       const char *path, const char *extra_hdrs,
                       const char *body, int body_len);

#define http_build_post(b, c, h, p, x, bd, bl)                                 \
    http_build_request((b), (c), "POST", (h), (p), (x), (bd), (bl))

/* Read and parse one HTTP/1.1 response from rd/ctx into *r.
 * Handles Content-Length and Transfer-Encoding: chunked.
 * r->body / r->body_cap must be set by caller. Returns 0 ok, -1 error. */
int http_read_response(http_read_fn rd, void *ctx, http_resp *r);

/* ---- chunked request-body writer ------------------------------------- */

/* Write callback: write all len bytes; return >=0 ok, <0 error. */
typedef int (*http_write_fn)(void *ctx, const char *buf, int len);

#define HTTP_CW_CAP 2048

/* Buffers data and emits Transfer-Encoding: chunked frames via wr(ctx,...).
 * After the last put, call http_cw_end() to flush + send the 0-chunk. */
typedef struct {
    http_write_fn wr;
    void *ctx;
    int n, err;
    long total;
    char buf[8 + HTTP_CW_CAP + 2];
} http_cw;

void http_cw_init(http_cw *w, http_write_fn wr, void *ctx);
/* Append len bytes. void* so it satisfies generic (void*,char*,int) sinks. */
int http_cw_put(void *w, const char *s, int len);
int http_cw_end(http_cw *w);

#endif
