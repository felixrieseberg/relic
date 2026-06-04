/* JSON parse helpers over jsmn. One implementation TU; callers own their
 * own jsmntok_t arrays. */
#ifndef CORE_JSONP_H
#define CORE_JSONP_H

#define JSMN_HEADER
#include "../../third_party/jsmn/jsmn.h"

/* Index past token i and all its descendants. */
int jsonp_skip(const jsmntok_t *toks, int i);

/* Index of value for key k in object toks[obj], or -1. */
int jsonp_child(const jsmntok_t *toks, int obj, const char *js, const char *k);

/* Unescape a raw JSON-string body (s[0..n), no enclosing quotes) into dst
 * (cap, NUL-terminated). Returns bytes written, or -1 if dst overflowed
 * (dst still holds what fit). */
int jsonp_unescape_span(const char *s, int n, char *dst, int cap);

/* As above, taking a string token *t. 0 and dst="" if NULL/not a string. */
int jsonp_unescape(const char *js, const jsmntok_t *t, char *dst, int cap);

#endif
