/* Drop-in stub for src/core/tls_client.c that serves canned HTTP responses
 * from files instead of opening a socket. Linked into dist/posix/relic-mock
 * so e2e scenarios can run offline.
 *
 * $RELIC_MOCK_RESPONSES is a colon-separated list of fixture paths; each
 * tls_open() consumes the next one. tls_write() is discarded (or appended
 * to $RELIC_MOCK_SENT for assertions). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/tls_client.h"

struct tls_conn {
    FILE *fp;
};
static struct tls_conn g_conn;
static const char *g_err = "mock: ok";
static int g_idx;

static const char *next_fixture(void)
{
    static char buf[1024];
    const char *list = getenv("RELIC_MOCK_RESPONSES");
    int i, n = 0, seg = 0;
    if (!list) return NULL;
    for (i = 0;; i++) {
        if (list[i] == ':' || list[i] == 0) {
            if (seg == g_idx) {
                buf[n] = 0;
                g_idx++;
                return n ? buf : NULL;
            }
            if (list[i] == 0) return NULL;
            seg++;
            n = 0;
        } else if (n < (int)sizeof buf - 1) {
            buf[n++] = list[i];
        }
    }
}

tls_conn *tls_open(const struct net_cfg *nc, const char *host)
{
    const char *path;
    (void)nc;
    (void)host;
    path = next_fixture();
    if (!path) {
        g_err = "mock: RELIC_MOCK_RESPONSES exhausted/unset";
        return NULL;
    }
    g_conn.fp = fopen(path, "rb");
    if (!g_conn.fp) {
        g_err = "mock: fixture open failed";
        return NULL;
    }
    return &g_conn;
}

int tls_write(tls_conn *c, const void *buf, int len)
{
    const char *out = getenv("RELIC_MOCK_SENT");
    (void)c;
    if (out) {
        FILE *f = fopen(out, "ab");
        if (f) {
            fwrite(buf, 1, (size_t)len, f);
            fclose(f);
        }
    }
    return len;
}

int tls_read(tls_conn *c, void *buf, int len)
{
    size_t n = fread(buf, 1, (size_t)len, c->fp);
    if (n == 0 && ferror(c->fp)) return -1;
    return (int)n;
}

void tls_close(tls_conn *c)
{
    if (c->fp) {
        fclose(c->fp);
        c->fp = NULL;
    }
}

int tls_last_error(void) { return -1; }
const char *tls_last_error_str(void) { return g_err; }
