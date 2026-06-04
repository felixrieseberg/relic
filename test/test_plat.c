#include "t.h"
#include "../src/plat/plat.h"

int main(void)
{
    char buf[PLAT_PATH_MAX];
    char dir[PLAT_PATH_MAX];
    unsigned char ent[32];
    int n, i, nz;

    t_group("plat");

    t_int(plat_dirsep(), '/', "dirsep is /");

    n = plat_getcwd(buf, (int)sizeof buf);
    t_ok(n > 0, "getcwd returns length");
    t_ok(buf[0] == '/', "getcwd is absolute");

    /* plat_mkdir doesn't create parents; ensure .work/ exists first. */
    system("rm -rf test/e2e/.work/t_plat && mkdir -p test/e2e/.work");
    snprintf(dir, sizeof dir, "%s/test/e2e/.work/t_plat", buf);
    t_int(plat_mkdir(dir), 0, "mkdir new");
    t_int(plat_mkdir(dir), 0, "mkdir already-exists -> 0");

    n = plat_list_dir("test", buf, (int)sizeof buf);
    t_ok(n > 0, "list_dir returns count");
    t_has(buf, "t.h", "list_dir contains t.h");
    t_has(buf, "e2e/", "list_dir suffixes dirs with /");

    n = plat_entropy(ent, (int)sizeof ent);
    t_int(n, (int)sizeof ent, "entropy fills buffer");
    nz = 0;
    for (i = 0; i < n; i++)
        if (ent[i]) nz++;
    t_ok(nz > 0, "entropy not all-zero");

    t_ok(plat_time_unix() > 1000000000UL, "time_unix is post-2001");

    t_ok(plat_os_desc()[0] != 0, "os_desc non-empty");
    t_ok(plat_shell_tool_name()[0] != 0, "shell_tool_name non-empty");

    return t_done();
}
