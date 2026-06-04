/* HID boot-keyboard usage ID -> ASCII / PLAT_KEY_*. Shared by every backend
 * that ends up with the standard 8-byte HID boot report (xbox USB, wii IOS
 * /dev/usb/kbd). Static-inline so each TU gets its own copy and there's no
 * extra link object. */
#ifndef HID_KEYMAP_H
#define HID_KEYMAP_H

#include "plat.h"

static int hid_to_key(unsigned char sc, int shift, int ctrl)
{
    if (sc >= 0x04 && sc <= 0x1D) { /* a..z */
        int c = 'a' + (sc - 0x04);
        if (ctrl) return c - 'a' + 1;
        return shift ? (c - 'a' + 'A') : c;
    }
    if (sc >= 0x1E && sc <= 0x26) { /* 1..9 */
        static const char sh[] = "!@#$%^&*(";
        return shift ? sh[sc - 0x1E] : ('1' + sc - 0x1E);
    }
    if (sc == 0x27) return shift ? ')' : '0';
    switch (sc) {
    case 0x28: return '\r';
    case 0x29: return 27;
    case 0x2A: return 8;
    case 0x2B: return '\t';
    case 0x2C: return ' ';
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    case 0x4B: return PLAT_KEY_PGUP;
    case 0x4E: return PLAT_KEY_PGDN;
    case 0x4A: return PLAT_KEY_HOME;
    case 0x4D: return PLAT_KEY_END;
    case 0x52: return PLAT_KEY_UP;
    case 0x51: return PLAT_KEY_DOWN;
    }
    return 0;
}

#endif
