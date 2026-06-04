#include "t.h"
#include "../src/core/util.h"

int main(void)
{
    char buf[16];
    int off;
    unsigned char b4[4];

    t_group("util");

    /* --- sb_put ------------------------------------------------------- */
    off = 0;
    t_int(sb_put(buf, 16, &off, "abc", 3), 0, "sb_put: basic append");
    t_int(off, 3, "sb_put: offset advanced");
    t_int(sb_put(buf, 16, &off, "de", 2), 0, "sb_put: second append");
    buf[off] = 0;
    t_str(buf, "abcde", "sb_put: bytes correct");

    off = 0;
    t_int(sb_put(buf, 16, &off, "hello", -1), 0, "sb_put: n<0 -> strlen");
    t_int(off, 5, "sb_put: strlen offset");

    off = 0;
    t_int(sb_put(buf, 4, &off, "abcd", 4), 0, "sb_put: exact fit");
    t_int(off, 4, "sb_put: exact fit offset");
    t_int(sb_put(buf, 4, &off, "x", 1), -1, "sb_put: overflow -> -1");
    t_int(off, 4, "sb_put: overflow leaves offset");

    off = 0;
    t_int(sb_put(buf, 4, &off, "abcde", 5), -1, "sb_put: too big up front");
    t_int(off, 0, "sb_put: nothing written on fail");

    off = 3;
    t_int(sb_put(buf, 16, &off, "", 0), 0, "sb_put: zero-length ok");
    t_int(off, 3, "sb_put: zero-length no-op");

    /* --- sb_udec ------------------------------------------------------ */
    off = 0;
    t_int(sb_udec(buf, 16, &off, 0), 0, "sb_udec: zero");
    buf[off] = 0;
    t_str(buf, "0", "sb_udec: zero -> \"0\"");

    off = 0;
    t_int(sb_udec(buf, 16, &off, 7), 0, "sb_udec: single digit");
    buf[off] = 0;
    t_str(buf, "7", "sb_udec: \"7\"");

    off = 0;
    t_int(sb_udec(buf, 16, &off, 12345), 0, "sb_udec: multi-digit");
    buf[off] = 0;
    t_str(buf, "12345", "sb_udec: \"12345\"");

    off = 0;
    t_int(sb_udec(buf, 16, &off, 4294967295U), 0, "sb_udec: UINT_MAX");
    buf[off] = 0;
    t_str(buf, "4294967295", "sb_udec: \"4294967295\"");

    off = 0;
    t_int(sb_udec(buf, 3, &off, 99), 0, "sb_udec: exact fit (2 in cap 3)");
    t_int(sb_udec(buf, 3, &off, 9), 0, "sb_udec: exact fit (1 more)");
    buf[3] = 0; /* deliberately past cap; buf is 16 */
    t_int(off, 3, "sb_udec: filled cap");

    off = 0;
    t_int(sb_udec(buf, 2, &off, 100), -1, "sb_udec: overflow -> -1");
    t_int(off, 0, "sb_udec: overflow leaves offset");

    /* --- str_set ------------------------------------------------------ */
    memset(buf, 'X', sizeof buf);
    t_int(str_set(buf, 16, "abc"), 3, "str_set: returns length");
    t_str(buf, "abc", "str_set: copies + NUL");

    memset(buf, 'X', sizeof buf);
    t_int(str_set(buf, 4, "abcdef"), 3, "str_set: truncate to cap-1");
    t_str(buf, "abc", "str_set: truncated + NUL");
    t_int(buf[4], 'X', "str_set: no write past cap");

    memset(buf, 'X', sizeof buf);
    t_int(str_set(buf, 1, "abc"), 0, "str_set: cap=1 -> empty");
    t_int(buf[0], 0, "str_set: cap=1 NUL only");

    buf[0] = 'X';
    t_int(str_set(buf, 0, "abc"), 0, "str_set: cap=0 -> 0");
    t_int(buf[0], 'X', "str_set: cap=0 no write");

    t_int(str_set(buf, 16, ""), 0, "str_set: empty src");
    t_str(buf, "", "str_set: empty -> \"\"");

    /* --- rd_u32le / wr_u32le ----------------------------------------- */
    b4[0] = 0x78;
    b4[1] = 0x56;
    b4[2] = 0x34;
    b4[3] = 0x12;
    t_int(rd_u32le(b4), 0x12345678UL, "rd_u32le: known bytes");

    wr_u32le(b4, 0xDEADBEEFUL);
    t_int(b4[0], 0xEF, "wr_u32le: byte 0");
    t_int(b4[1], 0xBE, "wr_u32le: byte 1");
    t_int(b4[2], 0xAD, "wr_u32le: byte 2");
    t_int(b4[3], 0xDE, "wr_u32le: byte 3");
    t_int(rd_u32le(b4), 0xDEADBEEFUL, "rd/wr u32le: round-trip");

    wr_u32le(b4, 0);
    t_int(rd_u32le(b4), 0, "rd/wr u32le: zero");
    wr_u32le(b4, 0xFFFFFFFFUL);
    t_int(rd_u32le(b4), 0xFFFFFFFFUL, "rd/wr u32le: max");

    /* --- tty_sanitize ------------------------------------------------- */
    {
        char s[] = "ok\x1b[2Kbad\x07\n\ttab\r\x7f.";
        tty_sanitize(s, (int)sizeof s - 1);
        t_str(s, "ok [2Kbad \n\ttab  .", "tty_sanitize: ESC/BEL/CR/DEL -> ' '");
    }
    {
        char s[] = "plain ascii";
        tty_sanitize(s, (int)sizeof s - 1);
        t_str(s, "plain ascii", "tty_sanitize: clean unchanged");
    }
    {
        char s[] = "\xE2\x82\xAC"; /* U+20AC */
        tty_sanitize(s, 3);
        t_mem(s, "\xE2\x82\xAC", 3, "tty_sanitize: UTF-8 high bytes pass");
    }

    return t_done();
}
