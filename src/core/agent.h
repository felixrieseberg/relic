#ifndef CORE_AGENT_H
#define CORE_AGENT_H

#include <signal.h>
#include "conv.h"
#include "cfg.h"

/* Caller-owned scratch buffers for one agent_run()/agent_list_models(). */
typedef struct agent_scratch {
    char body[65536]; /* HTTP response body */
    char out[16384];  /* tool output / per-iteration scratch */
    char sys[8192];   /* rendered system prompt (lazy-filled) */
    int sys_len;
} agent_scratch;

/* Run the agentic loop starting from the current state of *cv.
 * Reads cfg->model/key/net; consults and may mutate cfg permission flags
 * (the "always" prompt answer flips accept_edits / perm_always).
 * Returns 0 ok, 1 api-error, -1 fail. */
int agent_run(conv_t *cv, relic_cfg *cfg, agent_scratch *sc);

/* Fetch GET /v1/models and print the list. 0 ok, -1 fail. */
int agent_list_models(const relic_cfg *cfg, agent_scratch *sc);

/* Set from a signal handler to abort the current agent_run(). */
extern volatile sig_atomic_t g_agent_interrupt;

#endif
