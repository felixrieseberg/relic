/* plat_win.c -- Win95 / Winsock 1.1 implementation of plat.h.
 * Links: wsock32.lib (Winsock 1.1). All KERNEL32/USER32 calls below
 * are present in stock Win95 RTM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <windows.h>
#include <winsock.h>
#include <conio.h>
#include "plat/plat.h"

void plat_init(void) {}
char plat_dirsep(void) { return '\\'; }

static int g_ws_started = 0;

/* plat.h hands out int handles with negatives reserved for PLAT_NET_E*. A
 * Winsock SOCKET is an opaque UINT_PTR -- (int)s can legitimately be
 * negative, which core/ would mistake for an error. Keep a tiny slot table
 * (only one connection is live at a time) and return the slot index. */
#define MAX_SOCK 4
static SOCKET g_sock[MAX_SOCK];

static int ws_init(void)
{
    WSADATA w;
    int i;
    if (g_ws_started) return 0;
    if (WSAStartup(MAKEWORD(1, 1), &w) != 0) return -1;
    for (i = 0; i < MAX_SOCK; i++)
        g_sock[i] = INVALID_SOCKET;
    g_ws_started = 1;
    return 0;
}

static int sock_alloc(SOCKET s)
{
    int i;
    for (i = 0; i < MAX_SOCK; i++)
        if (g_sock[i] == INVALID_SOCKET) {
            g_sock[i] = s;
            return i;
        }
    return -1;
}

static SOCKET sock_get(int h)
{
    if ((unsigned)h >= MAX_SOCK) return INVALID_SOCKET;
    return g_sock[h];
}

int plat_net_connect(const char *host, unsigned short port)
{
    struct hostent *he;
    struct sockaddr_in sa;
    unsigned long addr;
    SOCKET s;
    int h;

    if (ws_init() != 0) return -1;

    /* Accept dotted-quad directly (lets --ip work without DNS). */
    addr = inet_addr(host);
    if (addr == INADDR_NONE) {
        he = gethostbyname(host);
        if (!he || !he->h_addr) return PLAT_NET_EDNS;
        memcpy(&addr, he->h_addr, 4);
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return PLAT_NET_ECONN;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, &addr, 4);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        closesocket(s);
        return PLAT_NET_ECONN;
    }
    h = sock_alloc(s);
    if (h < 0) {
        closesocket(s);
        return PLAT_NET_ECONN;
    }
    return h;
}

int plat_net_send(int h, const void *b, int n)
{
    SOCKET s = sock_get(h);
    if (s == INVALID_SOCKET) return -1;
    return send(s, (const char *)b, n, 0);
}
int plat_net_recv(int h, void *b, int n)
{
    SOCKET s = sock_get(h);
    if (s == INVALID_SOCKET) return -1;
    return recv(s, (char *)b, n, 0);
}
int plat_net_wait(int h, int ms)
{
    fd_set rfds;
    struct timeval tv;
    SOCKET s = sock_get(h);
    if (s == INVALID_SOCKET) return -1;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(0, &rfds, NULL, NULL, &tv);
}
void plat_net_close(int h)
{
    if ((unsigned)h >= MAX_SOCK || g_sock[h] == INVALID_SOCKET) return;
    closesocket(g_sock[h]);
    g_sock[h] = INVALID_SOCKET;
}

unsigned long plat_time_unix(void) { return (unsigned long)time(NULL); }

int plat_entropy(unsigned char *out, int len)
{
    /* This is BearSSL's only DRBG seed (ow_compat.h disables BR_USE_WIN32_RAND
     * for Win95). Prefer RtlGenRandom (advapi32!SystemFunction036, Win2000+)
     * when present. Otherwise sample QPC across 8 Sleep(1) boundaries so the
     * low bits carry real scheduler/interrupt jitter -- a single snapshot on
     * a fresh-boot VM is near-deterministic. ~100-400 ms once at startup.
     * Threat model: passive eavesdropper, retro toy. */
    typedef BYTE(WINAPI * rgr_fn)(void *, ULONG);
    static rgr_fn rgr;
    static int tried;
    struct {
        DWORD tick;
        LARGE_INTEGER qpc;
        SYSTEMTIME st;
        DWORD pid, tid;
        MEMORYSTATUS ms;
        POINT pt;
    } s;
    int i, j;
    if (!tried) {
        HMODULE h = LoadLibraryA("ADVAPI32.DLL");
        if (h) rgr = (rgr_fn)GetProcAddress(h, "SystemFunction036");
        tried = 1;
    }
    if (rgr && rgr(out, (ULONG)len)) return len;
    memset(out, 0, (size_t)len);
    memset(&s, 0, sizeof s);
    GetSystemTime(&s.st);
    s.pid = GetCurrentProcessId();
    s.tid = GetCurrentThreadId();
    s.ms.dwLength = sizeof s.ms;
    GlobalMemoryStatus(&s.ms);
    GetCursorPos(&s.pt);
    for (j = 0; j < 8; j++) {
        Sleep(1);
        QueryPerformanceCounter(&s.qpc);
        s.tick = GetTickCount();
        /* Ensure every output byte folds in at least one fresh QPC byte
         * each round (the rotated full-struct fold alone leaves the upper
         * half of out[] touching only the once-sampled fields). */
        for (i = 0; i < len; i++)
            out[i] ^= ((unsigned char *)&s)[(i + j) % (int)sizeof s]
                      ^ ((unsigned char *)&s.qpc)[(i + j) & 7];
    }
    return len;
}

const char *plat_os_desc(void)
{
    static char desc[64];
    OSVERSIONINFOA v;
    const char *name = NULL;
    if (desc[0]) return desc;
    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    if (!GetVersionExA(&v)) return "Windows (unknown)";
    if (v.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        if (v.dwMinorVersion < 10)
            name = "Windows 95";
        else if (v.dwMinorVersion < 90)
            name = "Windows 98";
        else
            name = "Windows ME";
    } else if (v.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        if (v.dwMajorVersion == 4)
            name = "Windows NT 4.0";
        else if (v.dwMajorVersion == 5)
            name = v.dwMinorVersion == 0 ? "Windows 2000" : "Windows XP";
        else
            name = "Windows NT";
    } else if (v.dwPlatformId == VER_PLATFORM_WIN32s) {
        name = "Win32s";
    }
    if (name) {
        const char *csd = v.szCSDVersion;
        while (*csd == ' ')
            csd++;
        if (*csd)
            snprintf(desc, sizeof desc, "%s %s", name, csd);
        else
            snprintf(desc, sizeof desc, "%s", name);
    } else {
        snprintf(desc, sizeof desc, "Windows %u.%u", (unsigned)v.dwMajorVersion,
                 (unsigned)v.dwMinorVersion);
    }
    return desc;
}

const char *plat_shell_tool_name(void) { return "Shell"; }

const char *plat_shell_hint(void)
{
    return "The Shell tool runs COMMAND.COM -- DOS syntax only (dir, type, "
           "copy, del, md, ren, cd). Unix commands like ls, cat, grep, rm, "
           "mv, find DO NOT EXIST; use the LS / Read / Grep / Edit tools "
           "instead. Paths use backslashes and 8.3 names. There is no &&, "
           "no 2>, no globbing beyond *.ext. The working directory persists "
           "across calls (cd / drive changes stick, and apply to Read/LS "
           "etc. too); environment variables (set) do not.";
}

const char *plat_charset_hint(void)
{
    return "IMPORTANT: This console is CP437, not UTF-8. Output plain 7-bit "
           "ASCII only -- no emoji, no Unicode punctuation (use - for dashes, "
           "\" for quotes), no box-drawing.";
}

const char *plat_logo_line(int n)
{
    /* Plain-ASCII arcade ghost (see plat.h); same art as every other
     * target. */
    static const char *L[3] = {"  .---.  ", " | o o | ", " |/\\/\\/| "};
    return L[n];
}

const char *plat_spinner(unsigned i)
{
    /* CP437: FA=· F9=∙ -- the low-range sun/diamond glyphs are control
     * codes through stdio, so this is as fancy as it safely gets. */
    static const char *F[4] = {"\xFA", "\xF9", "*", "\xF9"};
    return F[i & 3];
}

int plat_getcwd(char *out, int cap)
{
    DWORD n = GetCurrentDirectoryA((DWORD)cap, out);
    return (n > 0 && (int)n < cap) ? (int)n : 0;
}

int plat_mkdir(const char *path)
{
    if (CreateDirectoryA(path, NULL)) return 0;
    return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
}

unsigned long plat_file_mode(const char *path)
{
    (void)path;
    return 0;
}
int plat_set_file_mode(const char *path, unsigned long mode)
{
    (void)path;
    (void)mode;
    return 0;
}

/* COMMAND.COM internals -- anything else must resolve on PATH. */
static const char *const DOS_INTERNAL[] = {
    "break",  "call", "cd",   "chdir", "cls",    "copy",  "ctty",
    "date",   "del",  "dir",  "echo",  "erase",  "exit",  "for",
    "goto",   "if",   "lh",   "md",    "mkdir",  "path",  "pause",
    "prompt", "rd",   "rem",  "ren",   "rename", "rmdir", "set",
    "shift",  "time", "type", "ver",   "verify", "vol",   0};

static int first_token(const char *s, char *tok, int cap)
{
    int o = 0;
    while (*s == ' ' || *s == '\t' || *s == '@')
        s++;
    while (*s && *s != ' ' && *s != '\t' && *s != '/' && *s != '<' && *s != '>'
           && *s != '|' && o < cap - 1)
        tok[o++] = *s++;
    tok[o] = 0;
    return o;
}

static int is_dos_internal(const char *tok)
{
    int i;
    for (i = 0; DOS_INTERNAL[i]; i++)
        if (lstrcmpiA(tok, DOS_INTERNAL[i]) == 0) return 1;
    return 0;
}

static int on_path(const char *tok)
{
    char hit[MAX_PATH];
    return SearchPathA(NULL, tok, ".COM", MAX_PATH, hit, NULL)
           || SearchPathA(NULL, tok, ".EXE", MAX_PATH, hit, NULL)
           || SearchPathA(NULL, tok, ".BAT", MAX_PATH, hit, NULL)
           || SearchPathA(NULL, tok, NULL, MAX_PATH, hit, NULL);
}

/* Does the first token of cmd resolve to a .BAT (explicit extension, or
 * extensionless name whose first PATH match is a .BAT)? COMMAND.COM
 * search order is .COM, .EXE, .BAT -- mirror that so 'edit' picks
 * EDIT.COM, not some EDIT.BAT further down PATH. */
static int resolves_to_bat(const char *cmd)
{
    char tok[128], hit[MAX_PATH];
    int n = first_token(cmd, tok, sizeof tok);
    if (n > 4 && lstrcmpiA(tok + n - 4, ".BAT") == 0) return 1;
    if (strchr(tok, '.') || is_dos_internal(tok)) return 0;
    if (SearchPathA(NULL, tok, ".COM", MAX_PATH, hit, NULL)) return 0;
    if (SearchPathA(NULL, tok, ".EXE", MAX_PATH, hit, NULL)) return 0;
    return SearchPathA(NULL, tok, ".BAT", MAX_PATH, hit, NULL) != 0;
}

int plat_shell(const char *cmd, char *out, int cap)
{
    static char tmp[MAX_PATH + 16], wrapped[4096 + MAX_PATH + 32];
    char tok[MAX_PATH]; /* first_token + reused for cd target */
    DWORD n;
    int rc;
    FILE *f;
    out[0] = 0;
    /* cd / chdir / X: -- COMMAND.COM has no chaining, so a directory change
     * is always the whole command. Apply it to this process so it sticks. */
    first_token(cmd, tok, sizeof tok);
    if (!lstrcmpiA(tok, "cd") || !lstrcmpiA(tok, "chdir")
        || (tok[0] && tok[1] == ':' && tok[2] == 0)) {
        const char *a = cmd;
        int e;
        if (tok[1] != ':') { /* skip the verb for cd/chdir */
            while (*a == ' ' || *a == '\t' || *a == '@')
                a++;
            while (*a && *a != ' ' && *a != '\t')
                a++;
        }
        while (*a == ' ' || *a == '\t')
            a++;
        strncpy(tok, a, sizeof tok - 1);
        tok[sizeof tok - 1] = 0;
        e = (int)strlen(tok);
        while (e > 0
               && (tok[e - 1] == ' ' || tok[e - 1] == '\t' || tok[e - 1] == '\r'
                   || tok[e - 1] == '\n'))
            tok[--e] = 0;
        if (tok[0] == 0) { /* bare cd: print cwd */
            plat_getcwd(out, cap);
            strcat(out, "\r\n");
            return 0;
        }
        if (SetCurrentDirectoryA(tok)) {
            snprintf(out, (size_t)cap, "(cwd is now %s)\n", tok);
            return 0;
        }
        snprintf(out, (size_t)cap, "Invalid directory: %s\n", tok);
        return 1;
    }
    /* COMMAND.COM has no &&, ||, or 2>. It prints "Too many parameters" to
     * CON (uncapturable) and returns 0, so the model would see empty output
     * and retry forever. Reject up front with something actionable. */
    if (strstr(cmd, "&&") || strstr(cmd, "||") || strstr(cmd, "2>")) {
        snprintf(out, (size_t)cap,
                 "error: COMMAND.COM has no '&&', '||', or '2>'. Run one "
                 "command per Shell call (output is captured automatically), "
                 "or write a .BAT file.\n");
        return 1;
    }
    /* If the command already redirects stdout, our own '> TMP' would win
     * (COMMAND.COM takes the last '>'), leaving the model's target file
     * empty. Run as-is and report that output went where it was sent. */
    if (strchr(cmd, '>')) {
        rc = system(cmd);
        snprintf(out, (size_t)cap,
                 "(stdout redirected by command; exit code %d)\n", rc);
        return rc;
    }
    n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) strcpy(tmp, ".\\");
    strcat(tmp, "RLC$OUT.TMP");
    /* COMMAND.COM has no 2>; rely on 1> for now (Win95 has no stderr redirect).
     * 'foo.bat > out' loses the redirect once COMMAND.COM enters batch mode,
     * so the batch runs but nothing is captured. The previous fix nested a
     * second COMMAND.COM via %COMSPEC% /C, but on real Win95 that double-VDM
     * spawn page-faulted at FFFF:FFFFFFFF. CALL is an internal -- it makes
     * COMMAND.COM hold the redirect for the whole called batch with no extra
     * process, so it can't introduce VDM-nesting failures. */
    if (snprintf(wrapped, sizeof wrapped, "%s%s > %s",
                 resolves_to_bat(cmd) ? "CALL " : "", cmd, tmp)
        >= (int)sizeof wrapped)
        return -1;
    rc = system(wrapped);
    f = fopen(tmp, "rb");
    if (f) {
        int r = (int)fread(out, 1, (size_t)(cap - 1), f);
        out[r] = 0;
        fclose(f);
    }
    remove(tmp);
    /* COMMAND.COM prints "Bad command or file name" straight to CON, so '>'
     * never captures it and the model just sees empty output. Reconstruct a
     * useful error so the model can recover instead of retrying blindly. */
    if (out[0] == 0) {
        if (first_token(cmd, tok, sizeof tok) && !is_dos_internal(tok)
            && !on_path(tok)) {
            snprintf(out, (size_t)cap,
                     "Bad command or file name: '%s' is not a DOS internal "
                     "command and was not found on PATH.\n"
                     "This shell is COMMAND.COM (Windows 95), not a Unix "
                     "shell. Use DOS equivalents (dir, type, copy, del, md, "
                     "ren, cd) or the LS / Read / Grep / Edit tools instead.\n",
                     tok);
            if (rc == 0) rc = 1;
        } else if (rc != 0) {
            snprintf(out, (size_t)cap,
                     "(no output captured -- COMMAND.COM cannot redirect "
                     "stderr, so any error text went to the console only)\n");
        } else {
            strcpy(out, "(no output)\n");
        }
    }
    return rc;
}

int plat_list_dir(const char *path, char *out, int cap)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pat[MAX_PATH];
    int o = 0, cnt = 0;
    out[0] = 0;
    if (!path || !*path) path = ".";
    if ((int)strlen(path) > MAX_PATH - 5) return -1;
    sprintf(pat, "%s\\*.*", path);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        const char *nm = fd.cFileName;
        int l = (int)strlen(nm);
        int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        if (o + l + 3 >= cap) break;
        memcpy(out + o, nm, (size_t)l);
        o += l;
        if (isdir) out[o++] = '\\';
        out[o++] = '\n';
        cnt++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    out[o] = 0;
    return cnt;
}

/* --- console ---------------------------------------------------------- */

int plat_con_size(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &bi))
        return 0;
    *rows = bi.srWindow.Bottom - bi.srWindow.Top + 1;
    *cols = bi.srWindow.Right - bi.srWindow.Left + 1;
    return 1;
}

void plat_con_attr(int a)
{
    static WORD base;
    static int probed;
    CONSOLE_SCREEN_BUFFER_INFO bi;
    HANDLE hcon;
    WORD w;
    /* Re-fetch every call: on Win9x, system() -> COMMAND.COM can leave a
     * cached handle stale, after which SetConsoleTextAttribute silently
     * no-ops and every later header prints in the shell's default grey. */
    hcon = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hcon == NULL || hcon == INVALID_HANDLE_VALUE) return;
    if (!probed) {
        if (!GetConsoleScreenBufferInfo(hcon, &bi)) return;
        base = bi.wAttributes;
        probed = 1;
    }
    /* Keep the user's background; swap only the foreground nibble. */
    w = (WORD)(base & 0xF0);
    switch (a) {
    case PLAT_ATTR_PROMPT:
        w |= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
             | FOREGROUND_INTENSITY;
        break;
    case PLAT_ATTR_TOOL: w |= FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
    case PLAT_ATTR_DIM:
        w |= FOREGROUND_INTENSITY; /* dark grey on the default VGA palette */
        break;
    default: w = base; break;
    }
    /* Drain both stdio streams so no buffered bytes land under the new
     * attribute (stderr carries the spinner). main() unbuffers stdout, but
     * keep the flush so this stays correct if that ever changes. */
    fflush(stdout);
    fflush(stderr);
    SetConsoleTextAttribute(hcon, w);
    /* Read-back barrier: on Win9x the console lives in 16-bit CONAGENT and
     * a bare SetConsoleTextAttribute can be applied after a following
     * WriteFile has already painted cells. A round-trip read forces the
     * attribute to settle before we return. */
    GetConsoleScreenBufferInfo(hcon, &bi);
}

int plat_con_clear(void)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    COORD origin;
    DWORD n, sz;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    origin.X = 0;
    origin.Y = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &bi))
        return 0;
    sz = (DWORD)bi.dwSize.X * (DWORD)bi.dwSize.Y;
    FillConsoleOutputCharacterA(h, ' ', sz, origin, &n);
    FillConsoleOutputAttribute(h, bi.wAttributes, sz, origin, &n);
    SetConsoleCursorPosition(h, origin);
    return 1;
}

void plat_con_backwrap(int cols)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    COORD p;
    DWORD w;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    (void)cols;
    fflush(stdout);
    if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &bi))
        return;
    p.X = (SHORT)(bi.dwSize.X - 1);
    p.Y = bi.dwCursorPosition.Y > 0 ? (SHORT)(bi.dwCursorPosition.Y - 1) : 0;
    FillConsoleOutputCharacterA(h, ' ', 1, p, &w);
    SetConsoleCursorPosition(h, p);
}

int plat_con_raw(int on)
{
    (void)on; /* getch() is already raw/unbuffered on Watcom */
    return 1;
}

void plat_on_sigint(void (*fn)(int))
{
    /* Watcom's signal(SIGINT) wraps SetConsoleCtrlHandler. The handler runs
     * on a separate thread; recv() is NOT interrupted, so the abort takes
     * effect on the next byte received. */
    if (fn)
        signal(SIGINT, fn);
    else
        signal(SIGINT, SIG_DFL);
}

int plat_esc_poll(void)
{
    int c;
    if (!kbhit()) return 0;
    c = getch();
    if (c == 27) return 1;
    if (c == 0 || c == 0xE0)
        (void)getch(); /* extended key (arrows/PgUp): swallow scan code */
    else
        ungetch(c);
    return 0;
}

int plat_getkey(void)
{
    for (;;) {
        int c = getch();
        if (c != 0 && c != 0xE0) return c;
        switch (getch()) {
        case 0x48: return PLAT_KEY_UP;
        case 0x50: return PLAT_KEY_DOWN;
        case 0x49: return PLAT_KEY_PGUP;
        case 0x51: return PLAT_KEY_PGDN;
        case 0x47: return PLAT_KEY_HOME;
        case 0x4F: return PLAT_KEY_END;
        default: break; /* swallow unknown extended (F-keys etc.), re-read */
        }
    }
}

/* WSACleanup is intentionally omitted; process exit closes sockets and Win95
 * does not require it for short-lived console apps. */
