#include "t.h"
#include "../src/core/sess.h"
#include "../src/core/conv.h"

int main(void)
{
    static conv_t cv;
    char peek[64];
    long sz;
    int rc;

    t_group("sess");

    /* Isolate: run from a scratch dir with TMPDIR=. so pick_dir() lands on
     * ./RELIC/ regardless of how long the absolute cwd is (sess.c caps the
     * cwd-derived path at 96 bytes). */
    system("rm -rf test/e2e/.work/t_sess");
    t_int(system("mkdir -p test/e2e/.work/t_sess"), 0, "scratch dir");
    t_int(chdir("test/e2e/.work/t_sess"), 0, "chdir scratch");
    setenv("TMPDIR", ".", 1);
    unsetenv("TEMP");
    unsetenv("TMP");
    unsetenv("HOME");

    rc = sess_open(&cv, 0);
    t_int(rc, 0, "open fresh");
    if (rc != 0) return t_done();
    t_int(sess_id(), 1, "first id is 1");
    t_int(sess_last(), 0, "IDX not written until commit");
    t_has(sess_dir(), "RELIC/", "dir picked");
    t_has(cv.path, "RELIC001.DAT", "8.3 path");

    t_int(sess_commit(&cv), 0, "commit creates file");
    t_ok(cv.fp != NULL, "fp open after commit");
    t_int(sess_last(), 1, "IDX written");

    t_int(conv_push_text(&cv, CONV_ROLE_USER, "hello world\n"), 0, "push turn");
    fclose(cv.fp);
    cv.fp = NULL;

    t_int(sess_peek(1, peek, (int)sizeof peek, &sz), 1, "peek existing");
    t_str(peek, "hello world", "peek strips newline");
    t_ok(sz > 0, "peek file size");
    t_int(sess_peek(42, peek, (int)sizeof peek, &sz), 0, "peek missing -> 0");

    rc = sess_open(&cv, -1);
    t_int(rc, 0, "open latest");
    t_int(sess_id(), 1, "latest is 1");
    t_int(cv.count, 1, "resume scanned 1 turn");
    if (cv.fp) fclose(cv.fp);

    return t_done();
}
