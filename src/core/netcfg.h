#ifndef CORE_NETCFG_H
#define CORE_NETCFG_H

typedef struct net_cfg {
    char proxy_host[64]; /* "" = no proxy */
    unsigned short proxy_port;
    char host_ip[16]; /* "" = use DNS for api host */
} net_cfg;

/* Parse "host[:port]" into nc->proxy_*. Empty/NULL spec clears the proxy. */
void net_set_proxy(net_cfg *nc, const char *spec);

/* TCP connect to host:port, honoring nc->proxy_* (HTTP CONNECT tunnel) and
 * nc->host_ip (DNS bypass for the api host). nc may be NULL (= direct).
 * Returns handle >=0 or PLAT_NET_EDNS / PLAT_NET_ECONN / NET_EPROXY. */
#define NET_EPROXY -4
int netcfg_connect(const net_cfg *nc, const char *host, unsigned short port);

/* Self-test: prints each step (resolve, port-80 probe, port-443 probe,
 * proxy probe if set) to stdout. Returns 0 if at least one path reached
 * a remote server. */
int net_selftest(const net_cfg *nc);

#endif
