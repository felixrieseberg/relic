#ifndef CORE_CFG_H
#define CORE_CFG_H

#include "netcfg.h" /* net_cfg */
#include "tools.h"  /* NTOOLS */

/* All user-facing runtime knobs in one place. main() owns the single
 * instance and passes it down; nothing else holds config globals.
 *
 * Deliberately NOT here:
 *   g_verbose          -- debug-trace flag read from vtrace() deep in the
 *                         call tree; threading it everywhere is noise.
 *   g_agent_interrupt  -- written from a signal handler; must be a real
 *                         file-scope volatile sig_atomic_t.
 *   scratch buffers, scroll/ui/tls/sess singletons -- not config. */
typedef struct relic_cfg {
    char model[64];
    char key[256];
    net_cfg net;
    /* permission policy (mutated by ask_permission on "always" answers) */
    unsigned char perm_always[NTOOLS];
    int yolo;
    int accept_edits;
    int noninteractive;
    int chat_only;
    int max_tokens;
} relic_cfg;

#endif
