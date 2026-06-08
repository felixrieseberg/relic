/* plat_win16.c -- Windows 3.x implementation of plat.h.
 *
 * Built with Open Watcom's Win386 extender: core/ compiles as ordinary
 * 32-bit flat C (so int stays 32 bits and BearSSL builds unchanged), and
 * wbind packages it into a real NE executable that Windows 3.1+ in 386
 * enhanced mode loads natively. The Watcom supervisor provides 32-bit
 * covers for the standard KERNEL/USER/GDI API.
 *
 * Winsock is NOT covered by the supervisor (it postdates Win386), so this
 * file calls the 16-bit WINSOCK.DLL directly: LoadLibrary + GetProcAddress
 * for the entry points, _Call16() to invoke them ('p' arguments are
 * converted to 16:16 aliases for the duration of the call), and
 * MapAliasToFlat() to read back far pointers such as gethostbyname()'s
 * hostent. Structures passed across the boundary are declared here with
 * explicit 16-bit-accurate layouts -- the system <winsock.h> would compile
 * to the wrong shapes under a 32-bit compiler.
 *
 * The console is Open Watcom's "default windowing" (-bw): printf/fgets in
 * a scrollable window, same approach as the macppc port's RetroConsole.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <direct.h>
#include <conio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#include <win386.h>
#include <wdefwin.h>
#include "plat/plat.h"

void plat_init(void)
{
    _dwSetAppTitle("Relic");
    _dwSetAboutDlg("About Relic",
                   "Relic -- a minimal coding agent for Windows 3.x");
}

char plat_dirsep(void) { return '\\'; }

/* Pump the default-windowing console (repaints, input, scrollbars) and let
 * other tasks run. Win 3.x is cooperatively multitasked: every wait loop
 * below must call this or the whole system freezes for the duration. */
static void w16_yield(void)
{
    _dwYield();
    Yield();
}

/* --- 16-bit Winsock 1.1 thunks ----------------------------------------- */

/* Win16 layouts. 'int' is 16 bits on the far side, so every struct that
 * crosses the boundary is spelled out with shorts/longs here. */
typedef struct {
    short sin_family;
    unsigned short sin_port; /* network order */
    unsigned long sin_addr;  /* network order */
    char sin_zero[8];
} w16_sockaddr_in; /* 16 bytes, same offsets both sides */

typedef struct {
    DWORD h_name;      /* char FAR * */
    DWORD h_aliases;   /* char FAR * FAR * */
    short h_addrtype;  /* AF_INET == 2 */
    short h_length;    /* 4 */
    DWORD h_addr_list; /* char FAR * FAR * */
} w16_hostent;

typedef struct {
    unsigned short fd_count;
    unsigned short fd_array[1]; /* SOCKET is u_int == 16 bits on Win16;
                                 * select() reads fd_count entries and we
                                 * only ever poll one socket. */
} w16_fd_set;

typedef struct {
    long tv_sec;
    long tv_usec;
} w16_timeval;

#define W16_AF_INET 2
#define W16_SOCK_STREAM 1
#define W16_INVALID_SOCKET 0xFFFFu
#define W16_INADDR_NONE 0xFFFFFFFFUL

/* Convert a 16:16 far pointer handed back by the 16-bit side (e.g.
 * gethostbyname's hostent) into a pointer this flat 32-bit code can
 * dereference: flat == linearbase(selector) + offset - linearbase(our DS).
 * win386's own MapAliasToFlat is unusable for this (it returns
 * system-linear addresses), and raw DPMI int 31h faults inside the
 * supervisor's 32-bit segment, so ask 16-bit KERNEL instead:
 * GetSelectorBase (KERNEL.186, Windows 3.1+) via the same _Call16 channel
 * as Winsock itself. Our DS base is derived from an AllocAlias16 anchor
 * whose flat address we know. */
static FARPROC g_selbase_fn;
static DWORD g_flatbase;

static int w16_flat_init(void)
{
    static char anchor;
    HINSTANCE k;
    DWORD a;
    if (g_selbase_fn) return 0;
    k = LoadLibrary("KERNEL");
    if ((UINT)k < 32) return -1;
    g_selbase_fn = GetProcAddress(k, "GETSELECTORBASE");
    if (g_selbase_fn == 0) return -1;
    a = AllocAlias16(&anchor); /* base(a) == flatbase + &anchor */
    if (a == 0) {
        g_selbase_fn = 0;
        return -1;
    }
    g_flatbase = _Call16(g_selbase_fn, "w", (WORD)(a >> 16)) - (DWORD)&anchor;
    FreeAlias16(a);
    return 0;
}

static void *w16_far_to_flat(DWORD faraddr)
{
    if (faraddr == 0 || g_selbase_fn == 0) return 0;
    return (void *)(_Call16(g_selbase_fn, "w", (WORD)(faraddr >> 16))
                    + (faraddr & 0xFFFFul) - g_flatbase);
}

/* Entry points resolved from WINSOCK.DLL at runtime. The Winsock 1.1 spec
 * fixes the exported names (uppercase, as in the canonical WINSOCK.DEF), so
 * GetProcAddress works against every vendor's stack: Microsoft TCP/IP-32 on
 * WfW 3.11, Trumpet Winsock on 3.1, and Win95's 16-bit thunk layer. */
enum {
    WSF_STARTUP,
    WSF_CLEANUP,
    WSF_SOCKET,
    WSF_CONNECT,
    WSF_SEND,
    WSF_RECV,
    WSF_SELECT,
    WSF_CLOSE,
    WSF_GETHOST,
    WSF_INETADDR,
    WSF_NFUNC
};
static const char *const WS_NAMES[WSF_NFUNC] = {
    "WSASTARTUP", "WSACLEANUP", "SOCKET",      "CONNECT",       "SEND",
    "RECV",       "SELECT",     "CLOSESOCKET", "GETHOSTBYNAME", "INET_ADDR"};

static FARPROC g_ws_fn[WSF_NFUNC];
static int g_ws_started; /* 0 untried, 1 up, -1 failed */

/* All socket payloads go through this bounce buffer with ONE 16:16 alias
 * created at startup. _Call16's per-call 'p' aliasing proved unreliable
 * for arbitrarily-aligned interior pointers (handshakes died with the
 * server rejecting our Finished -- corrupted bytes on the wire); a single
 * AllocAlias16 of a static buffer is exact, and the alias is passed as a
 * raw dword ('d') so no per-call conversion happens at all. Sized past
 * BearSSL's mono buffer (BR_SSL_BUFSIZE_MONO == 16709) so a maximum-size
 * TLS record never splits into a second thunk round trip; plat_net_wait
 * borrows the first 128 bytes as fd_set/timeval scratch between
 * transfers. */
static unsigned char g_xfer[16768];
static DWORD g_xfer16;

/* plat.h hands out int handles with negatives reserved for PLAT_NET_E*;
 * keep the same tiny slot table as the win32 port. */
#define MAX_SOCK 4
static unsigned short g_sock[MAX_SOCK];

static unsigned short w16_htons(unsigned short v)
{
    return (unsigned short)((v << 8) | (v >> 8));
}

static void ws_shutdown(void)
{
    int i;
    if (g_ws_started != 1) return;
    for (i = 0; i < MAX_SOCK; i++)
        if (g_sock[i] != W16_INVALID_SOCKET) {
            _Call16(g_ws_fn[WSF_CLOSE], "w", (WORD)g_sock[i]);
            g_sock[i] = W16_INVALID_SOCKET;
        }
    /* Unlike Win95, a 3.x winsock keeps per-task state until WSACleanup;
     * skipping it can wedge Trumpet until reboot. */
    _Call16(g_ws_fn[WSF_CLEANUP], "");
    g_ws_started = 0;
}

static int ws_init(void)
{
    HINSTANCE dll;
    int i;
    if (g_ws_started) return g_ws_started == 1 ? 0 : -1;
    g_ws_started = -1;
    dll = LoadLibrary("WINSOCK.DLL"); /* never freed: lives for the run */
    if ((UINT)dll < 32) return -1;
    for (i = 0; i < WSF_NFUNC; i++) {
        g_ws_fn[i] = GetProcAddress(dll, WS_NAMES[i]);
        if (g_ws_fn[i] == 0) return -1;
    }
    if (w16_flat_init() != 0) return -1;
    g_xfer16 = AllocAlias16(g_xfer);
    if (g_xfer16 == 0) return -1;
    /* Request Winsock 1.1; WSADATA (~400 bytes on Win16) lands in the
     * bounce buffer and is not otherwise needed. */
    if ((short)_Call16(g_ws_fn[WSF_STARTUP], "wd", (WORD)0x0101, g_xfer16) != 0)
        return -1;
    for (i = 0; i < MAX_SOCK; i++)
        g_sock[i] = W16_INVALID_SOCKET;
    atexit(ws_shutdown);
    g_ws_started = 1;
    return 0;
}

static int sock_alloc(unsigned short s)
{
    int i;
    for (i = 0; i < MAX_SOCK; i++)
        if (g_sock[i] == W16_INVALID_SOCKET) {
            g_sock[i] = s;
            return i;
        }
    return -1;
}

static unsigned short sock_get(int h)
{
    if ((unsigned)h >= MAX_SOCK) return W16_INVALID_SOCKET;
    return g_sock[h];
}

int plat_net_connect(const char *host, unsigned short port)
{
    w16_sockaddr_in sa;
    unsigned long addr;
    unsigned short s;
    int h, hl;

    if (ws_init() != 0) return PLAT_NET_ECONN;

    /* The hostname crosses the boundary via the bounce buffer too. */
    hl = (int)strlen(host);
    if (hl > 255) return PLAT_NET_EDNS;
    memcpy(g_xfer, host, (size_t)hl + 1);

    /* Accept dotted-quad directly (lets --ip work without DNS). */
    addr = _Call16(g_ws_fn[WSF_INETADDR], "d", g_xfer16);
    if (addr == W16_INADDR_NONE) {
        DWORD ret;
        w16_hostent *he;
        void *list, *a0;
        /* Blocking resolve; WINSOCK.DLL's default blocking hook pumps
         * messages so the system stays alive meanwhile. */
        ret = _Call16(g_ws_fn[WSF_GETHOST], "d", g_xfer16);
        if (ret == 0) return PLAT_NET_EDNS;
        he = (w16_hostent *)w16_far_to_flat(ret);
        if (he == 0 || he->h_length != 4 || he->h_addr_list == 0)
            return PLAT_NET_EDNS;
        list = w16_far_to_flat(he->h_addr_list);
        if (list == 0 || *(DWORD *)list == 0) return PLAT_NET_EDNS;
        a0 = w16_far_to_flat(*(DWORD *)list);
        if (a0 == 0) return PLAT_NET_EDNS;
        memcpy(&addr, a0, 4);
    }

    s = (unsigned short)_Call16(g_ws_fn[WSF_SOCKET], "www", (WORD)W16_AF_INET,
                                (WORD)W16_SOCK_STREAM, (WORD)0);
    if (s == W16_INVALID_SOCKET) return PLAT_NET_ECONN;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = W16_AF_INET;
    sa.sin_port = w16_htons(port);
    sa.sin_addr = addr;
    memcpy(g_xfer, &sa, sizeof sa);
    if ((short)_Call16(g_ws_fn[WSF_CONNECT], "wdw", (WORD)s, g_xfer16,
                       (WORD)sizeof sa)
        != 0) {
        _Call16(g_ws_fn[WSF_CLOSE], "w", (WORD)s);
        return PLAT_NET_ECONN;
    }
    h = sock_alloc(s);
    if (h < 0) {
        _Call16(g_ws_fn[WSF_CLOSE], "w", (WORD)s);
        return PLAT_NET_ECONN;
    }
    return h;
}

int plat_net_send(int h, const void *b, int n)
{
    unsigned short s = sock_get(h);
    short r;
    if (s == W16_INVALID_SOCKET) return -1;
    if (n > (int)sizeof g_xfer) n = (int)sizeof g_xfer;
    memcpy(g_xfer, b, (size_t)n);
    r = (short)_Call16(g_ws_fn[WSF_SEND], "wdww", (WORD)s, g_xfer16, (WORD)n,
                       (WORD)0);
    return r;
}

int plat_net_recv(int h, void *b, int n)
{
    unsigned short s = sock_get(h);
    short r;
    if (s == W16_INVALID_SOCKET) return -1;
    if (n > (int)sizeof g_xfer) n = (int)sizeof g_xfer;
    r = (short)_Call16(g_ws_fn[WSF_RECV], "wdww", (WORD)s, g_xfer16, (WORD)n,
                       (WORD)0);
    if (r > 0) memcpy(b, g_xfer, (size_t)r);
    return r;
}

int plat_net_wait(int h, int ms)
{
    DWORD t0 = GetTickCount();
    unsigned short s = sock_get(h);
    if (s == W16_INVALID_SOCKET) return -1;
    for (;;) {
        w16_fd_set *fds = (w16_fd_set *)g_xfer;
        w16_timeval *tv = (w16_timeval *)(g_xfer + 64);
        short r;
        long left = (long)ms - (long)(GetTickCount() - t0);
        long slice;
        if (left < 0) left = 0;
        /* Short select slices with a message pump in between keep the
         * cooperative scheduler fed during long API waits. */
        slice = left < 250 ? left : 250;
        fds->fd_count = 1;
        fds->fd_array[0] = s;
        tv->tv_sec = 0;
        tv->tv_usec = slice * 1000L;
        r = (short)_Call16(g_ws_fn[WSF_SELECT], "wdddd", (WORD)0, g_xfer16, 0L,
                           0L, g_xfer16 + 64);
        if (r != 0) return r; /* >0 readable, <0 error */
        if (left <= slice) return 0;
        w16_yield();
    }
}

void plat_net_close(int h)
{
    if ((unsigned)h >= MAX_SOCK || g_sock[h] == W16_INVALID_SOCKET) return;
    _Call16(g_ws_fn[WSF_CLOSE], "w", (WORD)g_sock[h]);
    g_sock[h] = W16_INVALID_SOCKET;
}

/* --- entropy / time ----------------------------------------------------- */

/* Pentium timestamp counter, gated so 386/486 boxes don't fault on the
 * opcode: CPUID exists iff EFLAGS.ID (bit 21) is toggleable from ring 3,
 * and CPUID.1:EDX bit 4 then reports TSC support. The cycle counter's low
 * bits carry sub-microsecond scheduling jitter -- by far the best entropy
 * this platform can offer when present (raw opcode bytes: wasm predates
 * the mnemonics). */
static unsigned long w16_eflags_id(void);
#pragma aux w16_eflags_id = "pushfd"                                           \
                            "pop eax"                                          \
                            "mov edx,eax"                                      \
                            "xor eax,200000h"                                  \
                            "push eax"                                         \
                            "popfd"                                            \
                            "pushfd"                                           \
                            "pop eax"                                          \
                            "xor eax,edx" __value[__eax] __modify[__edx]

static unsigned long w16_cpuid1_edx(void);
#pragma aux w16_cpuid1_edx = "mov eax,1" 0x0F 0xA2 /* cpuid */                 \
    "mov eax,edx" __value[__eax] __modify[__ebx __ecx __edx]

static unsigned long w16_rdtsc(void);
#pragma aux w16_rdtsc = 0x0F 0x31 /* rdtsc */                                  \
    __value[__eax] __modify[__edx]

int plat_entropy(unsigned char *out, int len)
{
    /* This is BearSSL's only DRBG seed (no system RNG exists on Win 3.x).
     * Each round waits for a 55 ms timer tick boundary, then folds in the
     * TSC when the CPU has one, the tick count, the 8253 timer phase
     * (ports 40h/43h are virtualised by the VTD in 386 enhanced mode and
     * carry sub-tick interrupt jitter), cursor position, free memory, and
     * the running task handle. ~900 ms once at startup. Threat model:
     * passive eavesdropper, retro toy. */
    static int has_tsc = -1;
    struct {
        DWORD tsc;
        DWORD tick;
        WORD timer;
        POINT pt;
        DWORD freesp;
        WORD task;
        DWORD t;
        DWORD ver;
    } s;
    int i, j;
    if (has_tsc < 0)
        has_tsc = w16_eflags_id() != 0 && (w16_cpuid1_edx() & 0x10) != 0;
    memset(out, 0, (size_t)len);
    memset(&s, 0, sizeof s);
    s.ver = GetVersion();
    for (j = 0; j < 16; j++) {
        DWORD t = GetTickCount();
        do {
            w16_yield();
        } while (GetTickCount() == t);
        if (has_tsc) s.tsc = w16_rdtsc();
        s.tick = GetTickCount();
        outp(0x43, 0); /* latch counter 0 */
        s.timer = (WORD)inp(0x40);
        s.timer |= (WORD)(inp(0x40) << 8);
        GetCursorPos(&s.pt);
        s.freesp = GetFreeSpace(0);
        s.task = (WORD)GetCurrentTask();
        s.t = (DWORD)time(NULL);
        /* Fold the rotated struct into every byte, plus a fresh fast-moving
         * sample (TSC low bytes, else the 8253 phase) so each output byte
         * touches a high-jitter source every round. */
        for (i = 0; i < len; i++)
            out[i] ^= ((unsigned char *)&s)[(i + j) % (int)sizeof s]
                      ^ (has_tsc ? ((unsigned char *)&s.tsc)[(i + j) & 3]
                                 : ((unsigned char *)&s.timer)[(i + j) & 1]);
    }
    return len;
}

unsigned long plat_time_unix(void)
{
    /* DOS clock is local time with no TZ concept; a few hours of skew is
     * harmless for cert validation (tls_client falls back to the build
     * date only when the clock reads pre-2020). */
    return (unsigned long)time(NULL);
}

/* --- platform info / chrome --------------------------------------------- */

const char *plat_os_desc(void)
{
    static char desc[40];
    DWORD v;
    if (desc[0]) return desc;
    v = GetVersion(); /* AL=major, AH=minor */
    snprintf(desc, sizeof desc, "Windows %d.%02d (16-bit)", (int)(v & 0xFF),
             (int)((v >> 8) & 0xFF));
    return desc;
}

const char *plat_shell_tool_name(void) { return "Shell"; }

const char *plat_shell_hint(void)
{
    return "The Shell tool runs COMMAND.COM in a DOS box -- DOS syntax only "
           "(dir, type, copy, del, md, ren, cd). Unix commands like ls, cat, "
           "grep, rm, mv, find DO NOT EXIST; use the LS / Read / Grep / Edit "
           "tools instead. Paths use backslashes and 8.3 names. There is no "
           "&&, no 2>, no globbing beyond *.ext, and exit codes are not "
           "reported. Each command briefly opens a DOS window. The working "
           "directory persists across calls (cd / drive changes stick, and "
           "apply to Read/LS etc. too); environment variables (set) do not.";
}

const char *plat_charset_hint(void)
{
    return "IMPORTANT: This console uses the Windows ANSI charset, not "
           "UTF-8. Output plain 7-bit ASCII only -- no emoji, no Unicode "
           "punctuation (use - for dashes, \" for quotes), no box-drawing.";
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
    static const char *F[4] = {"|", "/", "-", "\\"};
    return F[i & 3];
}

/* --- filesystem ---------------------------------------------------------- */

int plat_getcwd(char *out, int cap)
{
    if (!getcwd(out, cap)) {
        out[0] = 0;
        return 0;
    }
    return (int)strlen(out);
}

int plat_mkdir(const char *path)
{
    struct stat st;
    if (mkdir(path) == 0) return 0;
    return (stat(path, &st) == 0 && (st.st_mode & S_IFDIR)) ? 0 : -1;
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

int plat_list_dir(const char *path, char *out, int cap)
{
    DIR *d;
    struct dirent *e;
    int o = 0, cnt = 0;
    out[0] = 0;
    if (!path || !*path) path = ".";
    d = opendir(path);
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        int l = (int)strlen(e->d_name);
        int isdir = (e->d_attr & _A_SUBDIR) ? 1 : 0;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (o + l + 3 >= cap) break;
        memcpy(out + o, e->d_name, (size_t)l);
        o += l;
        if (isdir) out[o++] = '\\';
        out[o++] = '\n';
        cnt++;
    }
    closedir(d);
    out[o] = 0;
    return cnt;
}

/* --- shell --------------------------------------------------------------- */

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
        if (stricmp(tok, DOS_INTERNAL[i]) == 0) return 1;
    return 0;
}

/* First existing temp-dir env var, with a trailing backslash, else ".\". */
static void temp_path(char *out, int cap, const char *fname)
{
    static const char *envs[] = {"TEMP", "TMP", 0};
    const char *d = 0;
    int i, n;
    for (i = 0; envs[i]; i++) {
        d = getenv(envs[i]);
        if (d && *d) break;
        d = 0;
    }
    if (!d) d = ".";
    n = (int)strlen(d);
    if (n > cap - 14) n = cap - 14;
    memcpy(out, d, (size_t)n);
    if (out[n - 1] != '\\' && out[n - 1] != ':') out[n++] = '\\';
    strcpy(out + n, fname);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Synchronous COMMAND.COM: Win16 has no process-wait API and
 * GetModuleUsage is unreliable for DOS boxes, so the wrapper batch drops a
 * flag file as its last line and we poll for it with the message pump
 * running. */
#define SHELL_TIMEOUT_MS (10L * 60L * 1000L)

int plat_shell(const char *cmd, char *out, int cap)
{
    static char bat[PLAT_PATH_MAX], tmp[PLAT_PATH_MAX], flg[PLAT_PATH_MAX];
    static char run[PLAT_PATH_MAX + 160];
    char tok[128];
    const char *comspec;
    DWORD t0;
    FILE *f;
    int n, vlen;
    out[0] = 0;

    /* cd / chdir / X: -- COMMAND.COM has no chaining, so a directory change
     * is always the whole command. Apply it to this process so it sticks.
     * DOS also accepts the argument glued to the verb ("cd..", "cd\DIR");
     * first_token can't split those, so match the verb by prefix. */
    first_token(cmd, tok, sizeof tok);
    vlen = 0;
    if (!strnicmp(tok, "chdir", 5)
        && (tok[5] == 0 || tok[5] == '.' || tok[5] == '\\' || tok[5] == '/'))
        vlen = 5;
    else if (!strnicmp(tok, "cd", 2)
             && (tok[2] == 0 || tok[2] == '.' || tok[2] == '\\'
                 || tok[2] == '/'))
        vlen = 2;
    if (vlen || (tok[0] && tok[1] == ':' && tok[2] == 0)) {
        const char *a = cmd;
        int e;
        if (vlen) { /* skip the verb for cd/chdir */
            while (*a == ' ' || *a == '\t' || *a == '@')
                a++;
            a += vlen;
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
        if (tok[1] == ':') { /* drive prefix (or bare "X:") */
            if (_chdrive((tok[0] | 0x20) - 'a' + 1) != 0) {
                snprintf(out, (size_t)cap, "Invalid drive: %c:\n", tok[0]);
                return 1;
            }
            if (tok[2] == 0) {
                snprintf(out, (size_t)cap, "(cwd is now %s)\n", tok);
                return 0;
            }
        }
        if (chdir(tok) == 0) {
            snprintf(out, (size_t)cap, "(cwd is now %s)\n", tok);
            return 0;
        }
        snprintf(out, (size_t)cap, "Invalid directory: %s\n", tok);
        return 1;
    }

    /* 'exit' would end COMMAND.COM before the wrapper batch writes its
     * completion flag, hanging the poll below for the full timeout. */
    if (!stricmp(tok, "exit")) {
        snprintf(out, (size_t)cap,
                 "(nothing to exit -- each Shell call runs its own "
                 "COMMAND.COM)\n");
        return 0;
    }

    /* COMMAND.COM has no &&, ||, or 2>. It prints "Too many parameters" to
     * CON (uncapturable), so the model would see empty output and retry
     * forever. Reject up front with something actionable. */
    if (strstr(cmd, "&&") || strstr(cmd, "||") || strstr(cmd, "2>")) {
        snprintf(out, (size_t)cap,
                 "error: COMMAND.COM has no '&&', '||', or '2>'. Run one "
                 "command per Shell call (output is captured automatically), "
                 "or write a .BAT file.\n");
        return 1;
    }

    temp_path(bat, (int)sizeof bat, "RLC$RUN.BAT");
    temp_path(tmp, (int)sizeof tmp, "RLC$OUT.TMP");
    temp_path(flg, (int)sizeof flg, "RLC$FLG.TMP");
    remove(tmp);
    remove(flg);

    /* "wb": the lines below carry explicit \r\n -- text mode would double
     * the CR and COMMAND.COM then mangles the redirect filenames. */
    f = fopen(bat, "wb");
    if (!f) {
        snprintf(out, (size_t)cap, "error: cannot write %s\n", bat);
        return -1;
    }
    fprintf(f, "@ECHO OFF\r\n");
    if (strchr(cmd, '>')) {
        /* The command redirects its own output; don't fight over '>'. */
        fprintf(f, "%s\r\n", cmd);
    } else {
        /* CALL makes COMMAND.COM hold the redirect across a called batch
         * (same reasoning as the win32 port); harmless detection here is
         * extension-only since Win16 has no SearchPath. */
        n = (int)strlen(cmd);
        fprintf(f, "%s%s > %s\r\n",
                (n > 4 && stricmp(cmd + n - 4, ".BAT") == 0) ? "CALL " : "",
                cmd, tmp);
    }
    fprintf(f, "ECHO X > %s\r\n", flg);
    fclose(f);

    comspec = getenv("COMSPEC");
    if (!comspec || !*comspec) comspec = "COMMAND.COM";
    if (snprintf(run, sizeof run, "%s /C %s", comspec, bat)
        >= (int)sizeof run) {
        remove(bat);
        return -1;
    }
    if ((UINT)WinExec(run, SW_SHOWMINNOACTIVE) < 32) {
        remove(bat);
        snprintf(out, (size_t)cap, "error: cannot start %s\n", comspec);
        return -1;
    }

    t0 = GetTickCount();
    while (!file_exists(flg)) {
        DWORD t = GetTickCount();
        if (t - t0 > SHELL_TIMEOUT_MS) {
            snprintf(out, (size_t)cap,
                     "(timed out after 10 minutes waiting for the DOS box "
                     "to finish)\n");
            remove(bat);
            return -1;
        }
        /* ~200 ms between filesystem polls, pumping messages throughout. */
        do {
            w16_yield();
        } while (GetTickCount() - t < 200);
    }

    f = fopen(tmp, "rb");
    if (f) {
        int r = (int)fread(out, 1, (size_t)(cap - 1), f);
        out[r] = 0;
        fclose(f);
    }
    remove(bat);
    remove(tmp);
    remove(flg);

    if (out[0] == 0) {
        if (strchr(cmd, '>')) {
            strcpy(out, "(stdout redirected by command)\n");
        } else if (tok[0] && !is_dos_internal(tok)) {
            /* tok still holds the first token from the cd check above --
             * every branch that rewrites it returns before reaching here.
             * COMMAND.COM prints "Bad command or file name" straight to
             * CON, so '>' never captures it. Win16 has no SearchPath to
             * confirm, so phrase it as a hint rather than a verdict. */
            snprintf(out, (size_t)cap,
                     "(no output captured -- if '%s' is not a program on "
                     "PATH, COMMAND.COM printed its error to the console "
                     "only. This shell is DOS COMMAND.COM, not Unix: use "
                     "dir, type, copy, del, md, ren, cd, or the LS / Read / "
                     "Grep / Edit tools.)\n",
                     tok);
        } else {
            strcpy(out, "(no output)\n");
        }
    }
    /* DOS exit codes are unreachable through WinExec; report success and
     * let the captured text speak for itself. */
    return 0;
}

/* --- console ------------------------------------------------------------- */

/* The default-windowing console handles its own scrollback, wrapping and
 * input line; raw key access is not part of its contract, so the in-app
 * pager and ESC-abort are disabled (same trade as the macppc port). */
int plat_con_size(int *rows, int *cols)
{
    (void)rows;
    (void)cols;
    return 0;
}
int plat_con_raw(int on)
{
    (void)on;
    return 0;
}
int plat_getkey(void) { return 0; }
int plat_esc_poll(void) { return 0; }
int plat_con_clear(void) { return 0; }
void plat_con_attr(int a) { (void)a; }
void plat_con_backwrap(int cols) { (void)cols; }
void plat_on_sigint(void (*fn)(int)) { (void)fn; }
