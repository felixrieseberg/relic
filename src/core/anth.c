#include <stdio.h>
#include <string.h>
#include "anth.h"
#include "util.h"
#include "jsonp.h"

/* jsmn token budget. The /v1/messages response shape is shallow: a handful of
 * fixed top-level keys, a usage object, and content[] of <=ANTH_MAX_BLOCKS
 * blocks whose tool_use `input` objects carry at most a few string fields
 * (our tool schemas are tiny). /v1/models -- whose data[] is unbounded -- is
 * handled by a flat string scan instead, so jsmn never sees it. */
#define MAX_TOK 512

static jsmntok_t TOK[MAX_TOK];

static int tok_streq(const jsmntok_t *t, const char *js, const char *s)
{
    int n = t->end - t->start;
    return t->type == JSMN_STRING && (int)strlen(s) == n
           && memcmp(js + t->start, s, (size_t)n) == 0;
}

static void unesc(const jsmntok_t *tk, int v, const char *js, char *dst,
                  int cap)
{
    jsonp_unescape(js, v >= 0 ? &tk[v] : 0, dst, cap);
}

int anth_parse(const char *body, int blen, anth_result *r)
{
    jsmn_parser p;
    int n, v, i, nb, blk;

    memset(r, 0, sizeof *r);
    jsmn_init(&p);
    n = jsmn_parse(&p, body, (size_t)blen, TOK, MAX_TOK);
    if (n < 1 || TOK[0].type != JSMN_OBJECT) return -1;

    v = jsonp_child(TOK, 0, body, "type");
    if (v >= 0 && tok_streq(&TOK[v], body, "error")) {
        int e = jsonp_child(TOK, 0, body, "error");
        int m = (e >= 0) ? jsonp_child(TOK, e, body, "message") : -1;
        r->is_error = 1;
        if (m >= 0)
            unesc(TOK, m, body, r->err_msg, (int)sizeof r->err_msg);
        else
            strcpy(r->err_msg, "unknown API error");
        return 0;
    }

    v = jsonp_child(TOK, 0, body, "stop_reason");
    unesc(TOK, v, body, r->stop_reason, (int)sizeof r->stop_reason);

    v = jsonp_child(TOK, 0, body, "content");
    if (v < 0 || TOK[v].type != JSMN_ARRAY) return -1;
    r->content_json = body + TOK[v].start;
    r->content_json_len = TOK[v].end - TOK[v].start;

    nb = TOK[v].size;
    if (nb > ANTH_MAX_BLOCKS) {
        /* Truncating here would replay tool_use blocks we never produced
         * tool_result entries for and 400 the next request. Surface as an
         * error instead so the session stays consistent. */
        r->is_error = 1;
        snprintf(r->err_msg, sizeof r->err_msg,
                 "response has %d content blocks (cap %d)", nb,
                 ANTH_MAX_BLOCKS);
        return 0;
    }
    blk = v + 1;
    for (i = 0; i < nb; i++) {
        int ty = jsonp_child(TOK, blk, body, "type");
        struct anth_block *b = &r->blocks[r->nblocks];
        if (ty >= 0 && tok_streq(&TOK[ty], body, "text")) {
            int tx = jsonp_child(TOK, blk, body, "text");
            if (tx >= 0 && TOK[tx].type == JSMN_STRING) {
                b->kind = ANTH_BLK_TEXT;
                b->body = body + TOK[tx].start;
                b->body_len = TOK[tx].end - TOK[tx].start;
                r->nblocks++;
            }
        } else if (ty >= 0 && tok_streq(&TOK[ty], body, "tool_use")) {
            int in = jsonp_child(TOK, blk, body, "input");
            b->kind = ANTH_BLK_TOOL;
            unesc(TOK, jsonp_child(TOK, blk, body, "id"), body, b->id,
                  (int)sizeof b->id);
            unesc(TOK, jsonp_child(TOK, blk, body, "name"), body, b->name,
                  (int)sizeof b->name);
            if (in >= 0) {
                b->body = body + TOK[in].start;
                b->body_len = TOK[in].end - TOK[in].start;
            } else {
                b->body = "{}";
                b->body_len = 2;
            }
            r->nblocks++;
        }
        blk = jsonp_skip(TOK, blk);
    }
    return 0;
}

#ifdef ANTH_TEST_HELPERS
int anth_parse_response(const char *body, int blen, char *out, int cap)
{
    anth_result r;
    int i;
    out[0] = 0;
    if (anth_parse(body, blen, &r) != 0) return -1;
    if (r.is_error) {
        str_set(out, cap, r.err_msg);
        return 1;
    }
    for (i = 0; i < r.nblocks; i++)
        if (r.blocks[i].kind == ANTH_BLK_TEXT) {
            jsonp_unescape_span(r.blocks[i].body, r.blocks[i].body_len, out,
                                cap);
            return 0;
        }
    return -1;
}
#endif

/* /v1/models is display-only and the response can carry thousands of jsmn
 * tokens (deep per-model `capabilities` trees), so do a flat string scan for
 * the two fields we actually print instead of tokenising the whole thing. */
static int scan_qstr(const char *body, int blen, int *pos, const char *key,
                     char *dst, int cap)
{
    int kl = (int)strlen(key), i = *pos, o = 0;
    for (; i + kl + 3 <= blen; i++)
        if (body[i] == '"' && memcmp(body + i + 1, key, (size_t)kl) == 0
            && body[i + 1 + kl] == '"')
            break;
    if (i + kl + 3 > blen) return -1;
    i += kl + 2; /* past `"key"` */
    while (i < blen && body[i] != '"')
        i++;
    i++; /* opening quote of value */
    while (i < blen && body[i] != '"') {
        if (body[i] == '\\' && i + 1 < blen) i++;
        if (o < cap - 1) dst[o++] = body[i];
        i++;
    }
    dst[o] = 0;
    *pos = i;
    return o;
}

int anth_parse_models(const char *body, int blen, char *out, int cap)
{
    int pos = 0, off = 0, count = 0;
    char id[64], dn[96];
    out[0] = 0;
    if (scan_qstr(body, blen, &pos, "type", id, (int)sizeof id) >= 0
        && strcmp(id, "error") == 0) {
        pos = 0;
        if (scan_qstr(body, blen, &pos, "message", out, cap) < 0)
            str_set(out, cap, "unknown API error");
        return -1;
    }
    pos = 0;
    while (scan_qstr(body, blen, &pos, "id", id, (int)sizeof id) >= 0) {
        int dpos = pos, n;
        dn[0] = 0;
        scan_qstr(body, blen, &dpos, "display_name", dn, (int)sizeof dn);
        n = snprintf(out + off, (size_t)(cap - off), "  %-36s  %s\n", id, dn);
        if (n < 0 || n >= cap - off) break; /* truncated; drop partial line */
        off += n;
        count++;
    }
    out[off] = 0;
    return count;
}
