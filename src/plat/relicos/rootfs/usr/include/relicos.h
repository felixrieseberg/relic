/* relicos.h - the RelicOS platform ABI for guest programs.
 *
 * Single header; #include from tcc programs. Covers the three things a
 * RelicOS program typically needs:
 *   - the framebuffer canvas (rows 0..CANVAS_H-1 of /dev/fb0, 32bpp XRGB)
 *   - the shared window registry (/tmp/windows) so independent programs
 *     can lay themselves out without a window manager
 *   - the persistent disk root
 *
 * Everything below the canvas row is owned by the shell; never write past
 * CANVAS_H. Layout policy is yours -- ros_place()/ros_window() are just a
 * left-to-right shelf suggestion, not a requirement. */
#ifndef RELICOS_H
#define RELICOS_H
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

typedef struct { unsigned *px; int w, h, canvas_h; } ros_fb;
typedef struct { int x, y, w, h; } rect;

#define ROS_WINDOWS "/tmp/windows"   /* registry: "x y w h pid title\n" */
#define ROS_DATA    "/data"          /* persistent ext4; survives reboot */

static inline int ros__env(const char *k) {
    const char *v = getenv(k); return v ? atoi(v) : 0;
}

/* mmap /dev/fb0 and read FB_W/FB_H/CANVAS_H from the environment. 0 / -1. */
static int ros_open(ros_fb *c) {
    int fd;
    c->w = ros__env("FB_W"); c->h = ros__env("FB_H");
    c->canvas_h = ros__env("CANVAS_H");
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0 || !c->w || !c->canvas_h) return -1;
    c->px = (unsigned *)mmap(0, (size_t)c->w * c->h * 4,
                             PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return c->px == MAP_FAILED ? -1 : 0;
}

/* Set one pixel; clipped to the canvas region. */
static inline void ros_put(ros_fb *c, int x, int y, unsigned rgb) {
    if ((unsigned)x < (unsigned)c->w && (unsigned)y < (unsigned)c->canvas_h)
        c->px[y * c->w + x] = rgb;
}

/* Fill a rect with a solid colour; clipped to the canvas region. */
static void ros_fill(ros_fb *c, rect r, unsigned rgb) {
    int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w, y1 = r.y + r.h;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->canvas_h) y1 = c->canvas_h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) c->px[y * c->w + x] = rgb;
}

/* Record a rect in ROS_WINDOWS so other programs (and the `Windows` tool)
 * see it. Draw-once-then-exit programs should still claim - the entry
 * outlives the pid. */
static int ros_claim(rect r, const char *title) {
    FILE *f = fopen(ROS_WINDOWS, "a");
    if (!f) return -1;
    fprintf(f, "%d %d %d %d %d %s\n", r.x, r.y, r.w, r.h, getpid(),
            title ? title : "?");
    fclose(f);
    return 0;
}

/* Suggest a free w*h rect using a left-to-right shelf layout over existing
 * registry entries. Returns r.w == 0 if it won't fit. Ignore this and pick
 * your own coords if you have a layout in mind. */
static rect ros_place(const ros_fb *c, int w, int h) {
    int sy = 0, sh = 0, cx = 0, x, y, rw, rh, pid; char t[64];
    rect r = {0, 0, w, h};
    FILE *f = fopen(ROS_WINDOWS, "r");
    if (f) {
        while (fscanf(f, "%d %d %d %d %d %63[^\n] ",
                      &x, &y, &rw, &rh, &pid, t) == 6) {
            if (y != sy) { sy = y; sh = 0; cx = 0; }
            if (x + rw > cx) cx = x + rw;
            if (rh > sh) sh = rh;
        }
        fclose(f);
    }
    if (cx + w <= c->w) { r.x = cx; r.y = sy; }
    else                { r.x = 0;  r.y = sy + sh; }
    if (r.y + h > c->canvas_h) r.w = r.h = 0;
    return r;
}

/* place + claim in one step. */
static rect ros_window(const ros_fb *c, int w, int h, const char *title) {
    rect r = ros_place(c, w, h);
    if (r.w) ros_claim(r, title);
    return r;
}

#endif
