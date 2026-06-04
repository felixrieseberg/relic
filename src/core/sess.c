#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sess.h"
#include "util.h"
#include "../plat/plat.h"

static char g_dir[96]; /* with trailing separator, or "" */
static int g_id;

int sess_id(void) { return g_id; }
int sess_last(void) /* read RELIC.IDX */
{
    char p[128], buf[16];
    FILE *f;
    int n;
    n = snprintf(p, sizeof p, "%sRELIC.IDX", g_dir);
    if (n < 0 || n >= (int)sizeof p) return 0;
    f = fopen(p, "rb");
    if (!f) return 0;
    n = (int)fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return atoi(buf);
}
const char *sess_dir(void) { return g_dir; }

static int dat_path(char *p, int cap, int id)
{
    int n = snprintf(p, (size_t)cap, "%sRELIC%03d.DAT", g_dir, id);
    return (n < 0 || n >= cap) ? -1 : 0;
}

static void write_idx(int id)
{
    char p[128];
    FILE *f;
    if (snprintf(p, sizeof p, "%sRELIC.IDX", g_dir) >= (int)sizeof p) return;
    f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "%d\n", id);
    fclose(f);
}

/* Try <base>RELIC/ as the session dir: mkdir it, probe writability. */
static int try_dir(const char *base)
{
    char p[128];
    FILE *f;
    int n = snprintf(g_dir, sizeof g_dir, "%sRELIC", base);
    if (n < 0 || n + 2 >= (int)sizeof g_dir) return 0;
    plat_mkdir(g_dir);
    g_dir[n++] = plat_dirsep();
    g_dir[n] = 0;
    if (snprintf(p, sizeof p, "%sRELIC.IDX", g_dir) >= (int)sizeof p) return 0;
    f = fopen(p, "ab");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Pick first writable RELIC/ subdir under cwd, $TEMP, $TMP, $TMPDIR, $HOME.
 * cwd is captured as an absolute path so the Shell tool's chdir() side
 * effect can't strand later session-file opens. */
static int pick_dir(void)
{
    static const char *envs[] = {"TEMP", "TMP", "TMPDIR", "HOME"};
    char base[96];
    int i, n;
    n = plat_getcwd(base, (int)sizeof base - 1);
    if (n) {
        if (base[n - 1] != '/' && base[n - 1] != '\\' && base[n - 1] != ':')
            base[n++] = plat_dirsep();
        base[n] = 0;
        if (try_dir(base)) return 0;
    }
    for (i = 0; i < (int)(sizeof envs / sizeof envs[0]); i++) {
        const char *d = getenv(envs[i]);
        int n = d ? (int)strlen(d) : 0;
        if (!n || n + 2 > (int)sizeof base) continue;
        memcpy(base, d, (size_t)n);
        if (d[n - 1] != '/' && d[n - 1] != '\\' && d[n - 1] != ':')
            base[n++] = plat_dirsep();
        base[n] = 0;
        if (try_dir(base)) return 0;
    }
    g_dir[0] = 0;
    return -1;
}

int sess_open(conv_t *cv, int want)
{
    char path[128];
    int last;
    if (pick_dir() != 0) return -1;
    last = sess_last();
    if (want == 0) {
        if (last >= SESS_MAX) return -2; /* store full */
        g_id = last + 1;
        if (dat_path(path, (int)sizeof path, g_id) != 0) return -1;
        memset(cv, 0, sizeof *cv);
        str_set(cv->path, (int)sizeof cv->path, path);
        return 0;
    }
    if (want < 0) {
        if (last == 0) return -1;
        g_id = last;
    } else {
        g_id = (want > SESS_MAX) ? SESS_MAX : want;
    }
    if (dat_path(path, (int)sizeof path, g_id) != 0) return -1;
    return conv_init(cv, path, 1);
}

int sess_commit(conv_t *cv)
{
    if (cv->fp) return 0;
    write_idx(g_id);
    cv->fp = fopen(cv->path, "w+b");
    return cv->fp ? 0 : -1;
}

int sess_switch(conv_t *cv, int id)
{
    char path[128];
    if (dat_path(path, (int)sizeof path, id) != 0) return -1;
    if (cv->fp) fclose(cv->fp);
    if (conv_init(cv, path, 1) != 0) return -1;
    g_id = id;
    return 0;
}

int sess_peek(int id, char *out, int cap, long *size)
{
    char path[128];
    unsigned char hdr[6];
    FILE *f;
    long len;
    int j, n = 0;
    out[0] = 0;
    *size = 0;
    if (dat_path(path, (int)sizeof path, id) != 0) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0L, SEEK_END) == 0) *size = ftell(f);
    fseek(f, 0L, SEEK_SET);
    if (fread(hdr, 1, 6, f) == 6 && hdr[0] == CONV_ROLE_USER && hdr[1] == 0) {
        len = (long)rd_u32le(hdr + 2);
        if (len < 0 || len > cap - 1) len = cap - 1;
        n = (int)fread(out, 1, (size_t)len, f);
        if (ferror(f)) n = 0;
    }
    fclose(f);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        n--;
    out[n] = 0;
    for (j = 0; j < n; j++)
        if (out[j] == '\n' || out[j] == '\r') out[j] = ' ';
    return 1;
}
