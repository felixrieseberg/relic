#include <string.h>
#include "json_write.h"
#include "util.h"

/* Length of the UTF-8 sequence starting at s (within n bytes), or 0 if the
 * bytes are not valid UTF-8. Loose: accepts overlongs/surrogates -- the goal
 * is structural validity so the API doesn't 400 on a stray CP437 byte. */
static int utf8_seq(const unsigned char *s, int n)
{
    int seq, k;
    unsigned char c = s[0];
    if (c < 0xC2 || c >= 0xF5) return 0;
    seq = (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (seq > n) return 0;
    for (k = 1; k < seq; k++)
        if ((s[k] & 0xC0) != 0x80) return 0;
    return seq;
}

int json_escape_buf(char *dst, int cap, int *off, const char *src, int len)
{
    static const char hex[] = "0123456789abcdef";
    int i = 0;
    while (i < len) {
        unsigned char c;
        char e1;
        int j = i;
        /* Fast path: copy the run of plain ASCII needing no escape. */
        while (j < len && (c = (unsigned char)src[j]) >= 0x20 && c < 0x80
               && c != '"' && c != '\\')
            j++;
        if (j > i) {
            if (sb_put(dst, cap, off, src + i, j - i)) return -1;
            i = j;
            if (i >= len) break;
        }
        c = (unsigned char)src[i];
        if (c >= 0x80) {
            /* Tool output / file contents arrive in the host code page
             * (CP437 on Win9x, MacRoman on classic Mac). Pass valid UTF-8
             * through untouched; replace anything else with U+FFFD so the
             * request body stays valid UTF-8 and the persisted turn can
             * be replayed. */
            int seq = utf8_seq((const unsigned char *)src + i, len - i);
            if (seq) {
                if (sb_put(dst, cap, off, src + i, seq)) return -1;
                i += seq;
            } else {
                if (sb_put(dst, cap, off, "\\ufffd", 6)) return -1;
                i++;
            }
            continue;
        }
        i++;
        switch (c) {
        case '"': e1 = '"'; break;
        case '\\': e1 = '\\'; break;
        case '\b': e1 = 'b'; break;
        case '\f': e1 = 'f'; break;
        case '\n': e1 = 'n'; break;
        case '\r': e1 = 'r'; break;
        case '\t': e1 = 't'; break;
        default: e1 = 0; break;
        }
        if (*off + (e1 ? 2 : 6) > cap) return -1;
        dst[(*off)++] = '\\';
        if (e1) {
            dst[(*off)++] = e1;
        } else {
            dst[(*off)++] = 'u';
            dst[(*off)++] = '0';
            dst[(*off)++] = '0';
            dst[(*off)++] = hex[(c >> 4) & 0xF];
            dst[(*off)++] = hex[c & 0xF];
        }
    }
    return 0;
}

int json_escape_str(char *dst, int cap, int *off, const char *src)
{
    return json_escape_buf(dst, cap, off, src, (int)strlen(src));
}

int json_escape_to(int (*wr)(void *, const char *, int), void *ctx,
                   const char *src, int len)
{
    char esc[256];
    int i;
    for (i = 0; i < len;) {
        int chunk = len - i, off = 0, back = 0;
        if (chunk > (int)sizeof esc / JSON_ESC_WORST)
            chunk = (int)sizeof esc / JSON_ESC_WORST;
        /* Don't split a UTF-8 sequence across chunk boundaries, or
         * json_escape_buf would see each half as invalid. */
        while (back < 3 && chunk > 1 && i + chunk < len
               && ((unsigned char)src[i + chunk] & 0xC0) == 0x80) {
            chunk--;
            back++;
        }
        json_escape_buf(esc, (int)sizeof esc, &off, src + i, chunk);
        if (wr(ctx, esc, off)) return -1;
        i += chunk;
    }
    return 0;
}
