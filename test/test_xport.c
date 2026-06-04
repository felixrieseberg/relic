/* Exercises xport.c's keepalive-retry path with a scripted TLS stub:
 * first request succeeds, second is served by a stale connection that
 * fails on read, third by a stale connection that fails on write. */

#include "t.h"
#include "../src/core/xport.h"
#include "../src/core/tls_client.h"
#include "../src/core/ui.h"

/* --- ui stubs ------------------------------------------------------- */
int g_verbose = 0;
void vtrace(const char *fmt, ...) { (void)fmt; }
void vdump(const char *l, const char *b, int n)
{
    (void)l;
    (void)b;
    (void)n;
}
void spin(const char *l) { (void)l; }
void spin_stat(const char *(*fn)(void)) { (void)fn; }
void spin_clear(void) {}
void errf(const char *fmt, ...) { (void)fmt; }
void errf_set_sink(void (*s)(const char *)) { (void)s; }

/* --- tls stub ------------------------------------------------------- */
struct tls_conn {
    int gen;
};
static struct tls_conn g_conn;
static int g_open_gen; /* incremented on each tls_open */
static int g_fail_read_gen = -1, g_fail_write_gen = -1;
static const char *g_resp;
static int g_resp_len, g_resp_pos;
static char g_sent[4096];
static int g_sent_len;

tls_conn *tls_open(const struct net_cfg *nc, const char *host)
{
    (void)nc;
    (void)host;
    g_conn.gen = ++g_open_gen;
    g_resp_pos = 0;
    return &g_conn;
}
int tls_write(tls_conn *c, const void *buf, int len)
{
    if (c->gen == g_fail_write_gen) return -1;
    if (g_sent_len + len <= (int)sizeof g_sent) {
        memcpy(g_sent + g_sent_len, buf, (size_t)len);
        g_sent_len += len;
    }
    return len;
}
int tls_read(tls_conn *c, void *buf, int len)
{
    int n;
    if (c->gen == g_fail_read_gen) return -1;
    n = g_resp_len - g_resp_pos;
    if (n > len) n = len;
    if (n <= 0) return 0;
    memcpy(buf, g_resp + g_resp_pos, (size_t)n);
    g_resp_pos += n;
    return n;
}
void tls_close(tls_conn *c) { (void)c; }
int tls_last_error(void) { return 0; }
const char *tls_last_error_str(void) { return "stub"; }

static const char OK[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";

int main(void)
{
    char body[256];
    int status, blen;

    t_group("xport");

    g_resp = OK;
    g_resp_len = (int)sizeof OK - 1;

    /* --- request 1: fresh connection ---------------------------------- */
    g_sent_len = 0;
    t_int(
        anth_get(NULL, "k", "/v1/ping", body, (int)sizeof body, &status, &blen),
        0, "GET ok");
    t_int(status, 200, "status 200");
    t_int(blen, 2, "body length");
    t_int(g_open_gen, 1, "one connect");
    g_sent[g_sent_len] = 0;
    t_has(g_sent, "GET /v1/ping HTTP/1.1\r\n", "request line");
    t_has(g_sent, "x-api-key: k\r\n", "api key header");

    /* --- request 2: stale read -> reconnect once ---------------------- */
    g_fail_read_gen = 1; /* the kept-alive gen-1 conn fails */
    g_sent_len = 0;
    t_int(
        anth_get(NULL, "k", "/v1/ping", body, (int)sizeof body, &status, &blen),
        0, "GET ok after stale read");
    t_int(g_open_gen, 2, "reconnected once");
    t_int(status, 200, "status 200 after retry");

    /* --- request 3: stale write -> reconnect once --------------------- */
    g_fail_write_gen = 2;
    g_fail_read_gen = -1;
    g_sent_len = 0;
    t_int(
        anth_get(NULL, "k", "/v1/ping", body, (int)sizeof body, &status, &blen),
        0, "GET ok after stale write");
    t_int(g_open_gen, 3, "reconnected once");

    return t_done();
}
