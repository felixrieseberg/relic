/* USB HID boot-keyboard driver for plat_xbox.c via nxdk's libusbohci.
 * On each 8-byte interrupt-in report, diff against the previous report
 * to detect new keydowns and push ASCII / PLAT_KEY_* into a ring buffer.
 * No auto-repeat (typematic): core's line editor handles single keystrokes. */

#include <string.h>
#include <xboxkrnl/xboxkrnl.h>
#include <usbh_lib.h>
#include <usbh_hid.h> /* HID_DEV_T struct body (bSubClassCode etc.) */

#include "xbox_kbd.h"
#include "../plat.h"
#include "../hid_keymap.h"

#define RING_CAP 64

static volatile int g_ring[RING_CAP];
static volatile unsigned g_head, g_tail;

/* One-key pushback for plat_esc_poll: non-ESC reads must go back so the
 * next plat_getkey() sees them (type-ahead). Mirrors the posix pattern. */
static int g_unget = -1;

static struct usbhid_dev *g_kbd;
static unsigned char g_prev[8];

static void ring_push(int c)
{
    unsigned n = (g_head + 1) % RING_CAP;
    if (n == g_tail) return; /* full; drop */
    g_ring[g_head] = c;
    g_head = n;
}

static int ring_pop(void)
{
    int c;
    if (g_unget >= 0) {
        c = g_unget;
        g_unget = -1;
        return c;
    }
    if (g_head == g_tail) return -1;
    c = g_ring[g_tail];
    g_tail = (g_tail + 1) % RING_CAP;
    return c;
}

/* Interrupt-in callback. Called from usbh_pooling_hubs() context, so pump
 * that regularly to drive input. */
static void kbd_irq_cb(struct usbhid_dev *hdev, uint16_t ep, int status,
                       uint8_t *data, uint32_t len)
{
    int shift, ctrl, i, j;
    (void)hdev;
    (void)ep;
    if (status != 0 || len < 8) return;
    shift = (data[0] & 0x22) != 0; /* LShift|RShift */
    ctrl = (data[0] & 0x11) != 0;  /* LCtrl|RCtrl */
    for (i = 2; i < 8; i++) {
        unsigned char sc = data[i];
        int held = 0;
        if (sc == 0 || sc == 0x01 /* ErrorRollOver */) continue;
        for (j = 2; j < 8; j++)
            if (g_prev[j] == sc) {
                held = 1;
                break;
            }
        if (!held) {
            int k = hid_to_key(sc, shift, ctrl);
            if (k) ring_push(k);
        }
    }
    memcpy(g_prev, data, 8);
}

static void hid_conn_cb(struct usbhid_dev *hdev, int param)
{
    (void)param;
    /* Interface descriptor: bInterfaceSubClass=1 (boot), bInterfaceProtocol=1
     * (keyboard). Grab the first match; ignore later HID devices. */
    if (g_kbd || hdev->bSubClassCode != 1 || hdev->bProtocolCode != 1) return;
    g_kbd = hdev;
    (void)usbh_hid_set_protocol(hdev, 0); /* boot protocol (fixed 8-byte) */
    (void)usbh_hid_start_int_read(hdev, 0, kbd_irq_cb);
}

static void hid_disc_cb(struct usbhid_dev *hdev, int param)
{
    (void)param;
    if (g_kbd == hdev) g_kbd = NULL;
}

void xbox_kbd_init(void)
{
    int i;
    usbh_core_init();
    usbh_hid_init();
    usbh_install_hid_conn_callback(hid_conn_cb, hid_disc_cb);
    /* Pump enumeration so a plug-at-boot keyboard is visible before the
     * first prompt. A keyboard hot-plugged later is picked up by the poll
     * inside plat_getkey(). */
    for (i = 0; i < 500; i++)
        usbh_pooling_hubs();
}

int xbox_kbd_poll(void) /* returns 1 if a key is waiting, else 0 */
{
    usbh_pooling_hubs();
    return g_unget >= 0 || g_head != g_tail;
}

int xbox_kbd_get(void) /* -1 if empty, else key code */ { return ring_pop(); }

void xbox_kbd_unget(int c) /* pushback; consumed by next get */ { g_unget = c; }

/* Return 1 and remove the first ESC from the pending input (unget slot +
 * ring), otherwise 0. Non-ESC keys are preserved in their original order
 * so the next plat_getkey() still sees them. Used by plat_esc_poll so a
 * stashed non-ESC key can't starve a later ESC. */
int xbox_kbd_take_esc(void)
{
    unsigned i;
    (void)usbh_pooling_hubs();
    if (g_unget == 27) {
        g_unget = -1;
        return 1;
    }
    for (i = g_tail; i != g_head; i = (i + 1) % RING_CAP) {
        if (g_ring[i] == 27) {
            /* Remove the ESC while preserving order: shift later entries
             * one slot forward. Single consumer (main thread) -- safe. */
            unsigned j = i;
            unsigned next;
            while ((next = (j + 1) % RING_CAP) != g_head) {
                g_ring[j] = g_ring[next];
                j = next;
            }
            g_head = (g_head + RING_CAP - 1) % RING_CAP;
            return 1;
        }
    }
    return 0;
}

int xbox_kbd_present(void) /* 1 after a keyboard has connected */
{
    return g_kbd != NULL;
}
