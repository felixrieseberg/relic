#include <string.h>
#include "conv.h"
#include "util.h"
#include "json_write.h"

#define REC_HDR 6
#define REC_LEN_MAX (1L << 20)

/* The API rejects messages[] whose first entry is role:"assistant", and a
 * tool_result with no preceding tool_use. A leading user-text turn is the
 * only valid anchor; once present it is pinned and drops happen after it. */
static int anchor(const conv_t *c)
{
    return c->count > 0 && c->turns[0].role == CONV_ROLE_USER
           && !c->turns[0].is_json;
}

static void drop_n(conv_t *c, int n)
{
    int k = anchor(c);
    if (n > c->count - k) n = c->count - k;
    if (n <= 0) return;
    memmove(&c->turns[k], &c->turns[k + n],
            (size_t)(c->count - k - n) * sizeof c->turns[0]);
    c->count -= n;
    c->dropped += n;
}

/* After a drop the slot just past the anchor may be an orphan tool_result
 * (its matching tool_use in the preceding assistant turn was just dropped);
 * shed those too. */
static void drop_to_boundary(conv_t *c)
{
    int k = anchor(c);
    while (c->count > k && c->turns[k].role == CONV_ROLE_USER
           && c->turns[k].is_json)
        drop_n(c, 1);
}

/* Scan an existing store, rebuilding turns[] (last CONV_CAP). Stops at the
 * first malformed/short record; c->fend is left at the last good boundary so
 * later writes overwrite the junk. */
static void conv_scan(conv_t *c)
{
    unsigned char hdr[REC_HDR];
    long pos = 0, fsz;
    if (fseek(c->fp, 0L, SEEK_END) != 0) return;
    fsz = ftell(c->fp);
    while (pos + REC_HDR <= fsz) {
        long len;
        conv_turn *t;
        if (fseek(c->fp, pos, SEEK_SET) != 0) break;
        if (fread(hdr, 1, REC_HDR, c->fp) != REC_HDR) break;
        if (hdr[0] != CONV_ROLE_USER && hdr[0] != CONV_ROLE_ASST) break;
        if (hdr[1] > 1) break;
        len = (long)rd_u32le(hdr + 2);
        if (len < 0 || len > REC_LEN_MAX) break;
        if (pos + REC_HDR + len > fsz) break;
        if (c->count >= CONV_CAP) drop_n(c, CONV_DROP_STEP);
        t = &c->turns[c->count++];
        t->role = hdr[0];
        t->is_json = hdr[1];
        t->off = pos + REC_HDR;
        t->len = len;
        pos += REC_HDR + len;
    }
    c->fend = pos;
    drop_to_boundary(c);
}

int conv_init(conv_t *c, const char *path, int resume)
{
    memset(c, 0, sizeof *c);
    str_set(c->path, (int)sizeof c->path, path);
    if (resume) {
        c->fp = fopen(c->path, "r+b");
        if (c->fp) {
            conv_scan(c);
            return 0;
        }
    }
    c->fp = fopen(c->path, "w+b");
    return c->fp ? 0 : -1;
}

void conv_reset(conv_t *c)
{
    c->count = 0;
    c->fend = 0;
    c->dropped = 0;
    if (c->fp) {
        c->fp = freopen(c->path, "w+b", c->fp);
        if (!c->fp) c->fp = fopen(c->path, "w+b");
    }
}

long conv_bytes(const conv_t *c)
{
    long s = 0;
    int i;
    for (i = 0; i < c->count; i++)
        s += c->turns[i].len;
    return s;
}

int conv_trim(conv_t *c, long budget)
{
    long target;
    int before = c->count;
    if (conv_bytes(c) <= budget) return 0;
    target = budget - budget / 4;
    while (c->count > 2 && conv_bytes(c) > target)
        drop_n(c, 2);
    drop_to_boundary(c);
    return before - c->count;
}

int conv_open(conv_t *c, int role, int is_json)
{
    unsigned char hdr[REC_HDR];
    conv_turn *t;
    if (c->count >= CONV_CAP) {
        drop_n(c, CONV_DROP_STEP);
        drop_to_boundary(c);
    }
    t = &c->turns[c->count];
    t->role = (unsigned char)role;
    t->is_json = (unsigned char)is_json;
    t->off = c->fend + REC_HDR;
    t->len = 0;
    hdr[0] = (unsigned char)role;
    hdr[1] = (unsigned char)is_json;
    wr_u32le(hdr + 2, 0); /* patched in conv_commit */
    if (fseek(c->fp, c->fend, SEEK_SET) != 0) return -1;
    if (fwrite(hdr, 1, REC_HDR, c->fp) != REC_HDR) return -1;
    c->fend += REC_HDR;
    return 0;
}

int conv_write(conv_t *c, const char *p, long n)
{
    if (n < 0) n = (long)strlen(p);
    if (n == 0) return 0;
    if ((long)fwrite(p, 1, (size_t)n, c->fp) != n) return -1;
    c->fend += n;
    c->turns[c->count].len += n;
    return 0;
}

int conv_commit(conv_t *c)
{
    unsigned char lb[4];
    conv_turn *t = &c->turns[c->count];
    wr_u32le(lb, (unsigned long)t->len);
    if (fseek(c->fp, t->off - 4, SEEK_SET) != 0 || fwrite(lb, 1, 4, c->fp) != 4
        || fflush(c->fp) != 0)
        return -1;
    c->count++;
    return 0;
}

int conv_push(conv_t *c, int role, int is_json, const char *p, long n)
{
    if (conv_open(c, role, is_json) || conv_write(c, p, n)) return -1;
    return conv_commit(c);
}

int conv_push_text(conv_t *c, int role, const char *text)
{
    return conv_push(c, role, 0, text, -1);
}

long conv_read(const conv_t *c, int i, char *dst, long cap)
{
    const conv_turn *t = &c->turns[i];
    if (t->len > cap) return -1;
    if (fseek(c->fp, t->off, SEEK_SET) != 0) return -1;
    if ((long)fread(dst, 1, (size_t)t->len, c->fp) != t->len) return -1;
    return t->len;
}

/* ---- request streamer ------------------------------------------------- */

static const char CACHE_CTRL[] = ",\"cache_control\":{\"type\":\"ephemeral\"}";

/* Stream the first `take` bytes of turn i's body, verbatim or JSON-escaped. */
static int wr_body(conv_wr_fn wr, void *ctx, conv_t *cv, int i, long take,
                   int escape)
{
    char tmp[512];
    long rem = take;
    if (fseek(cv->fp, cv->turns[i].off, SEEK_SET) != 0) return -1;
    while (rem > 0) {
        long want = rem < (long)sizeof tmp ? rem : (long)sizeof tmp;
        long r = (long)fread(tmp, 1, (size_t)want, cv->fp);
        if (r <= 0) return -1;
        if (escape ? json_escape_to(wr, ctx, tmp, (int)r)
                   : wr(ctx, tmp, (int)r))
            return -1;
        rem -= r;
    }
    return 0;
}

/* Does stored is_json body end exactly "}]"?  (so we can splice cache_ctrl) */
static int turn_ends_jarr(conv_t *cv, int i)
{
    char tail[2];
    const conv_turn *t = &cv->turns[i];
    if (t->len < 2) return 0;
    if (fseek(cv->fp, t->off + t->len - 2, SEEK_SET) != 0) return 0;
    if (fread(tail, 1, 2, cv->fp) != 2) return 0;
    return tail[0] == '}' && tail[1] == ']';
}

/* Sticky-error writer: every put_* is a no-op once w->err is set, so the
 * body reads top-to-bottom with one final error check instead of a return
 * hidden inside a macro at every call site. */
typedef struct {
    conv_wr_fn wr;
    void *ctx;
    int err;
} sw_t;

static void put(sw_t *w, const char *s, int n)
{
    if (!w->err && w->wr(w->ctx, s, n)) w->err = -1;
}
/* String-literal / static-array only: length is sizeof-1 at compile time. */
#define PUT_LIT(w, s) put((w), (s), (int)(sizeof(s) - 1))

static void put_escaped(sw_t *w, const char *s, int n)
{
    if (!w->err && json_escape_to(w->wr, w->ctx, s, n)) w->err = -1;
}
static void put_body(sw_t *w, conv_t *cv, int i, long take)
{
    if (!w->err && wr_body(w->wr, w->ctx, cv, i, take, 0)) w->err = -1;
}
static void put_body_escaped(sw_t *w, conv_t *cv, int i)
{
    if (!w->err && wr_body(w->wr, w->ctx, cv, i, cv->turns[i].len, 1))
        w->err = -1;
}

int conv_send_request(conv_wr_fn wr, void *ctx, const char *model,
                      int max_tokens, const char *system_prompt,
                      const char *tools_json, conv_t *cv)
{
    char num[16];
    sw_t w;
    int nlen = 0;
    int last = cv->count - 1;
    int i;

    w.wr = wr;
    w.ctx = ctx;
    w.err = 0;

    PUT_LIT(&w, "{\"model\":\"");
    put_escaped(&w, model, (int)strlen(model));
    PUT_LIT(&w, "\",\"max_tokens\":");
    sb_udec(num, (int)sizeof num, &nlen, (unsigned)max_tokens);
    put(&w, num, nlen);

    if (system_prompt) {
        PUT_LIT(&w, ",\"system\":\"");
        put_escaped(&w, system_prompt, (int)strlen(system_prompt));
        PUT_LIT(&w, "\"");
    }
    if (tools_json) {
        int tl = (int)strlen(tools_json);
        PUT_LIT(&w, ",\"tools\":");
        if (tl >= 2 && tools_json[tl - 2] == '}' && tools_json[tl - 1] == ']') {
            put(&w, tools_json, tl - 2);
            PUT_LIT(&w, CACHE_CTRL);
            PUT_LIT(&w, "}]");
        } else {
            put(&w, tools_json, tl);
        }
    }

    PUT_LIT(&w, ",\"messages\":[");
    for (i = 0; i < cv->count; i++) {
        const conv_turn *t = &cv->turns[i];
        if (i) PUT_LIT(&w, ",");
        if (t->role == CONV_ROLE_ASST)
            PUT_LIT(&w, "{\"role\":\"assistant\",\"content\":");
        else
            PUT_LIT(&w, "{\"role\":\"user\",\"content\":");
        if (t->is_json) {
            if (i == last && turn_ends_jarr(cv, i)) {
                put_body(&w, cv, i, t->len - 2);
                PUT_LIT(&w, CACHE_CTRL);
                PUT_LIT(&w, "}]");
            } else {
                put_body(&w, cv, i, t->len);
            }
        } else if (i == last) {
            PUT_LIT(&w, "[{\"type\":\"text\",\"text\":\"");
            put_body_escaped(&w, cv, i);
            PUT_LIT(&w, "\"");
            PUT_LIT(&w, CACHE_CTRL);
            PUT_LIT(&w, "}]");
        } else {
            PUT_LIT(&w, "\"");
            put_body_escaped(&w, cv, i);
            PUT_LIT(&w, "\"");
        }
        PUT_LIT(&w, "}");
    }
    PUT_LIT(&w, "]}");
    return w.err;
}

#undef PUT_LIT
