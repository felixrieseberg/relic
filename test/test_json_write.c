#include "t.h"
#include "../src/core/json_write.h"
#include "../src/core/jsonp.h"

static int sink_fail(void *ctx, const char *s, int len)
{
    (void)ctx;
    (void)s;
    (void)len;
    return -1;
}

int main(void)
{
    char buf[512];
    int off;

    t_group("jwrite");

    /* --- json_escape_str: pass-through ------------------------------- */
    off = 0;
    t_int(json_escape_str(buf, 512, &off, "hello world"), 0, "esc: plain ok");
    buf[off] = 0;
    t_str(buf, "hello world", "esc: plain unchanged");

    /* --- every short escape ------------------------------------------ */
    off = 0;
    t_int(json_escape_str(buf, 512, &off, "\"\\\b\f\n\r\t"), 0,
          "esc: short escapes ok");
    buf[off] = 0;
    t_str(buf, "\\\"\\\\\\b\\f\\n\\r\\t", "esc: \" \\ b f n r t");

    /* --- control chars -> \\u00XX ------------------------------------ */
    off = 0;
    t_int(json_escape_buf(buf, 512, &off, "\x01\x1f", 2), 0, "esc: ctrl ok");
    buf[off] = 0;
    t_str(buf, "\\u0001\\u001f", "esc: \\u0001 \\u001f");

    /* --- embedded NUL via _buf --------------------------------------- */
    off = 0;
    t_int(json_escape_buf(buf, 512, &off, "a\0b", 3), 0, "esc: embedded NUL");
    buf[off] = 0;
    t_str(buf, "a\\u0000b", "esc: NUL -> \\u0000");

    /* --- high bytes pass through (UTF-8 stays raw) ------------------- */
    off = 0;
    t_int(json_escape_str(buf, 512, &off, "\xc3\xa9"), 0, "esc: utf8 ok");
    buf[off] = 0;
    t_str(buf, "\xc3\xa9", "esc: high bytes verbatim");

    /* --- mixed run (fast-path span + escape + span) ------------------ */
    off = 0;
    t_int(json_escape_str(buf, 512, &off, "abc\ndef\"ghi"), 0, "esc: mixed ok");
    buf[off] = 0;
    t_str(buf, "abc\\ndef\\\"ghi", "esc: mixed runs");

    /* --- overflow: short escape doesn't fit -------------------------- */
    off = 0;
    t_int(json_escape_str(buf, 1, &off, "\n"), -1, "esc: overflow short -> -1");
    /* --- overflow: \\u00XX needs 6 ----------------------------------- */
    off = 0;
    t_int(json_escape_buf(buf, 5, &off, "\x01", 1), -1,
          "esc: overflow \\u -> -1");
    off = 0;
    t_int(json_escape_buf(buf, 6, &off, "\x01", 1), 0, "esc: \\u exact fit");
    /* --- overflow: plain run ----------------------------------------- */
    off = 0;
    t_int(json_escape_str(buf, 3, &off, "abcd"), -1, "esc: overflow plain");

    /* --- exact fit at cap -------------------------------------------- */
    off = 0;
    t_int(json_escape_str(buf, 4, &off, "ab\n"), 0, "esc: exact fit");
    t_int(off, 4, "esc: exact fit offset");

    /* --- empty input ------------------------------------------------- */
    off = 5;
    t_int(json_escape_buf(buf, 512, &off, "", 0), 0, "esc: empty ok");
    t_int(off, 5, "esc: empty no-op");

    /* --- json_escape_to: streamed, multi-chunk ----------------------- */
    {
        static char src[300], dst[2048];
        t_buf m;
        int i;
        for (i = 0; i < (int)sizeof src; i++)
            src[i] = (i % 7 == 0) ? '\n' : (char)('a' + (i % 26));
        t_buf_init(m, dst);
        t_int(json_escape_to(t_buf_wr, &m, src, (int)sizeof src), 0,
              "esc_to: stream ok");
        /* Compare against the non-streaming encoder. */
        off = 0;
        json_escape_buf(buf, 512, &off, src, (int)sizeof src);
        t_int(m.n, off, "esc_to: same length as _buf");
        t_mem(dst, buf, off, "esc_to: same bytes as _buf");
    }

    /* --- json_escape_to: sink failure propagates --------------------- */
    t_int(json_escape_to(sink_fail, 0, "x", 1), -1, "esc_to: wr fail -> -1");

    /* --- UTF-8: valid sequences pass, invalid -> \\ufffd ------------- */
    off = 0;
    json_escape_buf(buf, 512, &off, "a\xE2\x82\xAC z", 6);
    buf[off] = 0;
    t_str(buf, "a\xE2\x82\xAC z", "esc: valid UTF-8 passes through");
    off = 0;
    json_escape_buf(buf, 512, &off, "a\xC9 z", 4); /* lone CP437 0xC9 */
    buf[off] = 0;
    t_str(buf, "a\\ufffd z", "esc: stray high byte -> U+FFFD");
    off = 0;
    json_escape_buf(buf, 512, &off, "\x80\x81", 2); /* orphan continuation */
    buf[off] = 0;
    t_str(buf, "\\ufffd\\ufffd", "esc: orphan continuation -> U+FFFD");
    off = 0;
    json_escape_buf(buf, 512, &off, "\xF0\x9F\x98\x80", 4); /* U+1F600 */
    buf[off] = 0;
    t_mem(buf, "\xF0\x9F\x98\x80", 4, "esc: 4-byte UTF-8 passes");

    /* --- json_escape_to: chunk boundary mustn't split UTF-8 ---------- */
    {
        static char src[100], dst[1024];
        t_buf m;
        int i;
        /* Fill with 3-byte sequences so every chunk boundary lands
         * mid-sequence at some i. */
        for (i = 0; i + 3 <= (int)sizeof src; i += 3) {
            src[i] = '\xE2';
            src[i + 1] = '\x82';
            src[i + 2] = '\xAC';
        }
        t_buf_init(m, dst);
        json_escape_to(t_buf_wr, &m, src, i);
        t_int(m.n, i, "esc_to: UTF-8 not split at chunk boundary");
        t_mem(dst, src, i, "esc_to: UTF-8 bytes intact");
    }

    /* --- round-trip: escape then unescape == identity ---------------- */
    {
        const char *s = "line1\nline2\t\"q\"\\ end \x01.";
        char dec[128];
        off = 0;
        json_escape_str(buf, 512, &off, s);
        jsonp_unescape_span(buf, off, dec, 128);
        t_str(dec, s, "round-trip: escape -> unescape");
    }

    return t_done();
}
