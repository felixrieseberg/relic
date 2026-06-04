#ifndef CORE_XPORT_H
#define CORE_XPORT_H

#include "conv.h"

struct net_cfg;

/* Per-request parameters for an Anthropic /v1/messages POST. */
typedef struct {
    const char *model, *sys, *tools;
    int max_tokens;
    conv_t *cv;
} req_ctx;

/* POST /v1/messages with a chunked body built from *rc. Reuses a process-wide
 * persistent TLS keepalive (one reconnect on stale-socket failure). Response
 * body lands in body[0..*blen). nc may be NULL. 0 ok, -1 fail (errf'd). */
int anth_post(const struct net_cfg *nc, const char *api_key, const req_ctx *rc,
              char *body, int body_cap, int *status, int *blen);

/* GET path (no request body) over the same keepalive. */
int anth_get(const struct net_cfg *nc, const char *api_key, const char *path,
             char *body, int body_cap, int *status, int *blen);

#endif
