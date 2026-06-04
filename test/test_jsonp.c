#include "t.h"
#include "../src/core/jsonp.h"

static jsmntok_t toks[128];

static int parse(const char *js)
{
    jsmn_parser p;
    jsmn_init(&p);
    return jsmn_parse(&p, js, (unsigned)strlen(js), toks, 128);
}

int main(void)
{
    char out[128];
    const char *js;
    int n, v;

    t_group("jsonp");

    /* --- jsonp_skip --------------------------------------------------- */
    js = "{\"a\":1,\"b\":[2,3,{\"x\":4}],\"c\":\"s\"}";
    n = parse(js);
    t_ok(n > 0, "skip: fixture parses");
    t_int(jsonp_skip(toks, 0), n, "skip: root object -> ntoks");
    /* tok layout: 0=obj 1="a" 2=1 3="b" 4=[..] 5=2 6=3 7={} 8="x" 9=4
     * 10="c" 11="s" */
    t_int(jsonp_skip(toks, 2), 3, "skip: primitive -> +1");
    t_int(jsonp_skip(toks, 4), 10, "skip: nested array");
    t_int(jsonp_skip(toks, 7), 10, "skip: nested object");

    /* --- jsonp_child -------------------------------------------------- */
    v = jsonp_child(toks, 0, js, "a");
    t_int(v, 2, "child: first key");
    v = jsonp_child(toks, 0, js, "b");
    t_int(v, 4, "child: middle key (after skip)");
    v = jsonp_child(toks, 0, js, "c");
    t_int(v, 11, "child: last key");
    t_int(jsonp_child(toks, 0, js, "nope"), -1, "child: missing -> -1");
    t_int(jsonp_child(toks, 0, js, "ab"), -1, "child: prefix not a match");
    t_int(jsonp_child(toks, 4, js, "x"), -1, "child: array obj -> -1");
    /* nested lookup */
    v = jsonp_child(toks, 7, js, "x");
    t_int(v, 9, "child: nested object key");

    /* --- jsonp_unescape: NULL / wrong type --------------------------- */
    out[0] = 'Z';
    t_int(jsonp_unescape(js, NULL, out, 128), 0, "unescape: NULL token");
    t_str(out, "", "unescape: NULL -> empty");
    t_int(jsonp_unescape(js, &toks[2], out, 128), 0,
          "unescape: non-string token");
    t_str(out, "", "unescape: non-string -> empty");

    /* --- jsonp_unescape: plain string -------------------------------- */
    v = jsonp_child(toks, 0, js, "c");
    t_int(jsonp_unescape(js, &toks[v], out, 128), 1, "unescape: plain len");
    t_str(out, "s", "unescape: plain string");

    /* --- jsonp_unescape_span: every short escape --------------------- */
    {
        const char *s = "a\\n\\t\\r\\b\\f\\/\\\"\\\\z";
        n = jsonp_unescape_span(s, (int)strlen(s), out, 128);
        t_str(out, "a\n\t\r\b\f/\"\\z",
              "unescape: \\n \\t \\r \\b \\f \\/ \\\" \\\\");
        t_int(n, 10, "unescape: short-escape length");
    }

    /* --- jsonp_unescape_span: \\uXXXX -------------------------------- */
    n = jsonp_unescape_span("\\u0041", 6, out, 128);
    t_str(out, "A", "unescape: \\u0041 -> A");
    t_int(n, 1, "unescape: ascii len 1");

    n = jsonp_unescape_span("\\u00e9", 6, out, 128);
    t_str(out, "\xc3\xa9", "unescape: \\u00e9 -> 2-byte UTF-8");
    t_int(n, 2, "unescape: 2-byte len");

    n = jsonp_unescape_span("\\u20AC", 6, out, 128);
    t_str(out, "\xe2\x82\xac", "unescape: \\u20AC -> 3-byte UTF-8 (euro)");
    t_int(n, 3, "unescape: 3-byte len");

    /* surrogate pair: U+1F600 = \uD83D\uDE00 */
    n = jsonp_unescape_span("\\uD83D\\uDE00", 12, out, 128);
    t_str(out, "\xf0\x9f\x98\x80", "unescape: surrogate pair -> 4-byte");
    t_int(n, 4, "unescape: 4-byte len");

    /* unpaired high surrogate -> '?' */
    jsonp_unescape_span("\\uD83Dx", 7, out, 128);
    t_str(out, "?x", "unescape: lone high surrogate -> ?");

    /* unpaired low surrogate -> '?' */
    jsonp_unescape_span("\\uDE00", 6, out, 128);
    t_str(out, "?", "unescape: lone low surrogate -> ?");

    /* bad hex digits -> '?' */
    jsonp_unescape_span("\\u00zz", 6, out, 128);
    t_str(out, "?", "unescape: bad hex -> ?");

    /* truncated \\u -> '?' (then remaining bytes pass through literally) */
    jsonp_unescape_span("\\u00", 4, out, 128);
    t_str(out, "?00", "unescape: short \\u -> ? + tail");

    /* unknown escape -> literal */
    jsonp_unescape_span("\\q", 2, out, 128);
    t_str(out, "q", "unescape: unknown escape -> literal");

    /* --- cap handling ------------------------------------------------- */
    n = jsonp_unescape_span("abcdef", 6, out, 4);
    t_int(n, -1, "unescape: overflow -> -1");
    t_str(out, "abc", "unescape: truncated NUL-terminated");

    n = jsonp_unescape_span("abc", 3, out, 4);
    t_int(n, 3, "unescape: exact fit -> length");

    out[0] = 'Z';
    t_int(jsonp_unescape_span("abc", 3, out, 0), 0, "unescape: cap=0 -> 0");
    t_int(out[0], 'Z', "unescape: cap=0 no write");

    /* multi-byte that won't fit -> stops cleanly before it, signals -1 */
    n = jsonp_unescape_span("a\\u20ACb", 8, out, 3); /* room for "a" + NUL */
    t_int(n, -1, "unescape: multibyte won't fit -> -1");
    t_str(out, "a", "unescape: multibyte won't fit -> drop");

    return t_done();
}
