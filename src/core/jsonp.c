#include <string.h>
#include "../../third_party/jsmn/jsmn.h" /* implementation, extern linkage */
#include "jsonp.h"

int jsonp_skip(const jsmntok_t *toks, int i)
{
    int pend = 1;
    while (pend > 0) {
        pend += toks[i].size - 1;
        i++;
    }
    return i;
}

int jsonp_child(const jsmntok_t *toks, int obj, const char *js, const char *k)
{
    int i, n, key, kl = (int)strlen(k);
    if (toks[obj].type != JSMN_OBJECT) return -1;
    n = toks[obj].size;
    key = obj + 1;
    for (i = 0; i < n; i++) {
        int val = key + 1;
        if (toks[key].type == JSMN_STRING
            && toks[key].end - toks[key].start == kl
            && memcmp(js + toks[key].start, k, (size_t)kl) == 0)
            return val;
        key = jsonp_skip(toks, val);
    }
    return -1;
}

static int xdigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int jsonp_unescape_span(const char *s, int n, char *dst, int cap)
{
    int i, o = 0;
    if (cap <= 0) return 0;
    for (i = 0; i < n && o < cap - 1; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            char e = s[++i];
            switch (e) {
            case 'n': dst[o++] = '\n'; break;
            case 't': dst[o++] = '\t'; break;
            case 'r': dst[o++] = '\r'; break;
            case 'b': dst[o++] = '\b'; break;
            case 'f': dst[o++] = '\f'; break;
            case '/': dst[o++] = '/'; break;
            case '"': dst[o++] = '"'; break;
            case '\\': dst[o++] = '\\'; break;
            case 'u':
                if (i + 4 < n) {
                    int h0 = xdigit(s[i + 1]), h1 = xdigit(s[i + 2]),
                        h2 = xdigit(s[i + 3]), h3 = xdigit(s[i + 4]);
                    unsigned long u;
                    i += 4;
                    if ((h0 | h1 | h2 | h3) < 0) {
                        dst[o++] = '?';
                        break;
                    }
                    u = (unsigned long)((h0 << 12) | (h1 << 8) | (h2 << 4)
                                        | h3);
                    /* High surrogate followed by \uDC00..DFFF -> compose. */
                    if (u >= 0xD800 && u <= 0xDBFF && i + 6 < n
                        && s[i + 1] == '\\' && s[i + 2] == 'u') {
                        int l0 = xdigit(s[i + 3]), l1 = xdigit(s[i + 4]),
                            l2 = xdigit(s[i + 5]), l3 = xdigit(s[i + 6]);
                        if ((l0 | l1 | l2 | l3) >= 0) {
                            unsigned lo = (unsigned)((l0 << 12) | (l1 << 8)
                                                     | (l2 << 4) | l3);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                i += 6;
                                u = 0x10000UL + ((u - 0xD800) << 10)
                                    + (lo - 0xDC00);
                            }
                        }
                    }
                    if (u == 0) {
                        /* skip: a NUL byte would truncate the C string */
                    } else if (u < 0x80) {
                        dst[o++] = (char)u;
                    } else if (u < 0x800) {
                        if (o + 2 > cap - 1) goto done;
                        dst[o++] = (char)(0xC0 | (u >> 6));
                        dst[o++] = (char)(0x80 | (u & 0x3F));
                    } else if (u >= 0xD800 && u <= 0xDFFF) {
                        dst[o++] = '?'; /* unpaired surrogate */
                    } else if (u < 0x10000) {
                        if (o + 3 > cap - 1) goto done;
                        dst[o++] = (char)(0xE0 | (u >> 12));
                        dst[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
                        dst[o++] = (char)(0x80 | (u & 0x3F));
                    } else {
                        if (o + 4 > cap - 1) goto done;
                        dst[o++] = (char)(0xF0 | (u >> 18));
                        dst[o++] = (char)(0x80 | ((u >> 12) & 0x3F));
                        dst[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
                        dst[o++] = (char)(0x80 | (u & 0x3F));
                    }
                } else
                    dst[o++] = '?';
                break;
            default: dst[o++] = e; break;
            }
        } else
            dst[o++] = c;
    }
done:
    dst[o] = 0;
    /* -1: dst overflowed (still NUL-terminated with what fit). Callers that
     * act on the decoded value (Write, Edit, paths) must treat this as an
     * error rather than silently operating on a truncated string. */
    return (i < n) ? -1 : o;
}

int jsonp_unescape(const char *js, const jsmntok_t *t, char *dst, int cap)
{
    if (!t || t->type != JSMN_STRING) {
        dst[0] = 0;
        return 0;
    }
    return jsonp_unescape_span(js + t->start, t->end - t->start, dst, cap);
}
