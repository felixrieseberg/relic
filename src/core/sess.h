#ifndef CORE_SESS_H
#define CORE_SESS_H

#include "conv.h"

#define SESS_MAX 999

/* Session store: <dir>/RELIC.IDX (last id) + RELICnnn.DAT (1..SESS_MAX).
 * <dir> is the first writable RELIC/ subdirectory of cwd, $TEMP, $TMP,
 * $TMPDIR, $HOME. All paths are 8.3-safe. */

/* want: 0 = fresh (allocate next id, lazy -- .DAT/IDX written on
 * sess_commit); -1 = latest existing; >0 = that id.
 * Returns 0 on success, -1 if session dir is unwritable, -2 if the
 * store is full (want == 0 and last id is SESS_MAX). */
int sess_open(conv_t *cv, int want);
/* Materialise a lazy fresh session on first use. No-op once cv->fp open. */
int sess_commit(conv_t *cv);
/* Close current and reopen .DAT for id (must exist). 0 / -1. */
int sess_switch(conv_t *cv, int id);
/* Peek session id: copy first user text into out[cap] (newlines->space),
 * set *size to file bytes. Returns 1 if .DAT exists, else 0. */
int sess_peek(int id, char *out, int cap, long *size);

int sess_id(void);          /* current id (0 if none) */
int sess_last(void);        /* highest id from RELIC.IDX */
const char *sess_dir(void); /* "" if none picked */

#endif
