#include "textfb.h"
#include "font8x16.h"
#include <cstring>

void text_fill(struct fb *F, int x, int y, int w, int h, uint32_t bg) {
    if (F->bpp != 32) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > F->w) w = F->w - x;
    if (y + h > F->h) h = F->h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        uint32_t *d = (uint32_t *)(F->mem + (long)(y + j) * F->stride) + x;
        for (int i = 0; i < w; i++) d[i] = bg;
    }
}

/* Font is ASCII 32..126 only. Decode one UTF-8 codepoint, fold the handful of
 * punctuation marks LLMs love (em/en dash, curly quotes, ellipsis, nbsp, bullet)
 * to ASCII, and degrade anything else to a single '?'. Returns bytes consumed. */
static int next_glyph(const unsigned char *s, int *out) {
    unsigned c = s[0];
    if (c < 0x80) { *out = (c < 32 || c == 0x7f) ? '?' : (int)c; return 1; }
    unsigned cp; int n;
    if      ((c & 0xe0) == 0xc0 && s[1]) { cp = ((c & 0x1f) << 6)  |  (s[1] & 0x3f); n = 2; }
    else if ((c & 0xf0) == 0xe0 && s[1] && s[2])
        { cp = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f); n = 3; }
    else if ((c & 0xf8) == 0xf0 && s[1] && s[2] && s[3])
        { cp = ((c & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f); n = 4; }
    else { *out = '?'; return 1; }           /* malformed: eat one byte */
    switch (cp) {
        case 0x00a0: *out = ' ';  break;     /* nbsp */
        case 0x2010: case 0x2011: case 0x2012: case 0x2013:
        case 0x2014: case 0x2015: case 0x2212:
                     *out = '-';  break;     /* hyphens, en/em dash, minus */
        case 0x2018: case 0x2019: case 0x201a: case 0x2032:
                     *out = '\''; break;     /* curly/low single quote, prime */
        case 0x201c: case 0x201d: case 0x201e: case 0x2033:
                     *out = '"';  break;     /* curly/low double quote, dprime */
        case 0x2022: case 0x00b7: case 0x2219:
                     *out = '*';  break;     /* bullet, middot */
        case 0x2026: *out = '.';  break;     /* ellipsis -> single dot */
        default:     *out = '?';  break;
    }
    return n;
}

int text_draw(struct fb *F, int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    if (F->bpp != 32 || y < 0 || y + TEXT_CH > F->h) return 0;
    int x0 = x;
    while (*s) {
        int c; s += next_glyph((const unsigned char *)s, &c);
        if (x < 0) { x += TEXT_CW; continue; }
        if (x + TEXT_CW > F->w) break;
        const uint8_t *g = font8x16[c - 32];
        for (int j = 0; j < TEXT_CH; j++) {
            uint32_t *d = (uint32_t *)(F->mem + (long)(y + j) * F->stride) + x;
            uint8_t row = g[j];
            for (int i = 0; i < TEXT_CW; i++)
                d[i] = (row & (0x80 >> i)) ? fg : bg;
        }
        x += TEXT_CW;
    }
    return x - x0;
}
