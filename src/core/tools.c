#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include "tools.h"
#include "jsonp.h"
#include "json_write.h"
#include "../plat/plat.h"

#define OUT_CAP_TRUNC_MSG "\n...(truncated)\n"

/* snprintf an error message into out[cap] and return 1 (tool-level error). */
static int errp(char *out, int cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out, (size_t)cap, fmt, ap);
    va_end(ap);
    return 1;
}

/* Index 0 (TOOL_SHELL) is filled from plat_shell_tool_name() at first use. */
static const char *TOOL_NAMES[NTOOLS] = {NULL,   "Read", "Write", "LS",
                                         "Grep", "Edit", "Glob"};

static const char TOOLS_JSON_FMT[] =
    "["
    "{\"name\":\"%s\","
    "\"description\":\"Run a command via the operating system shell and return "
    "its combined output. %s\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"command\":{\"type\":\"string\"}},\"required\":[\"command\"]}},"
    "{\"name\":\"Read\","
    "\"description\":\"Read a text file. Output is cat -n style: each line "
    "prefixed with its 1-based line number and a tab. offset is the line to "
    "start at (default 1); limit caps the number of lines returned. Output "
    "is capped at ~16 KB.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"file_path\":{\"type\":\"string\"},\"offset\":{\"type\":\"integer\"},"
    "\"limit\":{\"type\":\"integer\"}},\"required\":[\"file_path\"]}},"
    "{\"name\":\"Write\","
    "\"description\":\"Create or overwrite a text file with the given content. "
    "Parent directories are created.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"file_path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},"
    "\"required\":[\"file_path\",\"content\"]}},"
    "{\"name\":\"LS\","
    "\"description\":\"List entries in a directory.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"Grep\","
    "\"description\":\"Recursively search text files under path (default '.') "
    "for a literal substring. Returns 'file:line: text' per match. Not a "
    "regex. Skips binaries; depth<=8.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"pattern\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"}},"
    "\"required\":[\"pattern\"]}},"
    "{\"name\":\"Edit\","
    "\"description\":\"Perform an exact string replacement in a file. "
    "old_string must match exactly (including whitespace) and appear exactly "
    "once unless replace_all is true. Use Read first if unsure of the current "
    "contents.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"file_path\":{\"type\":\"string\"},"
    "\"old_string\":{\"type\":\"string\"},"
    "\"new_string\":{\"type\":\"string\"},"
    "\"replace_all\":{\"type\":\"boolean\"}},"
    "\"required\":[\"file_path\",\"old_string\",\"new_string\"]}},"
    "{\"name\":\"Glob\","
    "\"description\":\"Find files by name. Recursively walks under path "
    "(default '.') and returns each relative path that matches pattern. "
    "Wildcards: '*' matches any run of characters including '/', '?' matches "
    "one character. Examples: '*.c', 'src/*.h', '*foo*'. Depth<=8.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"pattern\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"}},"
    "\"required\":[\"pattern\"]}}"
    "]";

/* TOOLS_JSON_FMT plus headroom for the platform-supplied shell name + hint
 * (wii's Lua hint is the longest at ~870 bytes). Overflow is caught at
 * init below. */
static char g_tools_json[sizeof TOOLS_JSON_FMT + 1024];

static void tools_init(void)
{
    /* Shell hint is platform-supplied and embedded in a JSON string value:
     * any backslash or quote in it must be JSON-escaped first. */
    char hint_esc[1024];
    const char *hint;
    int eoff = 0, n;
    if (TOOL_NAMES[TOOL_SHELL]) return;
    TOOL_NAMES[TOOL_SHELL] = plat_shell_tool_name();
    hint = plat_shell_hint();
    if (json_escape_str(hint_esc, (int)sizeof hint_esc, &eoff, hint) != 0
        || eoff >= (int)sizeof hint_esc) {
        fprintf(stderr, "tools_init: shell hint too long for escape buffer\n");
        g_tools_json[0] = 0;
        return;
    }
    hint_esc[eoff] = 0;
    n = snprintf(g_tools_json, sizeof g_tools_json, TOOLS_JSON_FMT,
                 TOOL_NAMES[TOOL_SHELL], hint_esc);
    if (n < 0 || n >= (int)sizeof g_tools_json) {
        fprintf(stderr, "tools_init: tool JSON exceeds %d bytes\n",
                (int)sizeof g_tools_json);
        g_tools_json[0] = 0; /* tools_json() returns NULL -> field omitted */
    }
}

const char *tools_json(void)
{
    tools_init();
    return g_tools_json[0] ? g_tools_json : NULL;
}

const char *tool_name(int idx)
{
    tools_init();
    return (idx >= 0 && idx < NTOOLS) ? TOOL_NAMES[idx] : "?";
}

int tool_index(const char *name)
{
    int i;
    tools_init();
    for (i = 0; i < NTOOLS; i++)
        if (strcmp(name, TOOL_NAMES[i]) == 0) return i;
    return -1;
}

/* ---- tiny field extractor over the input JSON span ---- */
#define MAXT 64

/* Unescape string value of object key k into dst (cap). Returns bytes (>=0),
 * -1 if absent / not a string, -2 if value overflows dst. */
static int field_full(const jsmntok_t *tk, const char *js, const char *k,
                      char *dst, int cap)
{
    int v = jsonp_child(tk, 0, js, k), n;
    dst[0] = 0;
    if (v < 0 || tk[v].type != JSMN_STRING) return -1;
    n = jsonp_unescape(js, &tk[v], dst, cap);
    return (n < 0) ? -2 : n;
}

/* As field_full() but tolerates overflow (returns the truncated length).
 * Used for previews where clipping is expected. */
static int field(const jsmntok_t *tk, const char *js, const char *k, char *dst,
                 int cap)
{
    int n = field_full(tk, js, k, dst, cap);
    return (n == -2) ? cap - 1 : n;
}

static int field_bool(const jsmntok_t *tk, const char *js, const char *k,
                      int dflt)
{
    int v = jsonp_child(tk, 0, js, k);
    if (v < 0 || tk[v].type != JSMN_PRIMITIVE) return dflt;
    return js[tk[v].start] == 't';
}

static int field_int(const jsmntok_t *tk, const char *js, const char *k,
                     int dflt)
{
    int v = jsonp_child(tk, 0, js, k);
    int n = 0, neg = 0, i, start;
    if (v < 0 || tk[v].type != JSMN_PRIMITIVE) return dflt;
    i = start = tk[v].start;
    if (js[i] == '-') {
        neg = 1;
        i++;
        start++;
    }
    for (; i < tk[v].end && js[i] >= '0' && js[i] <= '9'; i++) {
        if (n > (INT_MAX - 9) / 10) {
            n = INT_MAX;
            break;
        }
        n = n * 10 + (js[i] - '0');
    }
    return (i == start) ? dflt : (neg ? -n : n);
}

/* ---- compact human preview for permission prompt -------------------- */

#define PV_COL 68

/* First line of s[0..slen) into dst, clipped to PV_COL. Writes a suffix into
 * more[] describing what's hidden ("" if nothing, " ..." if same-line spill,
 * " (+N more lines)" if multiline) so the y/n prompt can't make a multi-line
 * shell command look like its first line. */
static void pv_line(const char *s, int slen, char *dst, char *more)
{
    int i, nl = 0;
    for (i = 0; i < slen && i < PV_COL && s[i] != '\n' && s[i] != '\r'; i++)
        dst[i] = ((unsigned char)s[i] < 0x20 || s[i] == 0x7F) ? ' ' : s[i];
    dst[i] = 0;
    more[0] = 0;
    if (i >= slen) return;
    for (; i < slen; i++)
        if (s[i] == '\n') nl++;
    if (nl)
        snprintf(more, 24, " (+%d more line%s)", nl, nl == 1 ? "" : "s");
    else
        strcpy(more, " ...");
}

int tool_preview(int idx, const char *input, int ilen, char *out, int cap)
{
    jsmntok_t tk[MAXT];
    char p[256], a[256], b[256];
    char la[PV_COL + 1], lb[PV_COL + 1], ma[24], mb[24];
    jsmn_parser jp;
    int al, bl, n = 0;
    out[0] = 0;
    jsmn_init(&jp);
    if (jsmn_parse(&jp, input, (size_t)ilen, tk, MAXT) < 1) return 0;
    switch (idx) {
    case TOOL_SHELL:
        al = field(tk, input, "command", a, (int)sizeof a);
        if (al < 0) return 0;
        pv_line(a, al, la, ma);
        n = snprintf(out, (size_t)cap, "$ %s%s\n", la, ma);
        break;
    case TOOL_WRITE:
        if (field(tk, input, "file_path", p, (int)sizeof p) < 0) return 0;
        al = field(tk, input, "content", a, (int)sizeof a);
        if (al < 0) return 0;
        pv_line(a, al, la, ma);
        n = snprintf(out, (size_t)cap, "%.64s\n> %s%s\n", p, la, ma);
        break;
    case TOOL_EDIT:
        if (field(tk, input, "file_path", p, (int)sizeof p) < 0) return 0;
        al = field(tk, input, "old_string", a, (int)sizeof a);
        bl = field(tk, input, "new_string", b, (int)sizeof b);
        if (al < 0 || bl < 0) return 0;
        pv_line(a, al, la, ma);
        pv_line(b, bl, lb, mb);
        n = snprintf(out, (size_t)cap, "%.64s%s\n- %s%s\n+ %s%s\n", p,
                     field_bool(tk, input, "replace_all", 0) ? "  (all)" : "",
                     la, ma, lb, mb);
        break;
    case TOOL_READ:
        if (field(tk, input, "file_path", p, (int)sizeof p) < 0) return 0;
        n = snprintf(out, (size_t)cap, "%.72s\n", p);
        break;
    case TOOL_LIST:
        if (field(tk, input, "path", p, (int)sizeof p) < 0) return 0;
        n = snprintf(out, (size_t)cap, "%.72s\n", p);
        break;
    case TOOL_GREP:
    case TOOL_GLOB:
        if (field(tk, input, "pattern", a, (int)sizeof a) < 0) return 0;
        field(tk, input, "path", p, (int)sizeof p);
        n = snprintf(out, (size_t)cap, "'%.40s'%s%.30s\n", a,
                     p[0] ? " in " : "", p);
        break;
    default: return 0;
    }
    return (n < cap) ? n : cap - 1;
}

/* Any of POSIX/DOS/HFS path separators -- input paths come from the model so
 * may not match the host convention; built paths use plat_dirsep(). */
static int is_sep(char c)
{
    return c == '/' || c == '\\' || c == plat_dirsep();
}

static void maybe_trunc(char *out, int cap, int n)
{
    if (n >= cap - 1) {
        int ml = (int)sizeof OUT_CAP_TRUNC_MSG - 1;
        memcpy(out + cap - 1 - ml, OUT_CAP_TRUNC_MSG, (size_t)ml);
        out[cap - 1] = 0;
    }
}

static int do_shell(const char *cmd, char *out, int cap)
{
    char cwd[PLAT_PATH_MAX];
    int tail = PLAT_PATH_MAX + 32, rc, n;
    if (tail >= cap) tail = 0;
    rc = plat_shell(cmd, out, cap - tail);
    n = (int)strlen(out);
    if (n >= cap - tail - 1) {
        maybe_trunc(out, cap - tail, n);
        n = (int)strlen(out);
    }
    if (rc != 0 && n + 24 < cap)
        n += snprintf(out + n, (size_t)(cap - n), "\n[exit code %d]", rc);
    if (plat_getcwd(cwd, (int)sizeof cwd) && n + (int)strlen(cwd) + 12 < cap)
        snprintf(out + n, (size_t)(cap - n), "\n[cwd: %s]\n", cwd);
    return 0;
}

#define READ_LINE 512

static int do_read(const char *path, int offset, int limit, char *out, int cap)
{
    FILE *f;
    char ln[READ_LINE];
    int lno = 0, emitted = 0, o = 0;
    f = fopen(path, "rb");
    if (!f) return errp(out, cap, "error: cannot open '%s'", path);
    if (offset < 1) offset = 1;
    while (fgets(ln, (int)sizeof ln, f)) {
        int l = (int)strlen(ln);
        int has_nl = (l > 0 && ln[l - 1] == '\n');
        lno++;
        if (lno >= offset) {
            if (has_nl) ln[--l] = 0;
            if (l > 0 && ln[l - 1] == '\r') ln[--l] = 0;
            /* "%6d\t...\n": up to 10 digits + tab + newline. */
            if (o + 10 + 1 + l + 1 + (int)sizeof OUT_CAP_TRUNC_MSG > cap) {
                strcpy(out + o, OUT_CAP_TRUNC_MSG);
                o += (int)sizeof OUT_CAP_TRUNC_MSG - 1;
                break;
            }
            o += snprintf(out + o, (size_t)(cap - o), "%6d\t%s\n", lno, ln);
            emitted++;
        }
        if (!has_nl) /* drain rest of over-long line */
            while (fgets(ln, (int)sizeof ln, f) && !strchr(ln, '\n'))
                ;
        if (limit > 0 && emitted >= limit) break;
    }
    fclose(f);
    if (emitted == 0)
        o = (offset > 1) ? snprintf(out, (size_t)cap,
                                    "(offset %d past end of file: %d lines)",
                                    offset, lno)
                         : snprintf(out, (size_t)cap, "(empty file)");
    out[o < cap ? o : cap - 1] = 0;
    return 0;
}

#define EDIT_FILE_CAP 32768

/* fwrite s[0..n), expanding LF -> CRLF if crlf. Returns 0 / nonzero error. */
static int fput_seg(FILE *f, const char *s, int n, int crlf)
{
    int i = 0, j;
    if (!crlf) return fwrite(s, 1, (size_t)n, f) != (size_t)n;
    while (i < n) {
        for (j = i; j < n && s[j] != '\n'; j++)
            ;
        if (j > i && fwrite(s + i, 1, (size_t)(j - i), f) != (size_t)(j - i))
            return 1;
        if (j < n) {
            if (fwrite("\r\n", 1, 2, f) != 2) return 1;
            j++;
        }
        i = j;
    }
    return 0;
}

static int do_edit(const char *path, const char *olds, int olen,
                   const char *news, int nlen, int all, char *out, int cap)
{
    static char fb[EDIT_FILE_CAP];
    char tmp[PLAT_PATH_MAX + 2];
    FILE *f;
    int fn, i, j, hits = 0, werr = 0, rderr, had_crlf = 0;
    unsigned long mode;

    if (olen == 0) return errp(out, cap, "error: old_string is empty");
    if (olen == nlen && memcmp(olds, news, (size_t)olen) == 0)
        return errp(out, cap, "error: old_string and new_string are identical");
    f = fopen(path, "rb");
    if (!f) return errp(out, cap, "error: cannot open '%s'", path);
    fn = (int)fread(fb, 1, sizeof fb, f);
    rderr = ferror(f);
    fclose(f);
    if (rderr) return errp(out, cap, "error: read of '%s' failed", path);
    if (fn >= (int)sizeof fb)
        return errp(out, cap, "error: file too large for Edit (>= %d bytes)",
                    (int)sizeof fb);
    /* Normalize CRLF -> LF so old_string (which the model sends LF-only)
     * matches files with native Windows line endings. Remember whether the
     * file used CRLF so the binary-mode write below can restore it -- text
     * mode would force the platform default and flip endings on POSIX. */
    for (i = j = 0; i < fn; i++)
        if (fb[i] == '\r' && i + 1 < fn && fb[i + 1] == '\n')
            had_crlf = 1;
        else
            fb[j++] = fb[i];
    fn = j;
    for (i = 0; i + olen <= fn;)
        if (memcmp(fb + i, olds, (size_t)olen) == 0) {
            hits++;
            i += olen;
        } else
            i++;
    if (hits == 0)
        return errp(out, cap,
                    "error: old_string not found in file. Make sure it matches "
                    "exactly, including whitespace and indentation.");
    if (hits > 1 && !all)
        return errp(out, cap,
                    "error: old_string appears %d times in file. Either add "
                    "more surrounding context to make it unique, or set "
                    "replace_all=true.",
                    hits);

    /* Temp file in the target's directory (rename must not cross volumes)
     * with a fixed 8.3-safe name: the old "path~" scheme grew a 4-char
     * extension that DOS filesystems (win16) silently truncate back to the
     * original name, so the "temp" WAS the target and the remove+rename
     * fallback deleted the only copy. */
    {
        int di = -1;
        for (i = 0; path[i]; i++)
            if (is_sep(path[i])) di = i;
        /* A drive-relative DOS path ("C:FOO.TXT") has no separator; the
         * drive prefix must still reach the temp name or it lands on the
         * current drive and the remove+rename fallback can cross volumes
         * (remove the original, then fail the second rename too). */
        if (di < 0 && path[0] && path[1] == ':') di = 1;
        if (snprintf(tmp, sizeof tmp, "%.*sRLC$EDIT.TMP", di + 1, path)
            >= (int)sizeof tmp)
            return errp(out, cap, "error: path too long");
    }
    /* fopen("wb") creates tmp with the umask default and rename() then
     * replaces path's inode, so an executable script would silently lose
     * its +x bit. Capture the original mode now and reapply it to tmp
     * before the rename. No-op on platforms without file modes. */
    mode = plat_file_mode(path);
    f = fopen(tmp, "wb");
    if (!f) return errp(out, cap, "error: cannot write '%s'", tmp);
    for (i = 0; i < fn && !werr; i = j) {
        for (j = i; j < fn; j++)
            if (j + olen <= fn && memcmp(fb + j, olds, (size_t)olen) == 0)
                break;
        if (j > i) werr |= fput_seg(f, fb + i, j - i, had_crlf);
        if (j < fn) {
            werr |= fput_seg(f, news, nlen, had_crlf);
            j += olen;
        }
    }
    if (fclose(f) != 0) werr = 1;
    if (werr) {
        remove(tmp);
        return errp(out, cap, "error: write to '%s' failed", tmp);
    }
    plat_set_file_mode(tmp, mode);
    /* POSIX rename() replaces atomically; on platforms where it refuses to
     * overwrite (Win32, classic Mac), fall back to remove+rename. */
    if (rename(tmp, path) != 0) {
        int rm = remove(path);
        if (rename(tmp, path) != 0)
            return errp(out, cap,
                        "error: rename failed; %snew content left at '%s'",
                        rm == 0 ? "original removed, " : "", tmp);
    }
    snprintf(out, (size_t)cap, "edited %s: %d replacement%s", path, hits,
             hits == 1 ? "" : "s");
    return 0;
}

static void mkparents(const char *path)
{
    char b[PLAT_PATH_MAX];
    int i;
    for (i = 0; path[i] && i < (int)sizeof b - 1; i++) {
        b[i] = path[i];
        if (i > 0 && is_sep(path[i])) {
            b[i] = 0;
            plat_mkdir(b);
            b[i] = path[i];
        }
    }
}

static int do_write(const char *path, const char *content, int clen, char *out,
                    int cap)
{
    FILE *f;
    mkparents(path);
    /* Text mode: the C runtime translates '\n' to the platform EOL (CRLF on
     * win32, no-op on POSIX). The model emits LF-only content; without this,
     * Write produced LF-only files that cmd.exe / .bat cannot parse. */
    f = fopen(path, "w");
    if (!f) return errp(out, cap, "error: cannot create '%s'", path);
    if ((int)fwrite(content, 1, (size_t)clen, f) != clen) {
        fclose(f);
        return errp(out, cap, "error: write to '%s' failed", path);
    }
    if (fclose(f) != 0)
        return errp(out, cap, "error: close of '%s' failed", path);
    snprintf(out, (size_t)cap, "wrote %d bytes to %s", clen, path);
    return 0;
}

/* ---- grep ------------------------------------------------------------ */

#define GREP_MAX_DEPTH 8
#define GREP_MAX_FILES 800
#define GREP_LINE 256

typedef struct {
    const char *pat;
    char *out;
    int cap, off;
    int files, hits;
    int glob; /* 0 = grep file contents, 1 = match path against pat */
} grep_t;

/* '*' matches any run (incl. '/'); '?' matches one char. */
static int wild_match(const char *p, const char *s)
{
    const char *star = NULL, *ss = NULL;
    while (*s) {
        if (*p == '*') {
            star = p++;
            ss = s;
        } else if (*p == '?' || *p == *s) {
            p++;
            s++;
        } else if (star) {
            p = star + 1;
            s = ++ss;
        } else
            return 0;
    }
    while (*p == '*')
        p++;
    return *p == 0;
}

static int grep_emit(grep_t *g, const char *path, int lno, const char *line)
{
    char buf[128 + 16 + GREP_LINE];
    int n = lno ? snprintf(buf, sizeof buf, "%.128s:%d: %.*s\n", path, lno,
                           GREP_LINE - 1, line)
                : snprintf(buf, sizeof buf, "%.256s\n", path);
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    if (g->off + n + (int)sizeof OUT_CAP_TRUNC_MSG > g->cap) {
        if (g->off + (int)sizeof OUT_CAP_TRUNC_MSG <= g->cap) {
            strcpy(g->out + g->off, OUT_CAP_TRUNC_MSG);
            g->off += (int)sizeof OUT_CAP_TRUNC_MSG - 1;
        }
        return -1;
    }
    memcpy(g->out + g->off, buf, (size_t)n + 1);
    g->off += n;
    g->hits++;
    return 0;
}

static int grep_file(grep_t *g, const char *path)
{
    char ln[GREP_LINE];
    FILE *f;
    int lno = 0, n, i, full = 0;
    if (++g->files > GREP_MAX_FILES) return -1;
    f = fopen(path, "rb");
    if (!f) return 0;
    n = (int)fread(ln, 1, sizeof ln, f);
    for (i = 0; i < n; i++)
        if (ln[i] == 0) {
            fclose(f);
            return 0;
        } /* binary */
    fseek(f, 0L, SEEK_SET);
    while (fgets(ln, (int)sizeof ln, f)) {
        int l = (int)strlen(ln);
        int has_nl = (l > 0 && ln[l - 1] == '\n');
        if (has_nl) ln[--l] = 0;
        if (l > 0 && ln[l - 1] == '\r') ln[--l] = 0;
        lno++;
        if (strstr(ln, g->pat) && grep_emit(g, path, lno, ln) < 0) {
            full = 1;
            break;
        }
        if (!has_nl) /* drain rest of over-long line */
            while (fgets(ln, (int)sizeof ln, f) && !strchr(ln, '\n'))
                ;
    }
    fclose(f);
    return full ? -1 : 0;
}

#define GREP_DIR_BUF 4096
/* One listing buffer per recursion depth (grep_dir iterates its listing while
 * recursing, so a single static would be clobbered; 4 KB on the stack x depth
 * 8 is too much for classic Mac). */
static char g_dirbuf[GREP_MAX_DEPTH + 1][GREP_DIR_BUF];

static int grep_dir(grep_t *g, char *path, int plen, int depth)
{
    char *list = g_dirbuf[depth];
    int n, i, j;
    if (depth > GREP_MAX_DEPTH) return 0;
    n = plat_list_dir(plen ? path : ".", list, GREP_DIR_BUF);
    if (n < 0) return 0;
    for (i = 0; list[i]; i = list[j] ? j + 1 : j) {
        int isdir = 0, nl;
        for (j = i; list[j] && list[j] != '\n'; j++)
            ;
        nl = j - i;
        if (nl > 0 && is_sep(list[i + nl - 1])) {
            isdir = 1;
            nl--;
        }
        if (nl == 0
            || (list[i] == '.' && (nl == 1 || (nl == 2 && list[i + 1] == '.'))))
            continue;
        if (plen + 1 + nl + 1 > PLAT_PATH_MAX) continue;
        /* Join, but don't double the separator (root may already end in one
         * -- e.g. ":" on classic Mac, where leading ':' means "relative"). */
        {
            int add = plen && !is_sep(path[plen - 1]);
            if (add) path[plen] = plat_dirsep();
            memcpy(path + plen + add, list + i, (size_t)nl);
            path[plen + add + nl] = 0;
        }
        if (isdir) {
            if (grep_dir(g, path, (int)strlen(path), depth + 1) < 0) return -1;
        } else if (g->glob) {
            g->files++;
            if (wild_match(g->pat, path) && grep_emit(g, path, 0, 0) < 0)
                return -1;
        } else if (grep_file(g, path) < 0)
            return -1;
        path[plen] = 0;
    }
    return 0;
}

static int do_grep(const char *pat, const char *root, int glob, char *out,
                   int cap)
{
    char path[PLAT_PATH_MAX];
    grep_t g;
    int plen;
    if (!*pat) return errp(out, cap, "error: empty pattern");
    g.pat = pat;
    g.out = out;
    g.cap = cap;
    g.off = 0;
    g.files = 0;
    g.hits = 0;
    g.glob = glob;
    out[0] = 0;
    plen = (root && *root && !(root[0] == '.' && root[1] == 0))
               ? (int)strlen(root)
               : 0;
    /* -2: room for the NUL plus a possible leading ':' inserted below. */
    if (plen > (int)sizeof path - 2) plen = 0;
    memcpy(path, root, (size_t)plen);
    path[plen] = 0;
    /* On HFS, "Sub:file" is absolute (Volume:file); a leading ':' makes a
     * built path relative. POSIX/DOS paths are relative without prefix. */
    if (plat_dirsep() == ':' && (plen == 0 || path[0] != ':')) {
        memmove(path + 1, path, (size_t)plen + 1);
        path[0] = ':';
        plen++;
    }
    grep_dir(&g, path, plen, 0);
    if (g.off == 0)
        snprintf(out, (size_t)cap, "(no matches in %d file%s)", g.files,
                 g.files == 1 ? "" : "s");
    return 0;
}

static int do_list(const char *path, char *out, int cap)
{
    int n = plat_list_dir(path && *path ? path : ".", out, cap);
    if (n < 0) return errp(out, cap, "error: cannot list '%s'", path);
    maybe_trunc(out, cap, (int)strlen(out));
    return 0;
}

/* Scratch for unescaped tool args. Never aliases `out`. */
#define ARG_BUF_CAP 16384
static char s_path[PLAT_PATH_MAX];
static char s_buf1[ARG_BUF_CAP];
static char s_buf2[ARG_BUF_CAP];

int tool_dispatch(const char *name, const char *input, int len, char *out,
                  int cap)
{
    jsmntok_t tk[MAXT];
    jsmn_parser p;
    int idx, cl, ol, nl;
    out[0] = 0;
    jsmn_init(&p);
    if (jsmn_parse(&p, input, (size_t)len, tk, MAXT) < 1)
        return errp(out, cap, "error: bad tool input JSON");

#define ARG(k, b, c)                                                           \
    do {                                                                       \
        int n_ = field_full(tk, input, k, b, c);                               \
        if (n_ == -1) return errp(out, cap, "error: missing '%s'", k);         \
        if (n_ == -2)                                                          \
            return errp(out, cap, "error: '%s' too long (>= %d bytes)", k,     \
                        (int)(c));                                             \
    } while (0)
#define ARGN(k, b, c, lv)                                                      \
    do {                                                                       \
        lv = field_full(tk, input, k, b, c);                                   \
        if (lv == -1) return errp(out, cap, "error: missing '%s'", k);         \
        if (lv == -2)                                                          \
            return errp(out, cap, "error: '%s' too long (>= %d bytes)", k,     \
                        (int)(c));                                             \
    } while (0)

    idx = tool_index(name);
    switch (idx) {
    case TOOL_SHELL:
        ARG("command", s_buf1, ARG_BUF_CAP);
        return do_shell(s_buf1, out, cap);
    case TOOL_READ:
        ARG("file_path", s_path, PLAT_PATH_MAX);
        return do_read(s_path, field_int(tk, input, "offset", 1),
                       field_int(tk, input, "limit", 0), out, cap);
    case TOOL_WRITE:
        ARG("file_path", s_path, PLAT_PATH_MAX);
        ARGN("content", s_buf1, ARG_BUF_CAP, cl);
        return do_write(s_path, s_buf1, cl, out, cap);
    case TOOL_LIST:
        if (field_full(tk, input, "path", s_path, PLAT_PATH_MAX) == -2)
            return errp(out, cap, "error: 'path' too long");
        return do_list(s_path, out, cap);
    case TOOL_GREP:
    case TOOL_GLOB:
        ARG("pattern", s_buf1, ARG_BUF_CAP);
        if (field_full(tk, input, "path", s_path, PLAT_PATH_MAX) == -2)
            return errp(out, cap, "error: 'path' too long");
        return do_grep(s_buf1, s_path, idx == TOOL_GLOB, out, cap);
    case TOOL_EDIT:
        ARG("file_path", s_path, PLAT_PATH_MAX);
        ARGN("old_string", s_buf1, ARG_BUF_CAP, ol);
        ARGN("new_string", s_buf2, ARG_BUF_CAP, nl);
        return do_edit(s_path, s_buf1, ol, s_buf2, nl,
                       field_bool(tk, input, "replace_all", 0), out, cap);
    default: return errp(out, cap, "error: unknown tool '%.64s'", name);
    }
#undef ARG
#undef ARGN
}

/* --- runtime self-test ----------------------------------------------- */

static int st_step(const char *label, const char *tool, const char *input,
                   const char *want_substr, char *out, int cap)
{
    int rc = tool_dispatch(tool, input, (int)strlen(input), out, cap);
    int ok = (rc == 0 && strstr(out, want_substr) != 0);
    printf("  %-20s %s", label, ok ? "OK" : "FAIL");
    if (!ok) {
        int n = (int)strlen(out);
        if (n > 60) {
            out[60] = 0;
            n = 60;
        }
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
            out[--n] = 0;
        printf("  (rc=%d, out=\"%s\")", rc, out);
    }
    printf("\n");
    return ok;
}

int tools_selftest(void)
{
    static char out[2048];
    int ok = 1;
    printf("tools self-test\n");
    /* 'echo' is built into both POSIX sh and COMMAND.COM. */
    ok &= st_step("shell (echo)", tool_name(TOOL_SHELL),
                  "{\"command\":\"echo relic_ok\"}", "relic_ok", out,
                  (int)sizeof out);
    ok &= st_step("shell (cd .)", tool_name(TOOL_SHELL),
                  "{\"command\":\"cd .\"}", "[cwd: ", out, (int)sizeof out);
    ok &=
        st_step("Write", "Write",
                "{\"file_path\":\"_RLCTEST.TMP\",\"content\":\"hello relic\"}",
                "wrote 11 bytes", out, (int)sizeof out);
    ok &= st_step("Read", "Read", "{\"file_path\":\"_RLCTEST.TMP\"}",
                  "hello relic", out, (int)sizeof out);
    ok &= st_step("Grep", "Grep", "{\"pattern\":\"hello relic\"}",
                  "_RLCTEST.TMP:1: hello relic", out, (int)sizeof out);
    ok &= st_step("Edit", "Edit",
                  "{\"file_path\":\"_RLCTEST.TMP\",\"old_string\":\"relic\","
                  "\"new_string\":\"RELIC!\"}",
                  "1 replacement", out, (int)sizeof out);
    ok &= st_step("Edit (verify)", "Read", "{\"file_path\":\"_RLCTEST.TMP\"}",
                  "hello RELIC!", out, (int)sizeof out);
    ok &=
        st_step("Write (multi)", "Write",
                "{\"file_path\":\"_RLCTEST.TMP\",\"content\":\"a\\nb\\nc\\n\"}",
                "wrote 6 bytes", out, (int)sizeof out);
    ok &= st_step("Read (offset)", "Read",
                  "{\"file_path\":\"_RLCTEST.TMP\",\"offset\":2,\"limit\":1}",
                  "     2\tb\n", out, (int)sizeof out);
    ok &= st_step("Glob", "Glob", "{\"pattern\":\"_RLC*.TMP\"}", "_RLCTEST.TMP",
                  out, (int)sizeof out);
    remove("_RLCTEST.TMP");
    printf("  %s\n", ok ? "all tools OK" : "one or more tools FAILED");
    return ok ? 0 : 1;
}
