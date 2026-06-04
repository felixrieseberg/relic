/* Tiny shared helpers. No malloc. */
#ifndef CORE_UTIL_H
#define CORE_UTIL_H

/* Append n bytes of s (n<0 -> strlen) at dst[*off]. -1 on overflow. */
int sb_put(char *dst, int cap, int *off, const char *s, int n);
/* Append unsigned decimal at dst[*off]. -1 on overflow. */
int sb_udec(char *dst, int cap, int *off, unsigned v);
/* Bounded NUL-terminated copy. Always terminates. Returns bytes copied
 * (excluding NUL), truncating if needed. */
int str_set(char *dst, int cap, const char *src);

/* In-place: replace terminal-control bytes (C0 except \n \t, plus DEL)
 * with ' ' so untrusted text can't drive the tty with escape sequences.
 * scroll_out() applies this to everything it prints; call directly only for
 * paths that bypass scroll (errf to stderr, etc.). */
void tty_sanitize(char *s, int n);

/* Read/write a 32-bit little-endian unsigned. */
unsigned long rd_u32le(const unsigned char *p);
void wr_u32le(unsigned char *p, unsigned long v);

#endif
