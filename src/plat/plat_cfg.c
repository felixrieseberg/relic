/* RELIC.CFG / api-key reader. Pure ANSI C; shared by all platforms.
 * Looks in the user's home dir first (HOME / USERPROFILE / WINDIR), then
 * falls back to RELIC.CFG in the cwd. Per-key: a key absent from the home
 * file is still looked up in the cwd file -- except when the caller passes
 * PLAT_CFG_TRUSTED, which restricts the lookup to the home file only.
 * Security-relevant keys (permission_mode, proxy, host_ip) use the trusted
 * path so a checked-out repo can't ship a RELIC.CFG that silently disables
 * the permission prompt or reroutes traffic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"

/* fgets() that also breaks on a bare CR -- classic Mac text editors save
 * CR-only, and Retro68's newlib stdio does no CR<->LF translation. */
static char *cfg_gets(char *s, int n, FILE *f)
{
    int i = 0, c;
    while (i < n - 1 && (c = getc(f)) != EOF) {
        s[i++] = (char)c;
        if (c == '\n' || c == '\r') break;
    }
    s[i] = 0;
    return i ? s : NULL;
}

/* Must accommodate the largest cap any caller passes (currently
 * sizeof relic_cfg.key == 256) plus "key=" and trailing CR/LF. A short
 * line buffer would split long values mid-stream and drop the tail. */
#define CFG_LINE_MAX 1152
static int cfg_read(const char *path, const char *key, char *out, int cap)
{
    FILE *f = fopen(path, "r");
    static char line[CFG_LINE_MAX];
    int klen = (int)strlen(key);
    if (!f) return 0;
    while (cfg_gets(line, (int)sizeof line, f)) {
        char *p = line;
        int n;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        if (strncmp(p, key, (size_t)klen) != 0 || p[klen] != '=') continue;
        p += klen + 1;
        n = (int)strlen(p);
        while (n > 0
               && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' '))
            n--;
        if (n > cap - 1) n = cap - 1;
        memcpy(out, p, (size_t)n);
        out[n] = 0;
        fclose(f);
        return n;
    }
    fclose(f);
    return 0;
}

/* First existing user-dir env var with room for "/RELIC.CFG", else "". */
static const char *cfg_home(void)
{
    static const char *envs[] = {"HOME", "USERPROFILE", "WINDIR"};
    int i;
    for (i = 0; i < (int)(sizeof envs / sizeof envs[0]); i++) {
        const char *d = getenv(envs[i]);
        if (d && *d && (int)strlen(d) + 12 < PLAT_PATH_MAX) return d;
    }
    return "";
}

int plat_get_cfg(const char *key, char *out, int cap, int flags)
{
    const char *h = cfg_home();
    char p[PLAT_PATH_MAX];
    int n;
    out[0] = 0;
    if (*h && (int)strlen(h) + 12 < (int)sizeof p) {
        int hl = (int)strlen(h);
        strcpy(p, h);
        if (h[hl - 1] != '/' && h[hl - 1] != '\\' && h[hl - 1] != ':')
            p[hl++] = plat_dirsep();
        strcpy(p + hl, "RELIC.CFG");
        n = cfg_read(p, key, out, cap);
        if (n) return n;
    }
    if (flags & PLAT_CFG_TRUSTED) return 0;
    n = cfg_read("RELIC.CFG", key, out, cap);
    if (n) return n;
    /* Final fallback: anchor at plat_getcwd() to build an absolute path.
     * Redundant on hosts whose CRT resolves bare relative names via cwd;
     * required on platforms that don't. */
    if (plat_getcwd(p, (int)sizeof p) && (int)strlen(p) + 12 < (int)sizeof p) {
        int pl = (int)strlen(p);
        if (p[pl - 1] != '/' && p[pl - 1] != '\\' && p[pl - 1] != ':')
            p[pl++] = plat_dirsep();
        strcpy(p + pl, "RELIC.CFG");
        return cfg_read(p, key, out, cap);
    }
    return 0;
}

int plat_get_api_key(char *out, int cap)
{
    const char *e = getenv("ANTHROPIC_API_KEY");
    out[0] = 0;
    if (e && *e) {
        int n = (int)strlen(e);
        if (n >= cap) n = cap - 1;
        memcpy(out, e, (size_t)n);
        out[n] = 0;
        return n;
    }
    return plat_get_cfg("api_key", out, cap, 0);
}
