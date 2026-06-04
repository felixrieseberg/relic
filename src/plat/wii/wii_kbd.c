/* Keyboard input for the Wii backend. Two independent sources feed the same
 * ring buffer:
 *
 *   1. The GameCube ASCII keyboard controller, polled over SI. This is the
 *      path Dolphin actually implements cross-platform (host keyboard ->
 *      emulated SI keyboard on a controller port). Dolphin's /dev/usb/kbd
 *      HLE is Windows-only (IsKeyPressed() is `return false;` elsewhere),
 *      so SI is the only thing that works on macOS/Linux Dolphin.
 *
 *   2. IOS /dev/usb/kbd, the dedicated USB-keyboard resource. Real hardware
 *      with a USB keyboard speaks this; ensure_kbd_ios() in plat_wii.c
 *      reloads to an IOS that exposes it.
 *
 * Either or both may be present; both push into g_ring. The IOS callback
 * runs on libogc's IPC reply thread, so ring writes are guarded with
 * _CPU_ISR_Disable -- the SPSC invariant the Xbox driver relies on doesn't
 * hold here. */
#include <string.h>
#include <gccore.h>
#include <ogc/machine/processor.h> /* _CPU_ISR_Disable/_CPU_ISR_Restore */
#include <ogc/ipc.h>
#include <ogc/si.h>

#include "wii_kbd.h"
#include "../plat.h"
#include "../hid_keymap.h"

#define RING_CAP 64

static volatile int g_ring[RING_CAP];
static volatile unsigned g_head, g_tail;

static void ring_push(int c)
{
    u32 level;
    unsigned n;
    _CPU_ISR_Disable(level);
    n = (g_head + 1) % RING_CAP;
    if (n != g_tail) {
        g_ring[g_head] = c;
        g_head = n;
    }
    _CPU_ISR_Restore(level);
}

/* --- SI backend: GameCube ASCII keyboard controller (ASC-1901) -------- */

static int g_si_chan = -1;
static unsigned char g_si_prev[3];
/* Scancodes that are reported as held on EVERY sample during init -- treated
 * as ghosts (Dolphin's macOS Quartz backend reports keycode 0 = 'A' as
 * permanently held; this also covers Caps Lock state etc.). */
static unsigned char g_si_stuck[3];
static int g_si_stuck_n;

static int is_stuck(unsigned char sc)
{
    int i;
    for (i = 0; i < g_si_stuck_n; i++)
        if (g_si_stuck[i] == sc) return 1;
    return 0;
}

/* GC-keyboard scancode -> ASCII / PLAT_KEY_*. The controller reports up to
 * three held keys per poll; modifiers (Shift/Ctrl) arrive as scancodes too,
 * not as a separate bitmap. Table matches the ASC-1901 layout (== Dolphin's
 * KeyScanCode enum). */
static int gck_to_key(unsigned char sc, int shift, int ctrl)
{
    if (sc >= 0x10 && sc <= 0x29) { /* A..Z */
        int c = 'a' + (sc - 0x10);
        if (ctrl) return c - 'a' + 1;
        return shift ? (c - 'a' + 'A') : c;
    }
    if (sc >= 0x2A && sc <= 0x32) { /* 1..9 */
        static const char sh[] = "!@#$%^&*(";
        return shift ? sh[sc - 0x2A] : ('1' + sc - 0x2A);
    }
    if (sc == 0x33) return shift ? ')' : '0';
    switch (sc) {
    case 0x06: return PLAT_KEY_HOME;
    case 0x07: return PLAT_KEY_END;
    case 0x08: return PLAT_KEY_PGUP;
    case 0x09: return PLAT_KEY_PGDN;
    case 0x34: return shift ? '_' : '-';
    case 0x35: return shift ? '+' : '=';
    case 0x37: return shift ? '{' : '[';
    case 0x38: return shift ? '}' : ']';
    case 0x39: return shift ? ':' : ';';
    case 0x3A: return shift ? '"' : '\'';
    case 0x3B: return shift ? '|' : '\\';
    case 0x3C: return shift ? '<' : ',';
    case 0x3D: return shift ? '>' : '.';
    case 0x3E: return shift ? '?' : '/';
    case 0x4C: return 27;
    case 0x4E: return 8; /* Delete -> backspace */
    case 0x4F: return shift ? '~' : '`';
    case 0x50: return 8;
    case 0x51: return '\t';
    case 0x59: return ' ';
    case 0x5C: return PLAT_KEY_HOME; /* no LEFT/RIGHT in plat.h; map sane */
    case 0x5D: return PLAT_KEY_DOWN;
    case 0x5E: return PLAT_KEY_UP;
    case 0x5F: return PLAT_KEY_END;
    case 0x61: return '\r';
    }
    return 0;
}

static void si_kbd_init(void)
{
    int ch;
    for (ch = 0; ch < SI_MAX_CHAN; ch++) {
        if (SI_DecodeType(SI_GetType(ch)) == SI_GC_KEYBOARD) {
            g_si_chan = ch;
            /* CMD_POLL = 0x54 in the SI command opcode byte. Hardware (or
             * Dolphin) sends this every sample once polling is enabled. */
            SI_SetCommand(ch, 0x00540000);
            SI_EnablePolling(SI_CHAN_BIT(ch));
            SI_TransferCommands();
            break;
        }
    }
    if (g_si_chan < 0) return;
    /* Stuck-key calibration: any scancode seen on every one of the first
     * ~20 valid samples is a ghost. */
    {
        u32 buf[2];
        unsigned char *b = (unsigned char *)buf;
        unsigned char acc[3] = {0, 0, 0};
        int got = 0, frames = 0, i, j;
        /* Cap by wall-clock frames too: a keyboard that answered the type
         * probe but never yields RDST would otherwise hang boot here. */
        while (got < 20 && frames < 60) {
            VIDEO_WaitVSync();
            frames++;
            if (!SI_GetResponse(g_si_chan, buf)) continue;
            if (got == 0) {
                acc[0] = b[4];
                acc[1] = b[5];
                acc[2] = b[6];
            } else {
                /* keep only scancodes still present this frame */
                for (i = 0; i < 3; i++) {
                    int still = 0;
                    for (j = 4; j < 7; j++)
                        if (acc[i] && acc[i] == b[j]) still = 1;
                    if (!still) acc[i] = 0;
                }
            }
            got++;
        }
        for (i = 0; i < 3; i++)
            if (acc[i]) g_si_stuck[g_si_stuck_n++] = acc[i];
        memcpy(g_si_prev, acc, 3);
    }
}

static void si_kbd_poll(void)
{
    u32 buf[2];
    unsigned char *b = (unsigned char *)buf;
    unsigned char sc[3];
    int shift = 0, ctrl = 0, i, j;
    if (g_si_chan < 0) return;
    if (!SI_GetResponse(g_si_chan, buf)) return;
    /* low word = key0 key1 key2 checksum (big-endian). */
    sc[0] = b[4];
    sc[1] = b[5];
    sc[2] = b[6];
    for (i = 0; i < 3; i++)
        if (is_stuck(sc[i])) sc[i] = 0;
    for (i = 0; i < 3; i++) {
        if (sc[i] == 0x54 || sc[i] == 0x55) shift = 1;
        if (sc[i] == 0x56) ctrl = 1;
    }
    for (i = 0; i < 3; i++) {
        int held = 0;
        if (sc[i] == 0) continue;
        for (j = 0; j < 3; j++)
            if (g_si_prev[j] == sc[i]) {
                held = 1;
                break;
            }
        if (!held) {
            int k = gck_to_key(sc[i], shift, ctrl);
            if (k) ring_push(k);
        }
    }
    memcpy(g_si_prev, sc, 3);
}

/* --- IOS backend: /dev/usb/kbd ---------------------------------------- */

static s32 g_usb_fd = -1;
static unsigned char g_usb_prev[6];
static u8 g_usb_msg[16] ATTRIBUTE_ALIGN(32);

static s32 usb_kbd_cb(s32 result, void *userdata);

static void usb_kbd_arm(void)
{
    if (g_usb_fd < 0) return;
    IOS_IoctlAsync(g_usb_fd, 0, NULL, 0, g_usb_msg, sizeof g_usb_msg,
                   usb_kbd_cb, NULL);
}

static s32 usb_kbd_cb(s32 result, void *userdata)
{
    (void)userdata;
    if (result < 0) {
        /* Device gone / ioctl rejected -- don't re-arm into a tight IPC
         * spin on the reply thread. */
        IOS_Close(g_usb_fd);
        g_usb_fd = -1;
        return 0;
    }
    if (g_usb_msg[3] == 2) {
        int shift = (g_usb_msg[8] & 0x22) != 0;
        int ctrl = (g_usb_msg[8] & 0x11) != 0;
        int i, j;
        for (i = 0; i < 6; i++) {
            unsigned char sc = g_usb_msg[10 + i];
            int held = 0;
            if (sc == 0 || sc == 0x01) continue;
            for (j = 0; j < 6; j++)
                if (g_usb_prev[j] == sc) {
                    held = 1;
                    break;
                }
            if (!held) {
                int k = hid_to_key(sc, shift, ctrl);
                if (k) ring_push(k);
            }
        }
        memcpy(g_usb_prev, g_usb_msg + 10, 6);
    }
    usb_kbd_arm();
    return 0;
}

static void usb_kbd_init(void)
{
    g_usb_fd = IOS_Open("/dev/usb/kbd", 0);
    usb_kbd_arm();
}

/* --- public ----------------------------------------------------------- */

void wii_kbd_init(void)
{
    si_kbd_init();
    usb_kbd_init();
}

int wii_kbd_get(void)
{
    int c;
    si_kbd_poll();
    if (g_head == g_tail) return -1;
    c = g_ring[g_tail];
    g_tail = (g_tail + 1) % RING_CAP;
    return c;
}

int wii_kbd_take_esc(void)
{
    u32 level;
    unsigned i;
    int hit = 0;
    si_kbd_poll();
    _CPU_ISR_Disable(level);
    for (i = g_tail; i != g_head; i = (i + 1) % RING_CAP) {
        if (g_ring[i] == 27) {
            unsigned j = i, next;
            while ((next = (j + 1) % RING_CAP) != g_head) {
                g_ring[j] = g_ring[next];
                j = next;
            }
            g_head = (g_head + RING_CAP - 1) % RING_CAP;
            hit = 1;
            break;
        }
    }
    _CPU_ISR_Restore(level);
    return hit;
}

int wii_kbd_present(void) { return g_si_chan >= 0 || g_usb_fd >= 0; }
