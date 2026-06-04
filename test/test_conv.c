#include "t.h"
#include "../src/core/conv.h"

#define P "test_conv_unit.dat"

int main(void)
{
    static conv_t cv;
    static char buf[2048], rd[256];
    long n;

    t_group("conv");

    /* --- init fresh --------------------------------------------------- */
    remove(P);
    t_int(conv_init(&cv, P, 0), 0, "init: fresh w+b");
    t_int(cv.count, 0, "init: count 0");
    t_int(cv.fend, 0, "init: fend 0");
    t_ok(cv.fp != 0, "init: fp open");

    /* --- push_text + read back --------------------------------------- */
    t_int(conv_push_text(&cv, CONV_ROLE_USER, "hello"), 0, "push_text: ok");
    t_int(cv.count, 1, "push_text: count 1");
    t_int(cv.turns[0].role, CONV_ROLE_USER, "push_text: role");
    t_int(cv.turns[0].is_json, 0, "push_text: is_json 0");
    t_int(cv.turns[0].len, 5, "push_text: len");
    t_int(conv_read(&cv, 0, rd, sizeof rd), 5, "read: returns len");
    t_mem(rd, "hello", 5, "read: bytes round-trip");

    /* --- push raw json ------------------------------------------------ */
    t_int(conv_push(&cv, CONV_ROLE_ASST, 1, "[{\"x\":1}]", -1), 0,
          "push: json ok");
    t_int(cv.turns[1].is_json, 1, "push: is_json 1");
    t_int(cv.turns[1].role, CONV_ROLE_ASST, "push: role asst");

    /* --- streaming open/write/commit --------------------------------- */
    t_int(conv_open(&cv, CONV_ROLE_USER, 0), 0, "open: ok");
    t_int(conv_write(&cv, "ab", 2), 0, "write: chunk 1");
    t_int(conv_write(&cv, "cde", -1), 0, "write: chunk 2 (n<0)");
    t_int(conv_write(&cv, "", 0), 0, "write: zero-length no-op");
    t_int(cv.count, 2, "open: not yet committed");
    t_int(conv_commit(&cv), 0, "commit: ok");
    t_int(cv.count, 3, "commit: count 3");
    t_int(cv.turns[2].len, 5, "commit: len summed");
    t_int(conv_read(&cv, 2, rd, sizeof rd), 5, "commit: read len");
    t_mem(rd, "abcde", 5, "commit: body round-trip");

    /* --- conv_bytes --------------------------------------------------- */
    t_int(conv_bytes(&cv), 5 + 9 + 5, "bytes: sum of lens");

    /* --- read: cap too small -> -1 ----------------------------------- */
    t_int(conv_read(&cv, 0, rd, 3), -1, "read: cap < len -> -1");

    /* --- resume: reopen and rescan ----------------------------------- */
    fclose(cv.fp);
    t_int(conv_init(&cv, P, 1), 0, "resume: reopen");
    t_int(cv.count, 3, "resume: count restored");
    t_int(cv.turns[0].len, 5, "resume: turn0 len");
    t_int(cv.turns[1].is_json, 1, "resume: turn1 is_json");
    t_int(conv_read(&cv, 2, rd, sizeof rd), 5, "resume: turn2 read");
    t_mem(rd, "abcde", 5, "resume: turn2 body");

    /* --- resume on missing file -> fresh create ---------------------- */
    fclose(cv.fp);
    remove(P);
    t_int(conv_init(&cv, P, 1), 0, "resume: missing -> create");
    t_int(cv.count, 0, "resume: missing -> empty");

    /* --- reset truncates --------------------------------------------- */
    conv_push_text(&cv, CONV_ROLE_USER, "x");
    conv_reset(&cv);
    t_int(cv.count, 0, "reset: count 0");
    t_int(cv.fend, 0, "reset: fend 0");
    t_int(cv.dropped, 0, "reset: dropped 0");
    t_ok(cv.fp != 0, "reset: fp still open");

    /* --- conv_trim ---------------------------------------------------- */
    conv_reset(&cv);
    conv_push_text(&cv, CONV_ROLE_USER, "0123456789"); /* 10 */
    conv_push(&cv, CONV_ROLE_ASST, 1, "0123456789", 10);
    conv_push_text(&cv, CONV_ROLE_USER, "0123456789");
    conv_push(&cv, CONV_ROLE_ASST, 1, "0123456789", 10);
    t_int(conv_bytes(&cv), 40, "trim: setup bytes");
    t_int(conv_trim(&cv, 50), 0, "trim: under budget -> 0");
    t_int(conv_trim(&cv, 30), 2, "trim: drops a pair");
    t_int(cv.count, 2, "trim: count 2");
    t_int(cv.dropped, 2, "trim: dropped tracked");
    t_int(conv_bytes(&cv), 20, "trim: bytes reduced");
    /* won't go below 2 */
    t_int(conv_trim(&cv, 1), 0, "trim: floor at 2");

    /* --- trim: anchor pinned, orphan tool_result not left at [1] ----- */
    conv_reset(&cv);
    conv_push_text(&cv, CONV_ROLE_USER, "xxxxxxxxxx");   /* u text */
    conv_push(&cv, CONV_ROLE_ASST, 1, "xxxxxxxxxx", 10); /* a json */
    conv_push(&cv, CONV_ROLE_USER, 1, "xxxxxxxxxx", 10); /* u json */
    conv_push(&cv, CONV_ROLE_ASST, 1, "xxxxxxxxxx", 10); /* a json */
    n = conv_trim(&cv, 30);
    /* pair-drop after the anchor removes [1,2]; [3] (asst) survives. */
    t_int(n, 2, "trim: pair after anchor");
    t_ok(cv.turns[0].role == CONV_ROLE_USER && !cv.turns[0].is_json,
         "trim: head stays user-text");
    t_int(cv.turns[1].role, CONV_ROLE_ASST, "trim: [1] is asst");

    /* odd alignment leaves an orphan tool_result at [1] -> shed */
    conv_reset(&cv);
    conv_push_text(&cv, CONV_ROLE_USER, "xxxxxxxxxx");
    conv_push(&cv, CONV_ROLE_USER, 1, "xxxxxxxxxx", 10); /* u json */
    conv_push(&cv, CONV_ROLE_ASST, 1, "xxxxxxxxxx", 10);
    conv_push(&cv, CONV_ROLE_USER, 1, "xxxxxxxxxx", 10); /* u json */
    conv_push(&cv, CONV_ROLE_ASST, 1, "xxxxxxxxxx", 10);
    n = conv_trim(&cv, 45);
    t_int(n, 3, "trim: orphan tool_result dropped");
    t_ok(cv.turns[0].role == CONV_ROLE_USER && !cv.turns[0].is_json,
         "trim: head still user-text");
    t_int(cv.turns[1].role, CONV_ROLE_ASST, "trim: [1] not orphan");

    /* --- CONV_CAP overflow -> oldest dropped in steps ---------------- */
    conv_reset(&cv);
    {
        int i;
        for (i = 0; i < CONV_CAP + 5; i++)
            conv_push_text(&cv, CONV_ROLE_USER, "a");
        t_ok(cv.count <= CONV_CAP, "cap: count <= CONV_CAP");
        t_ok(cv.dropped >= 5, "cap: dropped accounts for overflow");
        t_int(cv.count + cv.dropped, CONV_CAP + 5, "cap: nothing lost");
    }

    /* --- conv_send_request: minimal shape (full assert in test_anth) - */
    conv_reset(&cv);
    conv_push_text(&cv, CONV_ROLE_USER, "hi");
    {
        t_buf m;
        t_buf_init(m, buf);
        t_int(conv_send_request(t_buf_wr, &m, "mdl", 1024, NULL, NULL, &cv), 0,
              "send_request: ok");
        buf[m.n] = 0;
        t_ok(buf[0] == '{' && buf[m.n - 1] == '}', "send_request: braces");
        t_has(buf, "\"model\":\"mdl\"", "send_request: model");
        t_has(buf, "\"max_tokens\":1024", "send_request: max_tokens");
        t_has(buf, "\"messages\":[", "send_request: messages");
        m.cap = 3;
        m.n = 0;
        t_int(conv_send_request(t_buf_wr, &m, "mdl", 1, NULL, NULL, &cv), -1,
              "send_request: wr fail -> -1");
    }

    /* --- scan stops at corruption ------------------------------------ */
    conv_reset(&cv);
    conv_push_text(&cv, CONV_ROLE_USER, "good");
    /* append garbage past fend */
    fseek(cv.fp, 0, SEEK_END);
    fwrite("\xFF\xFF\xFF\xFF\xFF\xFF\xFFjunk", 1, 11, cv.fp);
    fflush(cv.fp);
    fclose(cv.fp);
    t_int(conv_init(&cv, P, 1), 0, "scan: reopen with junk");
    t_int(cv.count, 1, "scan: stops at bad header");
    t_int(cv.turns[0].len, 4, "scan: good record kept");
    /* next push overwrites the junk (fend at last good boundary) */
    t_int(conv_push_text(&cv, CONV_ROLE_ASST, "ok"), 0,
          "scan: push after junk");
    t_int(cv.count, 2, "scan: appended");

    fclose(cv.fp);
    remove(P);
    return t_done();
}
