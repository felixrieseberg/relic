#ifndef CORE_CONV_H
#define CORE_CONV_H

#include <stdio.h>

/* Disk-backed conversation history. Turn bodies live in CONV_STORE_PATH as
 * length-prefixed records; RAM holds only {role, is_json, off, len} per turn.
 * The store is persistent: conv_init() scans it and resumes the last session.
 * Record = [role:1][is_json:1][len:4 LE][body:len]. */

#define CONV_CAP 128
/* When the index fills, drop this many oldest turns at once so the
 * messages[] prefix stays stable across many requests (prompt caching). */
#define CONV_DROP_STEP (CONV_CAP / 4)

/* Values are the on-disk record tag bytes. */
enum { CONV_ROLE_USER = 'u', CONV_ROLE_ASST = 'a' };

typedef struct {
    unsigned char role; /* CONV_ROLE_* */
    unsigned char is_json;
    long off;
    long len;
} conv_turn;

typedef struct {
    FILE *fp;
    char path[128];
    long fend;
    conv_turn turns[CONV_CAP];
    int count;
    int dropped; /* total turns dropped this session */
} conv_t;

/* Open store at path. resume: 1 = "r+b" + scan existing records (falls back
 * to fresh create if absent); 0 = "w+b" truncate. 0 / -1. */
int conv_init(conv_t *c, const char *path, int resume);
/* Clear history AND truncate the on-disk store. */
void conv_reset(conv_t *c);
/* Sum of stored turn body lengths. */
long conv_bytes(const conv_t *c);
/* If conv_bytes() > budget, drop oldest turns (pair-aligned, after the
 * pinned leading user-text turn, plus any orphaned tool_result) until
 * <= 3/4 budget or count <= 2. Returns number of turns dropped. */
int conv_trim(conv_t *c, long budget);

/* Single-shot push. n<0 -> strlen. Returns 0 ok, -1 I/O error. */
int conv_push(conv_t *c, int role, int is_json, const char *p, long n);
int conv_push_text(conv_t *c, int role, const char *text);

/* Streaming push: open -> any number of conv_write -> commit. */
int conv_open(conv_t *c, int role, int is_json);
int conv_write(conv_t *c, const char *p, long n);
int conv_commit(conv_t *c);

/* Read body of turn i into dst (cap). Returns bytes read, or -1. */
long conv_read(const conv_t *c, int i, char *dst, long cap);

/* Sink callback for conv_send_request: write n bytes; return 0 ok / -1. */
typedef int (*conv_wr_fn)(void *ctx, const char *p, int n);

/* Stream a /v1/messages request body through wr(ctx,...). Turn bodies are
 * read from disk in small chunks; nothing is held in RAM. Inserts
 * cache_control markers on the last tool definition and the last message.
 * system_prompt / tools_json may be NULL. Returns 0 ok, -1 wr/I/O error. */
int conv_send_request(conv_wr_fn wr, void *ctx, const char *model,
                      int max_tokens, const char *system_prompt,
                      const char *tools_json, conv_t *cv);

#endif
