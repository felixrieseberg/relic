#include "t.h"
#include "../src/core/netcfg.h"

/* net_set_proxy is the only pure function in netcfg.c; the rest needs a
 * live socket. Stub the externs it pulls in via tls/ui/plat so the file
 * links standalone. */
struct tls_conn;
struct tls_conn *tls_open(const struct net_cfg *nc, const char *h)
{
    (void)nc;
    (void)h;
    return 0;
}
int tls_write(struct tls_conn *c, const void *b, int n)
{
    (void)c;
    (void)b;
    (void)n;
    return -1;
}
int tls_read(struct tls_conn *c, void *b, int n)
{
    (void)c;
    (void)b;
    (void)n;
    return -1;
}
void tls_close(struct tls_conn *c) { (void)c; }
const char *tls_last_error_str(void) { return ""; }
void errf(const char *fmt, ...) { (void)fmt; }
int g_verbose = 0;
int plat_net_connect(const char *h, unsigned short p)
{
    (void)h;
    (void)p;
    return -1;
}
int plat_net_send(int h, const char *b, int n)
{
    (void)h;
    (void)b;
    (void)n;
    return -1;
}
int plat_net_recv(int h, char *b, int n)
{
    (void)h;
    (void)b;
    (void)n;
    return -1;
}
void plat_net_close(int h) { (void)h; }

int main(void)
{
    net_cfg nc;

    t_group("netcfg");

    memset(&nc, 0xEE, sizeof nc);
    net_set_proxy(&nc, "proxy.example:3128");
    t_str(nc.proxy_host, "proxy.example", "set_proxy: host parsed");
    t_int(nc.proxy_port, 3128, "set_proxy: port parsed");

    net_set_proxy(&nc, "squid");
    t_str(nc.proxy_host, "squid", "set_proxy: host only");
    t_int(nc.proxy_port, 8080, "set_proxy: default port 8080");

    net_set_proxy(&nc, "h:0");
    t_int(nc.proxy_port, 0, "set_proxy: port 0 honoured");

    net_set_proxy(&nc, "");
    t_str(nc.proxy_host, "", "set_proxy: empty clears host");
    t_int(nc.proxy_port, 0, "set_proxy: empty clears port");

    nc.proxy_host[0] = 'X';
    nc.proxy_port = 9;
    net_set_proxy(&nc, NULL);
    t_str(nc.proxy_host, "", "set_proxy: NULL clears host");
    t_int(nc.proxy_port, 0, "set_proxy: NULL clears port");

    /* over-long host -> truncated to fit (sizeof proxy_host == 64). */
    {
        char spec[128];
        int i;
        for (i = 0; i < 100; i++)
            spec[i] = 'a';
        spec[100] = 0;
        net_set_proxy(&nc, spec);
        t_int((int)strlen(nc.proxy_host), 63, "set_proxy: long host truncated");
        t_int(nc.proxy_port, 8080, "set_proxy: long host default port");
    }

    return t_done();
}
