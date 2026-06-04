/* BearSSL client wrapper. Static single-connection state; portable C.
 * Platform I/O via plat.h only. */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "bearssl.h"
#include "tls_client.h"
#include "netcfg.h"
#include "anth.h"
#include "ui.h"
#include "../plat/plat.h"
#include "trust_anchors.h" /* TAs[], TAs_NUM */

/* Defined in xport.c. Checked here so the 250 ms poll loop is the abort
 * latency for both ESC and (on Win9x, where recv() isn't signal-broken)
 * Ctrl+C. */
extern volatile sig_atomic_t g_agent_interrupt;

struct tls_conn {
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    /* HTTP/1.1 is request-then-response, so half-duplex (one shared record
     * buffer) is sufficient and saves ~16 KB over BR_SSL_BUFSIZE_BIDI. */
    unsigned char iobuf[BR_SSL_BUFSIZE_MONO];
    int fd;
    int open;
    /* Handshake tracing: until hs_done, log every socket I/O so a "hang" on
     * a slow/emulated box can be pinned to either (a) recv() never returning
     * (winsock / emulator network) or (b) long CPU gap between recv and the
     * next send (BearSSL doing cert-verify + ECDHE -- tens of seconds on a
     * 486). */
    int hs_done;
    int hs_tx, hs_rx;
    unsigned long hs_t0;
    char hs_dir; /* last I/O direction: 's' or 'r' */
};

static struct tls_conn g_conn;
static int g_last_err;

static unsigned long hs_elapsed(const tls_conn *c)
{
    return plat_time_unix() - c->hs_t0;
}

static int io_recv(void *ctx, unsigned char *buf, size_t len)
{
    tls_conn *c = (tls_conn *)ctx;
    int r;
    if (!c->hs_done) {
        /* One "waiting" marker per server flight (or every recv at -v -v) so
         * a true recv() hang is visible as the last line on screen. */
        if (c->hs_dir != 'r' || g_verbose >= 2)
            vtrace("tls: <- recv() blocking, waiting for server ... [t+%lus]",
                   hs_elapsed(c));
        c->hs_dir = 'r';
    }
    /* Poll so the spinner's seconds counter ticks while we're blocked on the
     * server (handshake flights and the API "Waiting" phase alike). */
    do {
        spin(0);
        if (plat_esc_poll()) g_agent_interrupt = 1;
        if (g_agent_interrupt) return -1;
    } while ((r = plat_net_wait(c->fd, 250)) == 0);
    if (r < 0) return -1;
    r = plat_net_recv(c->fd, buf, (int)len);
    if (!c->hs_done) {
        if (r > 0) {
            c->hs_rx += r;
            vtrace("tls: <- recv %4d bytes (rx=%d) [t+%lus] -- processing ...",
                   r, c->hs_rx, hs_elapsed(c));
        } else
            vtrace("tls: <- recv returned %d (EOF/err) [t+%lus]", r,
                   hs_elapsed(c));
    }
    return (r == 0) ? -1 : r; /* BearSSL: 0 is invalid; map EOF -> -1 */
}
static int io_send(void *ctx, const unsigned char *buf, size_t len)
{
    tls_conn *c = (tls_conn *)ctx;
    int r;
    if (!c->hs_done) spin(0);
    r = plat_net_send(c->fd, buf, (int)len);
    if (!c->hs_done) {
        if (r > 0) c->hs_tx += r;
        vtrace("tls: -> send %4d bytes (tx=%d) [t+%lus]", r, c->hs_tx,
               hs_elapsed(c));
        c->hs_dir = 's';
    }
    return r;
}

/* Minimal TLS 1.2 client profile: ECDHE (RSA+ECDSA) with AES128-GCM and
 * ChaCha20-Poly1305 only. Replaces br_ssl_client_init_full so the linker
 * can drop CBC, 3DES, AES-256, MD5, the big AES tables, etc. */
static void tls_init_minimal(br_ssl_client_context *cc,
                             br_x509_minimal_context *xc,
                             const br_x509_trust_anchor *tas, size_t ntas)
{
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256};
    br_ssl_client_zero(cc);
    br_ssl_engine_set_versions(&cc->eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(&cc->eng, suites,
                             sizeof suites / sizeof suites[0]);

    br_ssl_engine_set_hash(&cc->eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_prf_sha256(&cc->eng, &br_tls12_sha256_prf);

    br_ssl_engine_set_gcm(&cc->eng, &br_sslrec_in_gcm_vtable,
                          &br_sslrec_out_gcm_vtable);
    br_ssl_engine_set_aes_ctr(&cc->eng, &br_aes_small_ctr_vtable);
    br_ssl_engine_set_ghash(&cc->eng, &br_ghash_ctmul32);
    br_ssl_engine_set_chapol(&cc->eng, &br_sslrec_in_chapol_vtable,
                             &br_sslrec_out_chapol_vtable);
    br_ssl_engine_set_chacha20(&cc->eng, &br_chacha20_ct_run);
    br_ssl_engine_set_poly1305(&cc->eng, &br_poly1305_ctmul32_run);

    br_ssl_engine_set_ec(&cc->eng, &br_ec_all_m31);
    br_ssl_engine_set_rsavrfy(&cc->eng, &br_rsa_i31_pkcs1_vrfy);
    br_ssl_engine_set_ecdsa(&cc->eng, &br_ecdsa_i31_vrfy_asn1);

    br_x509_minimal_init(xc, &br_sha256_vtable, tas, ntas);
    br_x509_minimal_set_hash(xc, br_sha1_ID, &br_sha1_vtable);
    br_x509_minimal_set_hash(xc, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_hash(xc, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_hash(xc, br_sha512_ID, &br_sha512_vtable);
    br_x509_minimal_set_rsa(xc, &br_rsa_i31_pkcs1_vrfy);
    br_x509_minimal_set_ecdsa(xc, &br_ec_all_m31, &br_ecdsa_i31_vrfy_asn1);
    br_ssl_engine_set_x509(&cc->eng, &xc->vtable);
}

tls_conn *tls_open(const net_cfg *nc, const char *host)
{
    tls_conn *c = &g_conn;
    unsigned char seed[32];
    unsigned long now, udays, secs;

    if (c->open) return NULL;
    g_last_err = 0;

    vtrace("connecting to %s:443%s%s ...", host,
           (nc && nc->proxy_host[0]) ? " via proxy " : "",
           (nc && nc->proxy_host[0]) ? nc->proxy_host : "");
    c->fd = netcfg_connect(nc, host, 443);
    if (c->fd < 0) {
        g_last_err = c->fd;
        return NULL;
    }
    c->hs_done = 0;
    c->hs_tx = c->hs_rx = 0;
    c->hs_dir = 0;
    c->hs_t0 = plat_time_unix();
    vtrace("TCP connected (fd=%d), starting TLS handshake (CPU-bound; can take "
           "MINUTES on slow/emulated x86) ...",
           c->fd);

    tls_init_minimal(&c->sc, &c->xc, TAs, TAs_NUM);
    br_ssl_engine_set_buffer(&c->sc.eng, c->iobuf, sizeof c->iobuf, 0);

    /* BearSSL is built without a system time source on retro targets, so we
     * MUST set the validation time explicitly. Use the platform clock when it
     * is plausible (>= 2020-01-01); otherwise fall back to the build date so a
     * Win95 box that thinks it's 1997 still validates the chain.
     * BearSSL days are since 0 AD; 1970-01-01 = day 719528. */
    now = plat_time_unix();
    if (now >= 1577836800UL) {
        udays = now / 86400UL;
        secs = now % 86400UL;
    } else {
        udays = (unsigned long)BUILD_UNIX_DAYS;
        secs = 0;
    }
    br_x509_minimal_set_time(&c->xc, (uint32_t)(udays + 719528UL),
                             (uint32_t)secs);

    if (plat_entropy(seed, (int)sizeof seed) > 0)
        br_ssl_engine_inject_entropy(&c->sc.eng, seed, sizeof seed);

    if (!br_ssl_client_reset(&c->sc, host, 0)) {
        g_last_err = br_ssl_engine_last_error(&c->sc.eng);
        plat_net_close(c->fd);
        return NULL;
    }
    br_sslio_init(&c->ioc, &c->sc.eng, io_recv, c, io_send, c);
    c->open = 1;
    return c;
}

int tls_write(tls_conn *c, const void *buf, int len)
{
    if (br_sslio_write_all(&c->ioc, buf, (size_t)len) < 0
        || br_sslio_flush(&c->ioc) < 0) {
        g_last_err = br_ssl_engine_last_error(&c->sc.eng);
        return -1;
    }
    if (!c->hs_done) {
        br_ssl_session_parameters sp;
        br_ssl_engine_get_session_parameters(&c->sc.eng, &sp);
        vtrace("TLS handshake OK in %lus (cipher=0x%04x, hs tx=%d rx=%d), "
               "request %d bytes flushed",
               hs_elapsed(c), (unsigned)sp.cipher_suite, c->hs_tx, c->hs_rx,
               len);
        c->hs_done = 1;
    }
    return len;
}

int tls_read(tls_conn *c, void *buf, int len)
{
    int r = br_sslio_read(&c->ioc, buf, (size_t)len);
    if (r < 0) {
        int e = br_ssl_engine_last_error(&c->sc.eng);
        if (e == 0 || e == BR_ERR_OK) return 0; /* clean close */
        g_last_err = e;
        return -1;
    }
    return r;
}

void tls_close(tls_conn *c)
{
    if (!c || !c->open) return;
    /* Don't br_sslio_close() -- it loops draining peer data; we just drop. */
    plat_net_close(c->fd);
    c->open = 0;
}

int tls_last_error(void) { return g_last_err; }

const char *tls_last_error_str(void)
{
    static char buf[96];
    switch (g_last_err) {
    case 0: return "ok";
    case PLAT_NET_EDNS:
        return "DNS lookup failed -- can't resolve " ANTH_HOST
               " (check network/DNS)";
    case PLAT_NET_ECONN:
        return "TCP connect to " ANTH_HOST
               ":443 failed (offline, firewalled, or behind a proxy?)";
    case NET_EPROXY: return "HTTP proxy refused CONNECT to " ANTH_HOST ":443";
    case BR_ERR_X509_NOT_TRUSTED:
        return "server certificate not trusted -- this build's pinned roots "
               "may be outdated";
    case BR_ERR_X509_EXPIRED:
        return "certificate date check failed -- set your system clock";
    default:
        snprintf(buf, sizeof buf, "BearSSL error %d", g_last_err);
        return buf;
    }
}
