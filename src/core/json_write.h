#ifndef CORE_JSON_WRITE_H
#define CORE_JSON_WRITE_H

/* Worst-case expansion of one input byte ("\\uXXXX"). */
#define JSON_ESC_WORST 6

/* Append JSON-escaped src into dst at *off (cap total). Returns 0 / -1. */
int json_escape_str(char *dst, int cap, int *off, const char *src);
int json_escape_buf(char *dst, int cap, int *off, const char *src, int len);

/* Stream JSON-escaped src via wr(ctx, chunk, n) using a small stack buffer.
 * Returns 0, or -1 if any wr() call fails. */
int json_escape_to(int (*wr)(void *, const char *, int), void *ctx,
                   const char *src, int len);

#endif
