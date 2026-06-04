#pragma once
#include <stdint.h>

struct fb {
    int      fd;
    int      w, h, stride;
    int      bpp;       /* bits per pixel: 16 or 32 */
    int      r_off, g_off, b_off;
    uint8_t *mem;
    long     mem_len;
};

int  fb_open(struct fb *f, const char *dev);
void fb_close(struct fb *f);
/* Copy an RGBA8888 buffer (w*h*4 bytes, row-major) to the framebuffer at (dx,dy). */
void fb_blit_rgba(struct fb *f, const uint8_t *rgba, int w, int h, int dx, int dy);
void fb_fill(struct fb *f, uint8_t r, uint8_t g, uint8_t b);
/* Snapshot/restore raw fb rows [y, y+h). buf is h*stride bytes. */
void fb_save(struct fb *f, int y, int h, uint8_t *buf);
void fb_restore(struct fb *f, int y, int h, const uint8_t *buf);
