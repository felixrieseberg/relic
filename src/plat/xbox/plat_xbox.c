/* plat_xbox.c -- Microsoft Xbox (2001) / nxdk implementation of plat.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#include <hal/video.h>
#include <nxdk/net.h>
#include <nxdk/mount.h> /* nxMountDrive -- E: partition is not auto-mounted */
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <windows.h>  /* Win32 shim: FindFirstFileA, CreateDirectoryA, etc. */
#include <winerror.h> /* ERROR_ALREADY_EXISTS */
#include "bearssl.h"  /* br_sha256 for plat_entropy's expand stage */

#include "../plat.h"
#include "xbox_fb.h"
#include "xbox_kbd.h"
#include "xbox_lua.h"

/* --- init ------------------------------------------------------------- */

static int g_net_up;
static char g_net_err[96];

const char *plat_net_errdetail(void) { return g_net_err; }

static void netfail(const char *stage, int err)
{
    snprintf(g_net_err, sizeof g_net_err, "%s err=%d", stage, err);
}

static void con_init(void)
{
    /* xbox_stdio.c renders directly into XVideoGetFB(), so all we need is
     * the 640x480x32 mode up. pb_init/pb_show_debug_screen add GPU push-
     * buffer setup that our text writer doesn't use, so skip them. */
    static int done = 0;
    if (done) return;
    done = 1;
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
}

static int net_init(void)
{
    /* nxNetInit returns 0 on success, -1/-2 on failure. Do NOT invert --
     * `!nxNetInit(...)` would treat success as failure. We pass explicit
     * DNS because xemu's user-mode NAT doesn't reliably hand it out via
     * DHCP option 6, and nxdk's lwIP won't fall back to the gateway. */
    nx_net_parameters_t p;
    int r;
    if (g_net_up) return 0;
    memset(&p, 0, sizeof p);
    p.ipv4_mode = NX_NET_DHCP;
    p.ipv4_dns1 = 0x08080808;
    p.ipv4_dns2 = 0x01010101;
    r = nxNetInit(&p);
    if (r != 0) {
        netfail("nxNetInit", r);
        return -1;
    }
    g_net_up = 1;
    return 0;
}

void entropy_flush(void);

/* After main() returns, xemu drops to the dashboard and repaints the
 * framebuffer. Park the CPU so output stays visible. atexit is LIFO and
 * this never returns -- flush the seed inline BEFORE blocking forever,
 * else any handler registered alongside would be stranded. */
static void xbox_hold_screen(void)
{
    entropy_flush();
    printf("\n(relic done; xbox has no 'exit' -- close xemu to quit)\n");
    fflush(stdout);
    for (;;)
        Sleep(1000);
}

void plat_init(void)
{
    /* D: is auto-mounted (nxdk libnxdk_automount_d) to the XBE's launch dir
     * before main(). We mount E: ourselves: it's the writable user-data
     * partition on every retail Xbox, so session files have a home when we
     * booted from the read-only DVD. Failures are non-fatal -- sess.c will
     * report the "cwd and $TEMP/... unwritable" diagnostic. */
    con_init();
    (void)nxMountDrive('E', "\\Device\\Harddisk0\\Partition1");
    xbox_kbd_init();
    xbox_lua_init();
    atexit(xbox_hold_screen);
}

char plat_dirsep(void) { return '\\'; }

/* --- time / entropy --------------------------------------------------- */

/* FILETIME epoch is 1601-01-01; Unix is 1970-01-01; gap is 11644473600 s
 * (a.k.a. 116444736000000000 in 100-ns ticks). */
#define XBOX_FT_UNIX_DELTA 116444736000000000ULL

unsigned long plat_time_unix(void)
{
    LARGE_INTEGER now;
    unsigned long long t;
    KeQuerySystemTime(&now);
    t = (unsigned long long)now.QuadPart;
    if (t < XBOX_FT_UNIX_DELTA) return 0; /* pre-1970, fresh-RTC box */
    return (unsigned long)((t - XBOX_FT_UNIX_DELTA) / 10000000ULL);
}

/* br_prng_seeder_system link stub lives in src/compat/nosys_bearssl.c */

/* Writable E: -- D: is the read-only DVD on shipped builds, so a write
 * there silently no-ops and cross-boot seed persistence never kicks in. */
#define SEED_PATH "E:\\RELIC.SED"
#define SEED_LEN 32

/* Cached across plat_entropy() calls. BearSSL pulls entropy multiple times
 * per handshake; re-hitting the FATX seed file each time added tens of ms
 * on real hardware. The seed is flushed via atexit. */
static unsigned char g_seed[SEED_LEN];
static int g_ent_loaded;
static int g_seed_dirty;

static void entropy_load_once(void)
{
    FILE *f;
    if (g_ent_loaded) return;
    g_ent_loaded = 1;
    f = fopen(SEED_PATH, "rb");
    if (f) {
        (void)fread(g_seed, 1, SEED_LEN, f);
        fclose(f);
    }
}

/* Non-static so xbox_lua.c can flush before XReboot/XLaunchXBE/shutdown,
 * which transfer to firmware without running atexit handlers. */
void entropy_flush(void)
{
    FILE *f;
    if (!g_seed_dirty) return;
    f = fopen(SEED_PATH, "wb");
    if (!f) return;
    (void)fwrite(g_seed, 1, SEED_LEN, f);
    fclose(f);
    g_seed_dirty = 0;
}

int plat_entropy(unsigned char *out, int len)
{
    /* Sole seed for BearSSL's HMAC-DRBG (BR_USE_URANDOM=0,
     * BR_USE_WIN32_RAND=0). On a dead-CMOS box KeQuerySystemTime is fixed after
     * power-on, so we MUST mix the persisted seed file to differentiate
     * cold-boot runs. */
    unsigned char pool[96];
    ULONGLONG qpc1, qpc2;
    LARGE_INTEGER now;
    volatile unsigned int spin;
    int po = 0;

    entropy_load_once();
    memcpy(pool + po, g_seed, SEED_LEN);
    po += SEED_LEN;

    /* nxdk's KeQueryPerformanceCounter() returns ULONGLONG (no out-param). */
    qpc1 = KeQueryPerformanceCounter();
    for (spin = 0; spin < 1000; spin++) {}
    qpc2 = KeQueryPerformanceCounter();
    KeQuerySystemTime(&now);

    memcpy(pool + po, &qpc1, sizeof qpc1);
    po += sizeof qpc1;
    memcpy(pool + po, &qpc2, sizeof qpc2);
    po += sizeof qpc2;
    memcpy(pool + po, &now, sizeof now);
    po += sizeof now;
    /* Per-console secret salt. XboxHDKey (kernel export, 16 bytes) is the
     * EEPROM-derived HDD lock key; unlike the factory MAC it never leaves
     * the box, so an on-LAN attacker can't reconstruct this pool input. */
    memcpy(pool + po, XboxHDKey, 16);
    po += 16;
    {
        unsigned int tc = KeTickCount;
        unsigned int sp = (unsigned int)(unsigned long)&spin;
        memcpy(pool + po, &tc, sizeof tc);
        po += sizeof tc;
        memcpy(pool + po, &sp, sizeof sp);
        po += sizeof sp;
    }

    /* SHA-256 counter-mode expand. A prior XOR fold produced zero bytes on
     * first boot (blank seed region dominated the output). */
    {
        br_sha256_context sc;
        unsigned char digest[32];
        unsigned int ctr;
        int off = 0;
        for (ctr = 0; off < len; ctr++) {
            br_sha256_init(&sc);
            br_sha256_update(&sc, pool, (size_t)po);
            br_sha256_update(&sc, &ctr, sizeof ctr);
            br_sha256_out(&sc, digest);
            {
                int take = len - off;
                if (take > 32) take = 32;
                memcpy(out + off, digest, (size_t)take);
                off += take;
            }
        }
    }

    /* Forward-secure the persisted seed: hash old seed + pool into new seed.
     * Flushed by atexit so we don't hit FATX on every handshake round. */
    {
        br_sha256_context sc;
        unsigned char digest[32];
        br_sha256_init(&sc);
        br_sha256_update(&sc, g_seed, SEED_LEN);
        br_sha256_update(&sc, pool, (size_t)po);
        br_sha256_out(&sc, digest);
        memcpy(g_seed, digest, SEED_LEN);
        g_seed_dirty = 1;
    }

    return len;
}

/* --- network (lwIP BSD sockets) --------------------------------------- */

int plat_net_connect(const char *host, unsigned short port)
{
    struct sockaddr_in sa;
    unsigned long addr;
    int s;

    if (net_init() != 0) return PLAT_NET_ECONN;

    addr = inet_addr(host);
    if (addr == INADDR_NONE) {
        struct hostent *he = lwip_gethostbyname(host);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            netfail("gethostbyname", 0);
            return PLAT_NET_EDNS;
        }
        memcpy(&addr, he->h_addr_list[0], 4);
    }
    s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        netfail("socket", s);
        return PLAT_NET_ECONN;
    }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, &addr, 4);
    if (lwip_connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        netfail("connect", 0);
        lwip_close(s);
        return PLAT_NET_ECONN;
    }
    return s;
}

int plat_net_send(int h, const void *b, int n)
{
    return lwip_send(h, b, (size_t)n, 0);
}
int plat_net_recv(int h, void *b, int n)
{
    return lwip_recv(h, b, (size_t)n, 0);
}

int plat_net_wait(int h, int ms)
{
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(h, &rfds);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return lwip_select(h + 1, &rfds, NULL, NULL, &tv);
}

void plat_net_close(int h) { lwip_close(h); }

/* --- shell tool: embedded Lua ----------------------------------------- */

const char *plat_shell_tool_name(void) { return "Lua"; }

const char *plat_shell_hint(void)
{
    return "This runs Lua 5.4, NOT a Unix shell. State persists across "
           "calls. print() output is the tool result. Hardware bindings: "
           "xbox.led(c1..c4), xbox.tray([open]), xbox.temp(), xbox.fan([n]), "
           "xbox.mem(), xbox.eeprom(), xbox.launch(path), xbox.reboot(), "
           "xbox.poweroff(), xbox.sleep(ms), xbox.ticks(), "
           "xbox.getkey([ms]); fb.dims/clear/"
           "pixel/rect/text draw on the 640x480 screen. Persistent world at "
           "E:/relic/: require() searches lib/, "
           "relic.register(name,fn,help) installs a global command, "
           "relic.help() lists them. Use Write/Edit to author files there.";
}

int plat_shell(const char *cmd, char *out, int cap)
{
    return xbox_lua_exec(cmd, out, cap);
}

const char *plat_charset_hint(void)
{
    return "IMPORTANT: This framebuffer console renders 7-bit ASCII only. "
           "No emoji, no Unicode punctuation (use - for dashes, \" for "
           "quotes), no box-drawing.";
}

const char *plat_os_desc(void) { return "Microsoft Xbox (nxdk)"; }

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

/* --- filesystem ------------------------------------------------------- */

int plat_getcwd(char *out, int cap)
{
    /* FATX has no cwd; anchor at D:\ (XBE launch dir + session store). */
    if (cap < 4) return 0;
    strcpy(out, "D:\\");
    return 3;
}

int plat_mkdir(const char *path)
{
    if (CreateDirectoryA(path, NULL)) return 0;
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : -1;
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
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pat[PLAT_PATH_MAX];
    int pl, o = 0, cnt = 0;

    out[0] = 0;
    if (!path || !*path) {
        strcpy(pat, "D:\\*");
    } else {
        pl = (int)strlen(path);
        if (pl + 3 >= (int)sizeof pat) return -1;
        memcpy(pat, path, (size_t)pl);
        if (pat[pl - 1] != '\\' && pat[pl - 1] != '/') pat[pl++] = '\\';
        pat[pl++] = '*';
        pat[pl] = 0;
    }
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

/* --- signals ---------------------------------------------------------- */

/* No Ctrl+C on Xbox (no OS signal); no cursor addressing on the debug
 * screen. plat_con_backwrap stays a no-op. */
void plat_on_sigint(void (*fn)(int)) { (void)fn; }
void plat_con_backwrap(int cols) { (void)cols; }

void plat_con_attr(int a)
{
    unsigned fg;
    fflush(stdout);
    switch (a) {
    case PLAT_ATTR_PROMPT: fg = 0x00FFFFFFu; break; /* bold -> white      */
    case PLAT_ATTR_TOOL: fg = 0x0000C000u; break;   /* green              */
    case PLAT_ATTR_DIM: fg = 0x00808080u; break;    /* grey               */
    default: fg = 0x00C0C0C0u; break;               /* reset: light grey  */
    }
    xbox_fb_set_fg(fg);
}

/* Keyboard: xbox_kbd.c owns the USB HID state. Raw mode is implicit
 * (the debug screen has no line-editing layer). */
int plat_con_raw(int on)
{
    (void)on;
    /* Always report raw-input support on Xbox. plat_getkey blocks while
     * pumping USB polls, so a keyboard hot-plugged AFTER boot still drives
     * the REPL -- without this, a slow-to-enumerate keyboard would make
     * main() fall through to the stdin-less fgets() one-shot path. */
    return 1;
}

int plat_getkey(void)
{
    int k;
    while ((k = xbox_kbd_get()) < 0) {
        (void)xbox_kbd_poll(); /* pumps usbh_pooling_hubs + ring */
        Sleep(2);
    }
    return k;
}

int plat_esc_poll(void)
{
    /* Scan the whole pending-input stream for ESC, preserving any non-ESC
     * keys in order. A simple pop-then-unget of the head swallows typeahead
     * forever because the ungot key is re-read on every subsequent poll. */
    return xbox_kbd_take_esc();
}

int plat_con_size(int *rows, int *cols)
{
    /* xbox_stdio subtracts a margin from the 640x480 framebuffer; ask it. */
    xbox_fb_dims(NULL, NULL, cols, rows);
    return 1;
}

int plat_con_clear(void)
{
    int i;
    for (i = 0; i < 32; i++)
        fputc('\n', stdout);
    fflush(stdout);
    return 1;
}
