/* Anthropic HTTPS transport: header build, persistent TLS keepalive,
 * chunked-body streaming, and the reconnect-once send/recv loop. Shared by
 * the CLI agent loop (agent.c) and embedders (src/plat/relicos). */

#include <stdio.h>
#include <signal.h>
#include "xport.h"
#include "anth.h"
#include "http.h"
#include "netcfg.h"
#include "tls_client.h"
#include "ui.h"

/* Set from a signal handler to abort the in-flight request. Declared in
 * agent.h but defined here because xport.c is the TU shared by both the CLI
 * (links agent.c) and the relicos embedder (does not). */
volatile sig_atomic_t g_agent_interrupt = 0;

/* Persistent TLS connection (keep-alive). */
static tls_conn *g_tls;

static void tls_drop(void)
{
    if (g_tls) {
        tls_close(g_tls);
        g_tls = NULL;
    }
}

static int rd_tls(void *ctx, char *buf, int len)
{
    int r;
    if (g_agent_interrupt) return -1;
    r = tls_read((tls_conn *)ctx, buf, len);
    if (g_agent_interrupt) return -1;
    if (r > 0) spin("Receiving");
    return r;
}

static int wr_tls(void *ctx, const char *buf, int len)
{
    return tls_write((tls_conn *)ctx, buf, len);
}

#define REQ_HDR_CAP 640

/* Build the HTTP request-line + headers (no body) for method/path.
 * chunked: 1 -> Transfer-Encoding: chunked + content-type; 0 -> no body.
 * Returns length, or -1 if it would not fit in REQ_HDR_CAP. */
static int build_hdr(char *h, const char *method, const char *path,
                     const char *api_key, int chunked)
{
    int n = snprintf(h, REQ_HDR_CAP,
                     "%s %s HTTP/1.1\r\n"
                     "Host: " ANTH_HOST "\r\n"
                     "x-api-key: %s\r\n"
                     "anthropic-version: " ANTH_VERSION "\r\n"
                     "%s\r\n",
                     method, path, api_key,
                     chunked ? "content-type: application/json\r\n"
                               "Transfer-Encoding: chunked\r\n"
                             : "");
    return (n < 0 || n >= REQ_HDR_CAP) ? -1 : n;
}

static int stream_body(tls_conn *tc, void *ctx)
{
    const req_ctx *rc = (const req_ctx *)ctx;
    http_cw w;
    http_cw_init(&w, wr_tls, tc);
    if (conv_send_request(http_cw_put, &w, rc->model, rc->max_tokens, rc->sys,
                          rc->tools, rc->cv)
        || http_cw_end(&w))
        return -1;
    vtrace("request body streamed (%ld bytes, chunked)", w.total);
    return 0;
}

/* Send hdr[0..hlen) then call send_body(tc, ctx) (if non-NULL) to stream the
 * body, and read the response into body[0..body_cap). Reuses g_tls; on a
 * stale-socket failure, reconnects once and replays (send_body must be
 * idempotent). */
static int send_recv(const struct net_cfg *nc, const char *hdr, int hlen,
                     int (*send_body)(tls_conn *, void *), void *ctx,
                     char *body, int body_cap, int *status, int *rlen)
{
    http_resp r;
    int attempt;
    for (attempt = 0; attempt < 2; attempt++) {
        int reused = (g_tls != NULL);
        if (g_agent_interrupt) {
            tls_drop();
            spin_clear();
            return -1;
        }
        if (!g_tls) {
            spin("Connecting");
            g_tls = tls_open(nc, ANTH_HOST);
            if (!g_tls) {
                spin_clear();
                errf("Connect failed: %s", tls_last_error_str());
                return -1;
            }
            vtrace("TLS connection opened");
        } else {
            spin("Sending");
            vtrace("reusing TLS connection");
        }
        if (tls_write(g_tls, hdr, hlen) < 0
            || (send_body && send_body(g_tls, ctx) != 0)) {
            tls_drop();
            if (g_agent_interrupt) {
                spin_clear();
                return -1;
            }
            if (reused) {
                vtrace("write on reused connection failed; reconnecting");
                continue;
            }
            spin_clear();
            errf("TLS handshake/write failed: %s", tls_last_error_str());
            return -1;
        }
        spin("Waiting");
        vtrace("waiting for response ...");
        r.body = body;
        r.body_cap = body_cap;
        if (http_read_response(rd_tls, g_tls, &r) != 0) {
            tls_drop();
            if (g_agent_interrupt) {
                spin_clear();
                return -1;
            }
            if (reused) {
                vtrace("read on reused connection failed; reconnecting");
                continue;
            }
            spin_clear();
            errf("HTTP parse failed");
            return -1;
        }
        spin_clear();
        if (r.conn_close) {
            vtrace("server sent Connection: close");
            tls_drop();
        }
        vtrace("HTTP %d, body %d bytes%s", r.status, r.body_len,
               r.truncated ? " (TRUNCATED)" : "");
        vdump("response body", body, r.body_len);
        *status = r.status;
        *rlen = r.body_len;
        return 0;
    }
    spin_clear();
    errf("Connection failed after retry: %s", tls_last_error_str());
    return -1;
}

int anth_post(const struct net_cfg *nc, const char *api_key, const req_ctx *rc,
              char *body, int body_cap, int *status, int *blen)
{
    char hdr[REQ_HDR_CAP];
    int hlen = build_hdr(hdr, "POST", ANTH_PATH, api_key, 1);
    if (hlen < 0) {
        errf("API key too long for request header");
        return -1;
    }
    vtrace("POST https://" ANTH_HOST ANTH_PATH "  (hdr %d, body chunked)",
           hlen);
    return send_recv(nc, hdr, hlen, stream_body, (void *)rc, body, body_cap,
                     status, blen);
}

int anth_get(const struct net_cfg *nc, const char *api_key, const char *path,
             char *body, int body_cap, int *status, int *blen)
{
    char hdr[REQ_HDR_CAP];
    int hlen = build_hdr(hdr, "GET", path, api_key, 0);
    if (hlen < 0) return -1;
    vtrace("GET https://" ANTH_HOST "%s  (%d bytes)", path, hlen);
    return send_recv(nc, hdr, hlen, NULL, NULL, body, body_cap, status, blen);
}
