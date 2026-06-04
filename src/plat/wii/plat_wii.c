/* plat_wii.c -- Nintendo Wii / devkitPPC + libogc implementation of plat.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* chdir, usleep */
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h> /* gettime(), ticks_to_* */
#include <network.h>
#include <fat.h>
#include "bearssl.h" /* br_sha256 for plat_entropy's expand stage */

#include "../plat.h"
#include "../lua_shell.h"
#include "wii_kbd.h"
#include "wii_lua.h"
#include "wii_fb.h"

/* --- init ------------------------------------------------------------- */

static int g_net_up;
static char g_net_err[96];
static char g_ip[16];

/* libogc's console understands a CSI subset; rows/cols are derived from the
 * framebuffer/font geometry once video is up. */
static int g_rows = 24, g_cols = 64;
static GXRModeObj *g_rmode;
static u32 *g_xfb;
static int g_fbw, g_fbh;

const char *plat_net_errdetail(void) { return g_net_err; }

static void netfail(const char *stage, int err)
{
    snprintf(g_net_err, sizeof g_net_err, "%s err=%d", stage, err);
}

static void con_init(void)
{
    int x, y, w, h;
    if (g_rmode) return;
    VIDEO_Init();
    g_rmode = VIDEO_GetPreferredMode(NULL);
    g_xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_rmode));
    g_fbw = g_rmode->fbWidth;
    g_fbh = g_rmode->xfbHeight;
    /* The Wii framebuffer is YUYV; an all-zero buffer renders as green, so
     * clear to YUV black before bringing up the console (whose 16-px CRT-
     * overscan margin would otherwise show that green). */
    VIDEO_ClearFrameBuffer(g_rmode, g_xfb, COLOR_BLACK);
    VIDEO_Configure(g_rmode);
    VIDEO_SetNextFramebuffer(g_xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (g_rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    /* 16-px margin keeps text out of the CRT overscan. CON_Init renders
     * straight into the xfb (CON_InitEx would allocate a private buffer
     * blitted on retrace, overwriting anything fb.* draws). */
    x = 16;
    y = 16;
    w = g_fbw - 32;
    h = g_fbh - 32;
    CON_Init(g_xfb, x, y, w, h, g_fbw * VI_DISPLAY_PIX_SZ);
    CON_GetMetrics(&g_cols, &g_rows);
    /* Route stderr through the same console; libogc only wires stdout. */
    fclose(stderr);
    stderr = stdout;
}

/* --- framebuffer drawing primitives for wii_lua.c --------------------- */

/* RGB888 -> packed YUYV pair (both pixels same colour). Integer BT.601. */
static u32 rgb_yuyv(unsigned rgb)
{
    int r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    int y = (66 * r + 129 * g + 25 * b + 128) / 256 + 16;
    int cb = (-38 * r - 74 * g + 112 * b + 128) / 256 + 128;
    int cr = (112 * r - 94 * g - 18 * b + 128) / 256 + 128;
    y = y < 16 ? 16 : (y > 235 ? 235 : y);
    cb = cb < 16 ? 16 : (cb > 240 ? 240 : cb);
    cr = cr < 16 ? 16 : (cr > 240 ? 240 : cr);
    return ((u32)y << 24) | ((u32)cb << 16) | ((u32)y << 8) | (u32)cr;
}

void wii_fb_dims(int *w, int *h, int *cols, int *rows)
{
    if (w) *w = g_fbw;
    if (h) *h = g_fbh;
    if (cols) *cols = g_cols;
    if (rows) *rows = g_rows;
}

void wii_fb_clear(unsigned rgb)
{
    if (g_rmode) VIDEO_ClearFrameBuffer(g_rmode, g_xfb, rgb_yuyv(rgb));
}

void wii_fb_pixel(int x, int y, unsigned rgb)
{
    /* YUYV packs two horizontal pixels per u32; setting one pixel means
     * rewriting the pair's luma half. The shared chroma takes the new
     * colour -- a single-pixel write can't preserve the neighbour's hue
     * exactly anyway. */
    u32 c, pair, ny;
    int idx;
    if (!g_xfb || (unsigned)x >= (unsigned)g_fbw
        || (unsigned)y >= (unsigned)g_fbh)
        return;
    c = rgb_yuyv(rgb);
    ny = c >> 24;
    idx = y * (g_fbw / 2) + (x / 2);
    pair = g_xfb[idx];
    if (x & 1)
        pair = (pair & 0xffff00ff) | (ny << 8);
    else
        pair = (pair & 0x00ffffff) | (ny << 24);
    g_xfb[idx] = (pair & 0xff00ff00) | (c & 0x00ff00ff);
}

void wii_fb_rect(int x, int y, int w, int h, unsigned rgb)
{
    u32 c = rgb_yuyv(rgb);
    int yy, xx, x0, x1;
    if (!g_xfb || w <= 0 || h <= 0 || x >= g_fbw || y >= g_fbh) return;
    if (w > g_fbw) w = g_fbw;
    if (h > g_fbh) h = g_fbh;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > g_fbw) w = g_fbw - x;
    if (y + h > g_fbh) h = g_fbh - y;
    if (w <= 0 || h <= 0) return;
    /* Snap to pixel-pair boundaries (YUYV); odd edges lose at most 1 px. */
    x0 = x / 2;
    x1 = (x + w + 1) / 2;
    for (yy = y; yy < y + h; yy++) {
        u32 *row = g_xfb + yy * (g_fbw / 2);
        for (xx = x0; xx < x1; xx++)
            row[xx] = c;
    }
}

static int wii_net_bringup(void)
{
    s32 r;
    if (g_net_up) return 0;
    /* if_config is the libogc one-shot DHCP path; max_retries handles the
     * IOS-side stack needing a moment after boot before the BBA/WiFi is
     * ready. */
    r = if_config(g_ip, NULL, NULL, TRUE, 10);
    if (r < 0) {
        netfail("if_config", (int)r);
        return -1;
    }
    g_net_up = 1;
    return 0;
}

void entropy_flush(void);

static void wii_hold_screen(void)
{
    entropy_flush();
    printf("\n(relic done; press RESET / close Dolphin to quit)\n");
    fflush(stdout);
    for (;;) {
        if (SYS_ResetButtonDown()) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
        VIDEO_WaitVSync();
    }
}

/* Dolphin (and the Homebrew Channel) boot raw .dol/.elf under IOS 58. The
 * IOS 57-59 family uses the "NewUSB" stack and doesn't expose /dev/usb/kbd,
 * which is the only keyboard device Dolphin emulates. Probe it; if absent,
 * reload to an IOS that has it BEFORE touching SD/network so no handles are
 * invalidated. Any IOS >=30 except 57-59 works; 35/36 are present on every
 * retail Wii and have SD + sockets + USB_KBD. */
static void ensure_kbd_ios(void)
{
    static const int cand[] = {36, 35, 80, 61};
    s32 fd = IOS_Open("/dev/usb/kbd", 0);
    int i;
    if (fd >= 0) {
        IOS_Close(fd);
        return;
    }
    /* On hardware tickets exist, so this succeeds; on Dolphin's blank NAND
     * it fails (ES_EINVAL) but the SI keyboard path covers Dolphin, so we
     * just leave IOS as-is. */
    for (i = 0; i < (int)(sizeof cand / sizeof cand[0]); i++)
        if (IOS_ReloadIOS(cand[i]) >= 0) return;
}

void plat_init(void)
{
    con_init();
    ensure_kbd_ios();
    /* SD via libfat. The Homebrew Channel sets argv[0] to the launch path,
     * which devkitPPC's CRT uses to seed cwd; we additionally chdir so a
     * raw-dol launch (no argv) still finds RELIC.CFG and writes sessions
     * somewhere sensible. */
    if (fatInitDefault()) (void)chdir(WII_APP_DIR);
    wii_kbd_init();
    wii_lua_init();
    atexit(wii_hold_screen);
}

char plat_dirsep(void) { return '/'; }

/* --- time / entropy --------------------------------------------------- */

unsigned long plat_time_unix(void)
{
    /* libogc's time() reads the EXI RTC; Dolphin syncs it to the host. */
    return (unsigned long)time(NULL);
}

/* br_prng_seeder_system link stub lives in src/compat/nosys_bearssl.c */

#define SEED_PATH WII_APP_DIR "/RELIC.SED"
#define SEED_LEN 32

/* Cached across plat_entropy() calls. BearSSL pulls entropy multiple times
 * per handshake; re-hitting the SD card each time adds noticeable latency on
 * real hardware. The seed is flushed via atexit. */
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

/* Non-static so wii_lua.c can flush before SYS_ResetSystem, which transfers
 * to IOS without running atexit handlers. */
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
    /* Sole seed for BearSSL's HMAC-DRBG (BR_USE_URANDOM=0). The Wii has no
     * OS RNG; we mix the persisted seed file with the timebase, busy-loop
     * jitter, RTC, a stack address and the per-console ES device ID, then
     * SHA-256 counter-expand. The device ID is the only per-unit input that
     * is neither broadcast on the wire nor near-zero on a cold boot. */
    unsigned char pool[96];
    u64 tb1, tb2;
    u32 sp, devid = 0;
    time_t now;
    volatile unsigned spin;
    int po = 0;

    entropy_load_once();
    memcpy(pool + po, g_seed, SEED_LEN);
    po += SEED_LEN;

    tb1 = gettime();
    for (spin = 0; spin < 1000; spin++) {}
    tb2 = gettime();
    now = time(NULL);
    sp = (u32)(uintptr_t)&spin;
    (void)ES_GetDeviceID(&devid);

    memcpy(pool + po, &tb1, sizeof tb1);
    po += sizeof tb1;
    memcpy(pool + po, &tb2, sizeof tb2);
    po += sizeof tb2;
    memcpy(pool + po, &now, sizeof now);
    po += sizeof now;
    memcpy(pool + po, &sp, sizeof sp);
    po += sizeof sp;
    memcpy(pool + po, &devid, sizeof devid);
    po += sizeof devid;

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

    /* Forward-secure the persisted seed and flush now: the common Wii exit
     * is a hard power-off, which never reaches atexit(). */
    {
        br_sha256_context sc;
        unsigned char digest[32];
        br_sha256_init(&sc);
        br_sha256_update(&sc, g_seed, SEED_LEN);
        br_sha256_update(&sc, pool, (size_t)po);
        br_sha256_out(&sc, digest);
        memcpy(g_seed, digest, SEED_LEN);
        g_seed_dirty = 1;
        entropy_flush();
    }
    return len;
}

/* --- network (libogc bsd-ish wrappers over IOS) ----------------------- */

int plat_net_connect(const char *host, unsigned short port)
{
    struct sockaddr_in sa;
    struct hostent *he;
    s32 s, r;

    if (wii_net_bringup() != 0) return PLAT_NET_ECONN;

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_len = sizeof sa;
    if (!inet_aton(host, &sa.sin_addr)) {
        he = net_gethostbyname(host);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            netfail("gethostbyname", 0);
            return PLAT_NET_EDNS;
        }
        memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
    }
    s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) {
        netfail("socket", (int)s);
        return PLAT_NET_ECONN;
    }
    r = net_connect(s, (struct sockaddr *)&sa, sizeof sa);
    if (r < 0) {
        netfail("connect", (int)r);
        net_close(s);
        return PLAT_NET_ECONN;
    }
    return (int)s;
}

int plat_net_send(int h, const void *b, int n)
{
    return (int)net_send(h, b, (s32)n, 0);
}
int plat_net_recv(int h, void *b, int n)
{
    return (int)net_recv(h, b, (s32)n, 0);
}

int plat_net_wait(int h, int ms)
{
    /* libogc's net_poll wraps the IOS POLL ioctl. The header omits the event
     * flags; IOS uses the standard POLLIN=0x0001. */
    struct pollsd pfd;
    pfd.socket = h;
    pfd.events = 0x0001;
    pfd.revents = 0;
    return (int)net_poll(&pfd, 1, (s32)ms);
}

void plat_net_close(int h) { net_close(h); }

/* --- shell tool: embedded Lua ----------------------------------------- */

const char *plat_shell_tool_name(void) { return "Lua"; }

const char *plat_shell_hint(void)
{
    return "This runs Lua 5.4, NOT a Unix shell. State persists across "
           "calls. print() output is the tool result. Hardware bindings: "
           "wii.mem(), wii.ip(), wii.ticks(), wii.sleep(ms); input: "
           "wii.getkey([ms]) (text from the GC/USB keyboard the user is "
           "typing on), wii.kbd() -> bool, wii.si(chan) -> device-type for "
           "each "
           "GameCube port; power: wii.reset(), "
           "wii.poweroff(), wii.menu(); graphics: fb.dims() -> w,h,cols,"
           "rows, fb.clear(rgb), fb.pixel(x,y,rgb), fb.rect(x,y,w,h,rgb) "
           "draw on the YUYV framebuffer (rgb is 0xRRGGBB; the text "
           "console shares the same surface). Persistent world at "
           "sd:/apps/relic/: require() searches lib/, "
           "relic.register(name,fn,help) installs a global "
           "command, relic.help() lists them. Use Write/Edit to author "
           "files there.";
}

int plat_shell(const char *cmd, char *out, int cap)
{
    return lua_shell_exec(cmd, out, cap);
}

const char *plat_charset_hint(void)
{
    return "IMPORTANT: This libogc console renders 7-bit ASCII only. "
           "No emoji, no Unicode punctuation (use - for dashes, \" for "
           "quotes), no box-drawing.";
}

const char *plat_os_desc(void) { return "Nintendo Wii (libogc)"; }

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
    if (getcwd(out, (size_t)cap)) return (int)strlen(out);
    if (cap < 4) return 0;
    strcpy(out, "sd:/");
    return 4;
}

int plat_mkdir(const char *path)
{
    if (mkdir(path, 0777) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
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
    struct dirent *de;
    int o = 0, cnt = 0;
    out[0] = 0;
    d = opendir((path && *path) ? path : ".");
    if (!d) return -1;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        int l = (int)strlen(nm);
        int isdir = (de->d_type == DT_DIR);
        if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0)))
            continue;
        if (o + l + 3 >= cap) break;
        memcpy(out + o, nm, (size_t)l);
        o += l;
        if (isdir) out[o++] = '/';
        out[o++] = '\n';
        cnt++;
    }
    closedir(d);
    out[o] = 0;
    return cnt;
}

const char *wii_net_ip(void) { return g_net_up ? g_ip : ""; }

/* --- signals / console ------------------------------------------------ */

void plat_on_sigint(void (*fn)(int)) { (void)fn; }

int plat_con_raw(int on)
{
    (void)on;
    /* Always advertise raw input so main() takes the interactive REPL path
     * even when the keyboard enumerates after boot; plat_getkey blocks while
     * pumping the USB queue. */
    return 1;
}

int plat_getkey(void)
{
    int k;
    while ((k = wii_kbd_get()) < 0)
        VIDEO_WaitVSync(); /* ~60 Hz poll; also keeps console retrace */
    return k;
}

int plat_esc_poll(void) { return wii_kbd_take_esc(); }

int plat_con_size(int *rows, int *cols)
{
    *cols = g_cols;
    *rows = g_rows;
    return 1;
}

int plat_con_clear(void)
{
    /* libogc's console honours CSI 2J + H. */
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    return 1;
}

void plat_con_backwrap(int cols)
{
    printf("\x1b[A\x1b[%dC \b", cols - 1);
    fflush(stdout);
}

void plat_con_attr(int a)
{
    /* libogc console supports SGR 30-37 / 0. */
    switch (a) {
    case PLAT_ATTR_PROMPT: fputs("\x1b[36m", stdout); break;
    case PLAT_ATTR_TOOL: fputs("\x1b[33m", stdout); break;
    case PLAT_ATTR_DIM: fputs("\x1b[34m", stdout); break;
    default: fputs("\x1b[37m", stdout); break;
    }
}
