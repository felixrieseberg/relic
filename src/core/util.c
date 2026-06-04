#include <string.h>
#include "util.h"

int sb_put(char *dst, int cap, int *off, const char *s, int n)
{
    if (n < 0) n = (int)strlen(s);
    if (n > cap - *off) return -1;
    memcpy(dst + *off, s, (size_t)n);
    *off += n;
    return 0;
}

int str_set(char *dst, int cap, const char *src)
{
    int i;
    if (cap <= 0) return 0;
    for (i = 0; i < cap - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return i;
}

void tty_sanitize(char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        /* CR is not whitelisted: it lets a single line overwrite itself on
         * any VT/ANSI terminal, which is exactly the cursor-repositioning
         * this function exists to prevent. */
        if ((c < 0x20 && c != '\n' && c != '\t') || c == 0x7F) s[i] = ' ';
    }
}

unsigned long rd_u32le(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
           | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

void wr_u32le(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

int sb_udec(char *dst, int cap, int *off, unsigned v)
{
    char rev[12];
    int i = 0;
    if (v == 0) {
        rev[i++] = '0';
    } else
        while (v) {
            rev[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
    if (i > cap - *off) return -1;
    while (i)
        dst[(*off)++] = rev[--i];
    return 0;
}
