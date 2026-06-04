#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "netcfg.h"
#include "anth.h"
#include "ui.h"
#include "util.h"
#include "tls_client.h"
#include "../plat/plat.h"

/* Truncate s at the first CR/LF in the first n bytes (or at n if none). */
static void chomp(char *s, int n)
{
    int i;
    for (i = 0; i < n && s[i] != '\r' && s[i] != '\n'; i++) {}
    s[i] = 0;
}

void net_set_proxy(net_cfg *nc, const char *spec)
{
    const char *c;
    int n;
    if (!spec || !*spec) {
        nc->proxy_host[0] = 0;
        nc->proxy_port = 0;
        return;
    }
    c = strchr(spec, ':');
    n = c ? (int)(c - spec) : (int)strlen(spec);
    if (n >= (int)sizeof nc->proxy_host) n = (int)sizeof nc->proxy_host - 1;
    memcpy(nc->proxy_host, spec, (size_t)n);
    nc->proxy_host[n] = 0;
    nc->proxy_port = (unsigned short)(c ? atoi(c + 1) : 8080);
}

static int sendall(int h, const char *s, int n)
{
    int w, off = 0;
    while (off < n) {
        w = plat_net_send(h, s + off, n - off);
        if (w <= 0) return -1;
        off += w;
    }
    return n;
}

/* Read until "\r\n\r\n" or buffer full. Returns bytes read. One byte at a
 * time on purpose: this drains a proxy CONNECT reply and MUST NOT consume
 * any TLS bytes that follow the blank line. */
static int read_headers(int h, char *buf, int cap)
{
    int n = 0, r;
    while (n < cap - 1) {
        r = plat_net_recv(h, buf + n, 1);
        if (r <= 0) break;
        n++;
        if (n >= 4 && memcmp(buf + n - 4, "\r\n\r\n", 4) == 0) break;
    }
    buf[n] = 0;
    return n;
}

int netcfg_connect(const net_cfg *nc, const char *host, unsigned short port)
{
    /* Override DNS for the API host if user supplied --ip / host_ip=. */
    const char *target = host;
    char req[320], resp[512];
    int h, n;

    if (nc && nc->host_ip[0] && strcmp(host, ANTH_HOST) == 0)
        target = nc->host_ip;

    if (!nc || !nc->proxy_host[0]) return plat_net_connect(target, port);

    /* HTTP CONNECT tunnel through proxy. */
    h = plat_net_connect(nc->proxy_host, nc->proxy_port);
    if (h < 0) return h;

    n = snprintf(req, sizeof req,
                 "CONNECT %s:%u HTTP/1.1\r\n"
                 "Host: %s:%u\r\n"
                 "User-Agent: relic\r\n"
                 "\r\n",
                 target, (unsigned)port, host, (unsigned)port);
    if (n < 0 || n >= (int)sizeof req) {
        plat_net_close(h);
        return NET_EPROXY;
    }
    if (sendall(h, req, n) < 0) {
        plat_net_close(h);
        return NET_EPROXY;
    }

    n = read_headers(h, resp, (int)sizeof resp);
    if (n < 12 || memcmp(resp, "HTTP/1.", 7) != 0 || resp[9] != '2') {
        chomp(resp, n);
        errf("  proxy CONNECT refused: %s", n ? resp : "(no reply)");
        plat_net_close(h);
        return NET_EPROXY;
    }
    return h;
}

/* --- self-test ------------------------------------------------------- */

static int probe(const char *label, const char *host, unsigned short port,
                 int http_get)
{
    int h, n = 0;
    char buf[128];
    printf("  %-32s ", label);
    fflush(stdout);
    h = plat_net_connect(host, port);
    if (h == PLAT_NET_EDNS) {
        printf("DNS FAIL\n");
        return 0;
    }
    if (h < 0) {
        printf("connect FAIL\n");
        return 0;
    }
    if (http_get) {
        n = snprintf(buf, sizeof buf, "GET / HTTP/1.0\r\nHost: %s\r\n\r\n",
                     host);
        sendall(h, buf, n);
        n = plat_net_recv(h, buf, (int)sizeof buf - 1);
        if (n > 0) {
            chomp(buf, n);
            tty_sanitize(buf, (int)strlen(buf));
        }
    }
    plat_net_close(h);
    if (http_get && n > 0)
        printf("OK  [%s]\n", buf);
    else
        printf("OK  (connected)\n");
    return 1;
}

int net_selftest(const net_cfg *nc)
{
    int ok = 0, save_verbose = g_verbose;
    /* Self-test is a diagnostic: force full trace so the per-I/O TLS
     * handshake hops from tls_client.c are always shown. */
    if (g_verbose < 2) g_verbose = 2;
    printf("network self-test (verbose trace forced on)\n");
    if (nc->host_ip[0]) printf("  host_ip override = %s\n", nc->host_ip);
    if (nc->proxy_host[0])
        printf("  proxy = %s:%u\n", nc->proxy_host, (unsigned)nc->proxy_port);

    ok |= probe("example.com:80 (plain HTTP)", "example.com", 80, 1);
    ok |= probe("neverssl.com:80 (plain HTTP)", "neverssl.com", 80, 1);
    ok |= probe("api.anthropic.com:443 (TCP)",
                nc->host_ip[0] ? nc->host_ip : ANTH_HOST, 443, 0);

    if (nc->proxy_host[0]) {
        int h;
        printf("  %-32s ", "proxy CONNECT api:443");
        fflush(stdout);
        h = netcfg_connect(nc, ANTH_HOST, 443);
        if (h >= 0) {
            printf("OK  (tunnel up)\n");
            plat_net_close(h);
            ok |= 1;
        } else if (h == NET_EPROXY)
            printf("REFUSED\n");
        else
            printf("FAIL (%d)\n", h);
    }

    /* Full TLS handshake against the API host -- drives the BearSSL
     * client so every send/recv hop is traced (see tls_client.c). */
    printf("  -- TLS handshake to %s:443 --\n", ANTH_HOST);
    {
        tls_conn *c = tls_open(nc, ANTH_HOST);
        if (!c)
            printf("  TLS FAIL: %s\n", tls_last_error_str());
        else {
            char req[128], resp[256];
            int n = snprintf(req, sizeof req,
                             "HEAD / HTTP/1.1\r\nHost: %s\r\n"
                             "Connection: close\r\n\r\n",
                             ANTH_HOST);
            if (tls_write(c, req, n) != n)
                printf("  TLS FAIL (handshake/write): %s\n",
                       tls_last_error_str());
            else if ((n = tls_read(c, resp, (int)sizeof resp - 1)) <= 0)
                printf("  TLS FAIL (read): %s\n", tls_last_error_str());
            else {
                chomp(resp, n);
                printf("  TLS OK  [%s]\n", resp);
                ok |= 1;
            }
            tls_close(c);
        }
    }

    g_verbose = save_verbose;

    if (!ok)
        printf(
            "\n  No outbound TCP at all. Check Win95 TCP/IP setup, gateway,\n"
            "  or set proxy=HOST:PORT in RELIC.CFG (see --proxy).\n");
    else if (nc->proxy_host[0] == 0)
        printf("\n  If only port 80 works, you're behind a proxy.\n"
               "  Find its address (Control Panel > Internet > Connection)\n"
               "  and run: RELIC --proxy HOST:PORT --nettest\n");
    return ok ? 0 : 1;
}
