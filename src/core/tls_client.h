#ifndef CORE_TLS_CLIENT_H
#define CORE_TLS_CLIENT_H

typedef struct tls_conn tls_conn;
struct net_cfg;

/* Open TLS 1.2 to host:443 (via nc->proxy if set; nc may be NULL = direct).
 * One connection at a time. NULL on failure; tls_last_error() gives the
 * BearSSL code (or -1 for TCP failure). */
tls_conn *tls_open(const struct net_cfg *nc, const char *host);
int tls_write(tls_conn *c, const void *buf, int len); /* writes all */
int tls_read(tls_conn *c, void *buf, int len);        /* >0, 0 EOF, -1 */
void tls_close(tls_conn *c);
/* >0 = BearSSL error code; <0 = PLAT_NET_* / NET_EPROXY. */
int tls_last_error(void);
const char *tls_last_error_str(void);

#endif
