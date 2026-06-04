/* plat_macppc.c -- classic Mac OS (PPC) implementation of plat.h.
 * Built with Retro68; runs under RetroConsole. Networking via Open Transport
 * (synchronous/blocking), filesystem via the HFS File Manager.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <AppleEvents.h>
#include <Events.h>
#include <Files.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <MacMemory.h>
#include <OSUtils.h>
#include <Processes.h>
#include <Windows.h>
#include "ot_compat.h"
#include "../plat.h"

/* Retro68's multiversal interfaces omit OSA.h and Components.h. We only need
 * a handful of entry points from AppleScriptLib (OSADoScript, OSAScriptError)
 * plus OpenDefaultComponent from the Component Manager; declare them here. */
typedef struct ComponentInstanceRecord *ComponentInstance;
typedef long OSAID;
typedef long OSAError;
#define kOSAComponentType 'osa '
#define kOSAGenericScriptingComponentSubtype 'scpt'
#define kOSANullScript ((OSAID)0)
#define kOSAModeCanInteract 0x00000008L
#define kOSAErrorMessage 'errs'
#define gestaltAppleScriptAttr 'ascr'
#define gestaltAppleScriptPresent 0
extern pascal ComponentInstance OpenDefaultComponent(OSType t, OSType s);
extern pascal OSAError OSADoScript(ComponentInstance ci, const AEDesc *src,
                                   OSAID ctx, DescType desiredType,
                                   long modeFlags, AEDesc *result);
extern pascal OSAError OSAScriptError(ComponentInstance ci, OSType selector,
                                      DescType desiredType, AEDesc *result);

/* --- helpers ---------------------------------------------------------- */

static void c2p(const char *c, Str255 p)
{
    int n = (int)strlen(c);
    if (n > 255) n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, c, (size_t)n);
}

/* Default volume + directory of the running app (set at launch). */
static int default_dir(short *vref, long *dirid)
{
    WDPBRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioNamePtr = 0;
    if (PBHGetVolSync(&pb) != 0) return -1;
    *vref = pb.ioWDVRefNum;
    *dirid = pb.ioWDDirID;
    return 0;
}

/* --- networking (Open Transport) -------------------------------------- */

static InetSvcRef g_inetSvc;
static int g_ot_started;

static char g_net_err[96];
#define NET_FAIL(stage, e)                                                     \
    (snprintf(g_net_err, sizeof g_net_err, "%s err=%ld", stage, (long)(e)), 0)
const char *plat_net_errdetail(void) { return g_net_err; }

#define MAX_EP 4
static EndpointRef g_ep[MAX_EP];

static int ot_init(void)
{
    OSStatus err;
    if (g_ot_started) return 0;
    err = InitOpenTransport();
    if (err != 0) {
        NET_FAIL("InitOpenTransport", err);
        return -1;
    }
    g_inetSvc = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
    if (g_inetSvc == 0 || err != 0) {
        NET_FAIL("OTOpenInternetServices", err);
        CloseOpenTransport();
        return -1;
    }
    OTSetSynchronous(g_inetSvc);
    OTSetBlocking(g_inetSvc);
    g_ot_started = 1;
    return 0;
}

static int ep_alloc(EndpointRef ep)
{
    int i;
    for (i = 0; i < MAX_EP; i++)
        if (g_ep[i] == 0) {
            g_ep[i] = ep;
            return i;
        }
    return -1;
}

int plat_net_connect(const char *host, unsigned short port)
{
    OSStatus err;
    EndpointRef ep;
    InetHostInfo hi;
    InetHost ip = 0;
    InetAddress addr;
    TCall call;
    char namebuf[kMaxHostNameLen + 1];
    int h;

    if (ot_init() != 0) return PLAT_NET_ECONN;

    if (OTInetStringToHost(host, &ip) != 0 || ip == 0) {
        strncpy(namebuf, host, kMaxHostNameLen);
        namebuf[kMaxHostNameLen] = 0;
        memset(&hi, 0, sizeof hi);
        err = OTInetStringToAddress(g_inetSvc, namebuf, &hi);
        if (err != 0 || hi.addrs[0] == 0) {
            NET_FAIL("OTInetStringToAddress", err);
            return PLAT_NET_EDNS;
        }
        ip = hi.addrs[0];
    }

    ep = OTOpenEndpoint(OTCreateConfiguration(kTCPName), 0, 0, &err);
    if (ep == 0 || err != 0) {
        NET_FAIL("OTOpenEndpoint", err);
        return PLAT_NET_ECONN;
    }
    OTSetSynchronous(ep);
    OTSetBlocking(ep);

    err = OTBind(ep, 0, 0);
    if (err != 0) {
        NET_FAIL("OTBind", err);
        OTCloseProvider(ep);
        return PLAT_NET_ECONN;
    }

    OTInitInetAddress(&addr, port, ip);
    memset(&call, 0, sizeof call);
    call.addr.len = sizeof addr;
    call.addr.buf = (UInt8 *)&addr;

    err = OTConnect(ep, &call, 0);
    if (err != 0) {
        long reason = err;
        if (err == kOTLookErr) {
            TDiscon dis;
            memset(&dis, 0, sizeof dis);
            if (OTRcvDisconnect(ep, &dis) == 0) reason = dis.reason;
        }
        NET_FAIL("OTConnect", reason);
        OTUnbind(ep);
        OTCloseProvider(ep);
        return PLAT_NET_ECONN;
    }

    h = ep_alloc(ep);
    if (h < 0) {
        NET_FAIL("ep_alloc", 0);
        OTCloseProvider(ep);
        return PLAT_NET_ECONN;
    }
    return h;
}

int plat_net_send(int h, const void *buf, int len)
{
    OTResult r;
    if ((unsigned)h >= MAX_EP || g_ep[h] == 0) return -1;
    r = OTSnd(g_ep[h], (void *)buf, (OTByteCount)len, 0);
    return (r > 0) ? (int)r : -1;
}

int plat_net_recv(int h, void *buf, int len)
{
    OTResult r;
    OTFlags flags = 0;
    if ((unsigned)h >= MAX_EP || g_ep[h] == 0) return -1;
    for (;;) {
        r = OTRcv(g_ep[h], buf, (OTByteCount)len, &flags);
        if (r > 0) return (int)r;
        if (r == kOTLookErr) {
            OTResult ev = OTLook(g_ep[h]);
            if (ev == T_ORDREL) {
                OTRcvOrderlyDisconnect(g_ep[h]);
                return 0;
            }
            if (ev == T_DISCONNECT) {
                OTRcvDisconnect(g_ep[h], 0);
                return 0;
            }
            return -1;
        }
        if (r == kOTNoDataErr) continue;
        return -1;
    }
}

int plat_net_wait(int h, int ms)
{
    UInt32 deadline;
    if ((unsigned)h >= MAX_EP || g_ep[h] == 0) return -1;
    deadline = TickCount() + (UInt32)(ms * 60 / 1000) + 1;
    for (;;) {
        OTByteCount n = 0;
        OSStatus e = OTCountDataBytes(g_ep[h], &n);
        if (e == 0 && n > 0) return 1;
        /* A pending T_ORDREL/T_DISCONNECT also makes recv() return now. */
        if (e == kOTLookErr) return 1;
        if (TickCount() >= deadline) return 0;
        /* Cooperative scheduler: without this the Finder freezes for the
         * full API latency. */
        SystemTask();
    }
}

void plat_net_close(int h)
{
    if ((unsigned)h >= MAX_EP || g_ep[h] == 0) return;
    OTSndOrderlyDisconnect(g_ep[h]);
    OTUnbind(g_ep[h]);
    OTCloseProvider(g_ep[h]);
    g_ep[h] = 0;
}

/* --- entropy / time --------------------------------------------------- */

int plat_entropy(unsigned char *out, int len)
{
    /* This is BearSSL's only seed (BR_USE_URANDOM=0). The previous tight
     * loop sampled Microseconds 32x in ~30 us -- effectively a single
     * timestamp. Here each sample is taken at a 60 Hz tick boundary
     * (~270 ms total), so the low bits of Microseconds at each boundary
     * carry real interrupt-latency jitter, and SystemTask lets the OS run
     * (mouse, net, disk) between samples for additional state churn.
     * Threat model: passive eavesdropper, retro toy. */
    UnsignedWide us;
    Point mouse;
    UInt32 acc[8], t;
    int i, j;
    memset(out, 0, (size_t)len);
    for (j = 0; j < 16; j++) {
        t = TickCount();
        do {
            SystemTask();
        } while (TickCount() == t);
        Microseconds(&us);
        GetMouse(&mouse);
        acc[0] = us.lo;
        acc[1] = us.hi;
        acc[2] = TickCount();
        acc[3] = FreeMem();
        acc[4] = ((UInt32)mouse.v << 16) | (UInt16)mouse.h;
        GetDateTime(&acc[5]);
        acc[6] = (UInt32)(unsigned long)&acc ^ LMGetTicks();
        acc[7] = (UInt32)j * 0x9E3779B9u;
        for (i = 0; i < len; i++)
            out[i] ^= ((unsigned char *)acc)[(i + j) % (int)sizeof acc];
    }
    return len;
}

/* Retro68's multiversal headers lack MachineLocation/ReadLocation; the symbol
 * is present in InterfaceLib so a local declaration suffices. Layout matches
 * Apple Universal Interfaces <OSUtils.h>. */
#ifndef __OSUTILS__
typedef struct {
    SInt32 latitude;
    SInt32 longitude;
    union {
        SInt8 dlsDelta;
        long gmtDelta;
    } u;
} MachineLocation;
extern pascal void ReadLocation(MachineLocation *loc);
#endif

unsigned long plat_time_unix(void)
{
    UInt32 t;
    MachineLocation loc;
    long gmt;
    GetDateTime(&t);
    /* GetDateTime returns *local* time. ReadLocation's gmtDelta is a 24-bit
     * signed seconds-east-of-UTC packed in the low 3 bytes; sign-extend and
     * subtract so cert validation sees real UTC. */
    ReadLocation(&loc);
    gmt = (long)(loc.u.gmtDelta & 0x00FFFFFF);
    if (gmt & 0x00800000) gmt |= 0xFF000000L;
    if ((long)t > gmt) t -= (UInt32)gmt;
    /* Mac epoch is 1904-01-01; Unix is 1970-01-01. */
    return (t > 2082844800UL) ? (unsigned long)(t - 2082844800UL) : 0;
}

/* --- shell / process -------------------------------------------------- */

/* Classic Mac OS has no Unix shell, but Open Scripting Architecture (OSA)
 * with AppleScript plays the same role: a text language that drives every
 * scriptable app on the system (Finder, MPW, BBEdit, Eudora, ...). We expose
 * OSADoScript under the "AppleScript" tool name. */

static ComponentInstance g_osa;
static int g_osa_checked;

static int osa_available(void)
{
    long attr;
    if (g_osa_checked) return g_osa != 0;
    g_osa_checked = 1;
    /* Bit 0 of gestaltAppleScriptAttr => AppleScript Extension present. */
    if (Gestalt(gestaltAppleScriptAttr, &attr) != 0) return 0;
    if (!(attr & (1L << gestaltAppleScriptPresent))) return 0;
    g_osa = OpenDefaultComponent(kOSAComponentType,
                                 kOSAGenericScriptingComponentSubtype);
    return g_osa != 0;
}

/* Copy an AEDesc whose data is a Handle (the classic layout) into a C
 * buffer. Returns bytes written (cap-bounded, NUL-terminated). */
static int desc_to_cstr(const AEDesc *d, char *out, int cap)
{
    Handle h = d->dataHandle;
    Size n;
    if (!h || cap <= 0) {
        if (cap > 0) out[0] = 0;
        return 0;
    }
    n = GetHandleSize(h);
    if (n > cap - 1) n = cap - 1;
    HLock(h);
    memcpy(out, *h, (size_t)n);
    HUnlock(h);
    out[n] = 0;
    return (int)n;
}

const char *plat_shell_tool_name(void) { return "AppleScript"; }

const char *plat_shell_hint(void)
{
    /* Embedded in a JSON string literal -- use \\\" so the JSON sees \" and
     * AppleScript sees a real double quote at runtime. */
    return "The command is AppleScript source, executed via OSA (the Mac's "
           "scripting equivalent of a shell). Use it to drive any scriptable "
           "app: 'tell application \\\"Finder\\\" to ...' for file ops, "
           "'tell application \\\"MPW\\\" to DoScript ...' to run builds if "
           "MPW is installed. Paths use ':' (e.g. 'Macintosh HD:Folder'). "
           "The returned text is the script result coerced to a string; on "
           "error, it is the AppleScript error message. There is no /bin/sh.";
}

int plat_shell(const char *cmd, char *out, int cap)
{
    AEDesc src, result, errDesc;
    OSAError err;
    int n;

    if (cap <= 0) return -1;
    out[0] = 0;

    if (!osa_available()) {
        snprintf(out, (size_t)cap,
                 "AppleScript (OSA) is not installed on this system. "
                 "No shell available -- use LS / Read / Write / Edit / "
                 "Grep instead.\n");
        return -1;
    }

    memset(&src, 0, sizeof src);
    memset(&result, 0, sizeof result);
    if (AECreateDesc(typeChar, cmd, (Size)strlen(cmd), &src) != 0) {
        snprintf(out, (size_t)cap, "AECreateDesc failed\n");
        return -1;
    }

    err = OSADoScript(g_osa, &src, kOSANullScript, typeChar,
                      kOSAModeCanInteract, &result);
    AEDisposeDesc(&src);

    if (err != 0) {
        memset(&errDesc, 0, sizeof errDesc);
        if (OSAScriptError(g_osa, kOSAErrorMessage, typeChar, &errDesc) == 0) {
            n = desc_to_cstr(&errDesc, out, cap);
            AEDisposeDesc(&errDesc);
            if (n == 0)
                snprintf(out, (size_t)cap, "OSA error %ld\n", (long)err);
        } else {
            snprintf(out, (size_t)cap, "OSA error %ld\n", (long)err);
        }
        AEDisposeDesc(&result);
        return -1;
    }

    desc_to_cstr(&result, out, cap);
    AEDisposeDesc(&result);
    return 0;
}

/* --- filesystem ------------------------------------------------------- */

char plat_dirsep(void) { return ':'; }

int plat_getcwd(char *out, int cap)
{
    short vref;
    long dirid;
    CInfoPBRec pb;
    Str255 name;
    char tmp[PLAT_PATH_MAX];
    int tlen = 0;
    out[0] = 0;
    if (default_dir(&vref, &dirid) != 0) return 0;
    /* Walk up to the volume root, prepending "name:" each step. */
    for (;;) {
        memset(&pb, 0, sizeof pb);
        pb.dirInfo.ioNamePtr = name;
        pb.dirInfo.ioVRefNum = vref;
        pb.dirInfo.ioFDirIndex = -1;
        pb.dirInfo.ioDrDirID = dirid;
        if (PBGetCatInfoSync(&pb) != 0) return 0;
        if (tlen + name[0] + 1 >= (int)sizeof tmp) return 0;
        memmove(tmp + name[0] + 1, tmp, (size_t)tlen);
        memcpy(tmp, name + 1, name[0]);
        tmp[name[0]] = ':';
        tlen += name[0] + 1;
        if (pb.dirInfo.ioDrDirID == 2) break; /* fsRtDirID -- volume root */
        dirid = pb.dirInfo.ioDrParID;
    }
    if (tlen >= cap) tlen = cap - 1;
    memcpy(out, tmp, (size_t)tlen);
    out[tlen] = 0;
    return tlen;
}

/* Resolve a directory path to vRefNum+dirID. Empty / "." -> default dir. */
static int resolve_dir(const char *path, short *vref, long *dirid)
{
    Str255 p;
    FSSpec spec;
    CInfoPBRec pb;
    OSErr e;
    if (!path || !*path || strcmp(path, ".") == 0 || strcmp(path, ":") == 0)
        return default_dir(vref, dirid);
    c2p(path, p);
    e = FSMakeFSSpec(0, 0, p, &spec);
    if (e != 0) return -1;
    memset(&pb, 0, sizeof pb);
    pb.dirInfo.ioNamePtr = spec.name;
    pb.dirInfo.ioVRefNum = spec.vRefNum;
    pb.dirInfo.ioDrDirID = spec.parID;
    pb.dirInfo.ioFDirIndex = 0;
    if (PBGetCatInfoSync(&pb) != 0) return -1;
    if (!(pb.dirInfo.ioFlAttrib & 0x10)) return -1; /* not a directory */
    *vref = spec.vRefNum;
    *dirid = pb.dirInfo.ioDrDirID;
    return 0;
}

int plat_list_dir(const char *path, char *out, int cap)
{
    short vref;
    long dirid;
    CInfoPBRec pb;
    Str255 name;
    int idx = 1, o = 0, cnt = 0;
    out[0] = 0;
    if (resolve_dir(path, &vref, &dirid) != 0) return -1;
    for (;; idx++) {
        memset(&pb, 0, sizeof pb);
        pb.hFileInfo.ioNamePtr = name;
        pb.hFileInfo.ioVRefNum = vref;
        pb.hFileInfo.ioDirID = dirid;
        pb.hFileInfo.ioFDirIndex = (short)idx;
        if (PBGetCatInfoSync(&pb) != 0) break;
        if (o + name[0] + 3 >= cap) break;
        memcpy(out + o, name + 1, name[0]);
        o += name[0];
        if (pb.hFileInfo.ioFlAttrib & 0x10) out[o++] = ':';
        out[o++] = '\n';
        cnt++;
    }
    out[o] = 0;
    return cnt;
}

int plat_mkdir(const char *path)
{
    Str255 p;
    FSSpec spec;
    long id;
    OSErr e;
    c2p(path, p);
    e = FSMakeFSSpec(0, 0, p, &spec);
    if (e == 0) return 0;    /* already exists */
    if (e != -43) return -1; /* fnfErr: parent ok, target absent */
    return FSpDirCreate(&spec, -1, &id) == 0 ? 0 : -1;
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

/* --- platform info / chrome ------------------------------------------- */

const char *plat_os_desc(void)
{
    static char desc[32];
    long v;
    if (desc[0]) return desc;
    if (Gestalt(gestaltSystemVersion, &v) != 0) return "Mac OS (PPC)";
    snprintf(desc, sizeof desc, "Mac OS %ld.%ld.%ld (PPC)",
             (long)((v >> 8) & 0xF) + (long)((v >> 12) & 0xF) * 10,
             (long)((v >> 4) & 0xF), (long)(v & 0xF));
    return desc;
}

const char *plat_charset_hint(void)
{
    return "This console renders MacRoman, not UTF-8. Output plain 7-bit "
           "ASCII only -- no emoji, no Unicode punctuation, no box-drawing.";
}

const char *plat_logo_line(int n)
{
    /* Plain-ASCII arcade ghost (see plat.h); same art as every other
     * target. Monaco renders all of these glyphs at identical cell widths,
     * so it stays aligned when the console scrolls. */
    static const char *L[3] = {"  .---.  ", " | o o | ", " |/\\/\\/| "};
    return L[n];
}

const char *plat_spinner(unsigned i)
{
    static const char *F[4] = {"|", "/", "-", "\\"};
    return F[i & 3];
}

/* --- console ---------------------------------------------------------- */

/* RetroConsole closes its window the instant main() returns; without a
 * pause every error message just flashes. Hook atexit before main() runs. */
static void pause_at_exit(void)
{
    printf("\n(press return to quit)");
    fflush(stdout);
    getchar();
}

/* Retro68's crt does not chdir to the app's folder, so a bare
 * fopen("RELIC.CFG") would look on the boot volume. Point the default
 * directory at our own location so relative paths in core/ work. */
static void chdir_to_app(void)
{
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    FSSpec spec;
    WDPBRec pb;
    if (GetCurrentProcess(&psn) != 0) return;
    memset(&info, 0, sizeof info);
    info.processInfoLength = sizeof info;
    info.processAppSpec = &spec;
    if (GetProcessInformation(&psn, &info) != 0) return;
    memset(&pb, 0, sizeof pb);
    pb.ioVRefNum = spec.vRefNum;
    pb.ioWDDirID = spec.parID;
    PBHSetVolSync(&pb);
}

/* RetroConsole's window is created lazily on the first stdio write with a
 * hard-coded "Retro68 Console" title; force it into existence and rename. */
extern void _ZN5retro11InitConsoleEv(void); /* retro::InitConsole() */

/* RetroConsole hardcodes Monaco 9; cells are 6 px wide, 11 px tall. The
 * window also has a ~2 px inside margin on each side before text starts. */
#define CON_CELL_W 6
#define CON_CELL_H 11
#define CON_MARGIN 4

void plat_init(void)
{
    WindowPtr w;
    chdir_to_app();
    atexit(pause_at_exit);
    _ZN5retro11InitConsoleEv();
    w = FrontWindow();
    if (w) {
        /* RetroConsole creates a fixed, oversized window with no grow box.
         * Pick a size that's large enough for ~110 columns so long API
         * responses don't soft-wrap at 80 cols. */
        SetWTitle(w, "\pRelic");
        MoveWindow(w, 20, 44, false);
        SizeWindow(w, 800, 520, true);
    }
}

int plat_con_size(int *rows, int *cols)
{
    WindowPtr w = FrontWindow();
    Rect r;
    if (!w) return 0;
    r = ((GrafPtr)w)->portRect;
    *cols = ((r.right - r.left) - CON_MARGIN) / CON_CELL_W;
    *rows = ((r.bottom - r.top) - CON_MARGIN) / CON_CELL_H;
    if (*cols < 20 || *rows < 5) return 0;
    return 1;
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
