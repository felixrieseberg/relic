#include "evdev.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int has_letter_keys(int fd) {
    unsigned long kb[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof kb), kb) < 0) return 0;
    auto bit = [&](int k) { return (kb[k / (8 * sizeof(long))] >> (k % (8 * sizeof(long)))) & 1; };
    return bit(KEY_A) && bit(KEY_ENTER);
}

int ev_open_keyboard(void) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int fd = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int f = open(path, O_RDONLY);
        if (f < 0) continue;
        if (has_letter_keys(f)) {
            ioctl(f, EVIOCGRAB, 1);
            char name[64] = "?";
            ioctl(f, EVIOCGNAME(sizeof name), name);
            fprintf(stderr, "kbd: %s (%s)\n", path, name);
            fd = f;
            break;
        }
        close(f);
    }
    closedir(d);
    return fd;
}

void ev_grab(int fd, int on) { if (fd >= 0) ioctl(fd, EVIOCGRAB, on); }
void ev_close(int fd) { if (fd >= 0) close(fd); }

/* Minimal US keymap, indexed by linux/input KEY_* (1..58 covers the basics). */
static const char km_lo[] =
    "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./";
static const char km_hi[] =
    "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|"  "ZXCVBNM<>?";

static int g_shift;

int ev_read_key(int fd) {
    struct input_event ev;
    if (read(fd, &ev, sizeof ev) != (ssize_t)sizeof ev) return 0;
    if (ev.type != EV_KEY) return 0;
    int code = ev.code;
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) { g_shift = (ev.value != 0); return 0; }
    if (ev.value != 1) return 0; /* only key-press; ignore release & autorepeat for now */
    switch (code) {
        case KEY_ENTER:
        case KEY_KPENTER:   return EVK_ENTER;
        case KEY_BACKSPACE: return EVK_BKSP;
        case KEY_ESC:       return EVK_ESC;
        case KEY_TAB:       return EVK_TAB;
        case KEY_SPACE:     return ' ';
    }
    if (code > 0 && code < (int)sizeof km_lo) {
        char c = (g_shift ? km_hi : km_lo)[code];
        if (c) return (unsigned char)c;
    }
    return 0;
}

int ev_poll_key(int fd) {
    struct pollfd p = {fd, POLLIN, 0};
    while (poll(&p, 1, 0) > 0) {
        int k = ev_read_key(fd);
        if (k) return k;
    }
    return 0;
}

int ev_wait_key(int fd, int timeout_ms) {
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, timeout_ms) <= 0) return 0;
    return ev_poll_key(fd);
}
