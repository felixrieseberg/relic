#include "t.h"
#include "../src/core/http.h"

typedef struct {
    const char *p;
    int n, i, step;
} memrd_t;
static int memrd(void *ctx, char *buf, int len)
{
    memrd_t *m = (memrd_t *)ctx;
    int avail = m->n - m->i;
    int take;
    if (avail <= 0) return 0;
    take = (m->step < len) ? m->step : len;
    if (take > avail) take = avail;
    memcpy(buf, m->p + m->i, (size_t)take);
    m->i += take;
    return take;
}

static int load(const char *path, char *buf, int cap)
{
    FILE *f = fopen(path, "rb");
    int n;
    t_ok(f != 0, path);
    if (!f) return 0;
    n = (int)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

static char raw[8192], body[8192];

/* Encode src via http_cw, prepend a minimal chunked-response header, decode
 * via http_read_response, assert byte-for-byte round-trip. */
static void cw_roundtrip(const char *label, const char *src, int slen, int step)
{
    static char wire[8192];
    t_buf mw;
    http_cw cw;
    http_resp r;
    memrd_t mr;
    int i, hlen, ok = 1;
    const char *hdr = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";

    hlen = (int)strlen(hdr);
    memcpy(wire, hdr, (size_t)hlen);
    mw.p = wire + hlen;
    mw.n = 0;
    mw.cap = (int)sizeof wire - hlen;
    http_cw_init(&cw, t_buf_wr, &mw);
    for (i = 0; i < slen; i += step)
        if (http_cw_put(&cw, src + i, (slen - i < step) ? slen - i : step) != 0)
            ok = 0;
    if (http_cw_end(&cw) != 0) ok = 0;
    t_ok(ok, "cw_roundtrip: encode ok");
    t_int(cw.total, slen, "cw_roundtrip: total bytes");

    mr.p = wire;
    mr.n = hlen + mw.n;
    mr.i = 0;
    mr.step = 999;
    r.body = body;
    r.body_cap = (int)sizeof body;
    t_int(http_read_response(memrd, &mr, &r), 0, "cw_roundtrip: decode ok");
    t_int(r.status, 200, "cw_roundtrip: status 200");
    t_int(r.body_len, slen, "cw_roundtrip: body_len");
    t_mem(body, src, slen, label);
}

static void run_fixture(const char *path, int expect_status, const char *grep)
{
    http_resp r;
    memrd_t m;
    int n = load(path, raw, (int)sizeof raw);
    int step;
    for (step = 1; step <= 7; step += 6) { /* 1 byte and 7 bytes at a time */
        m.p = raw;
        m.n = n;
        m.i = 0;
        m.step = step;
        r.body = body;
        r.body_cap = (int)sizeof body;
        t_int(http_read_response(memrd, &m, &r), 0, "read_response: parse");
        t_int(r.status, expect_status, "read_response: status");
        t_int(r.conn_close, 0, "read_response: conn_close 0");
        t_has(body, grep, path);
    }
}

int main(void)
{
    char req[512];
    int n;

    t_group("http");

    run_fixture("test/fixtures/resp_200_cl.txt", 200, "\"content\"");
    run_fixture("test/fixtures/resp_401.txt", 401, "authentication_error");
    run_fixture("test/fixtures/resp_200_chunked.txt", 200, "chunked ok");

    n = http_build_post(req, (int)sizeof req, "h.example", "/v1/x",
                        "x-a: b\r\n", "BODY", 4);
    t_ok(n > 0, "build_post: returns length");
    req[n] = 0;
    t_has(req, "POST /v1/x HTTP/1.1\r\n", "build_post: request line");
    t_has(req, "Host: h.example\r\n", "build_post: Host header");
    t_has(req, "Content-Length: 4\r\n", "build_post: Content-Length");
    t_has(req, "x-a: b\r\n\r\nBODY", "build_post: extra hdr + body");
    t_ok(strstr(req, "Connection:") == 0, "build_post: no Connection header");

    /* GET: no Content-Length */
    n = http_build_request(req, (int)sizeof req, "GET", "h", "/", NULL, NULL,
                           0);
    t_ok(n > 0, "build_request: GET ok");
    req[n] = 0;
    t_has(req, "GET / HTTP/1.1\r\n", "build_request: GET line");
    t_ok(strstr(req, "Content-Length:") == 0, "build_request: GET no CL");

    /* overflow */
    t_int(http_build_post(req, 10, "h", "/", NULL, "BODY", 4), -1,
          "build_post: overflow -> -1");

    /* conn_close detection */
    {
        const char *resp = "HTTP/1.1 200 OK\r\nConnection: Close\r\n"
                           "Content-Length: 2\r\n\r\nok";
        http_resp r;
        memrd_t m;
        m.p = resp;
        m.n = (int)strlen(resp);
        m.i = 0;
        m.step = 99;
        r.body = body;
        r.body_cap = (int)sizeof body;
        t_int(http_read_response(memrd, &m, &r), 0, "conn_close: parse");
        t_int(r.status, 200, "conn_close: status");
        t_int(r.conn_close, 1, "conn_close: detected");
        t_int(r.body_len, 2, "conn_close: body_len");
    }

    /* chunked writer: exact framing for small + empty bodies */
    {
        t_buf mw;
        http_cw cw;
        t_buf_init(mw, raw);
        http_cw_init(&cw, t_buf_wr, &mw);
        t_int(http_cw_put(&cw, "hello", 5), 0, "cw: put ok");
        t_int(http_cw_end(&cw), 0, "cw: end ok");
        raw[mw.n] = 0;
        t_str(raw, "5\r\nhello\r\n0\r\n\r\n", "cw: small frame exact");

        mw.n = 0;
        http_cw_init(&cw, t_buf_wr, &mw);
        t_int(http_cw_end(&cw), 0, "cw: empty end ok");
        raw[mw.n] = 0;
        t_str(raw, "0\r\n\r\n", "cw: empty frame exact");
    }

    /* chunked writer: round-trip through our own decoder */
    cw_roundtrip("cw_roundtrip: tiny", "abc", 3, 1);
    {
        static char big[5000];
        int i;
        for (i = 0; i < (int)sizeof big; i++)
            big[i] = (char)('A' + (i % 26));
        cw_roundtrip("cw_roundtrip: multi-chunk", big, (int)sizeof big, 137);
        cw_roundtrip("cw_roundtrip: one-shot", big, (int)sizeof big,
                     (int)sizeof big);
    }
    return t_done();
}
