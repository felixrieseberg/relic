#include "fb.h"
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int fb_open(struct fb *f, const char *dev) {
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo x;
    memset(f, 0, sizeof *f);
    f->fd = open(dev, O_RDWR);
    if (f->fd < 0) return -1;
    if (ioctl(f->fd, FBIOGET_VSCREENINFO, &v) || ioctl(f->fd, FBIOGET_FSCREENINFO, &x)) {
        close(f->fd);
        return -1;
    }
    f->w      = v.xres;
    f->h      = v.yres;
    f->bpp    = v.bits_per_pixel;
    f->stride = x.line_length;
    f->r_off  = v.red.offset;
    f->g_off  = v.green.offset;
    f->b_off  = v.blue.offset;
    f->mem_len = x.smem_len;
    f->mem = (uint8_t *)mmap(0, f->mem_len, PROT_READ | PROT_WRITE, MAP_SHARED, f->fd, 0);
    if (f->mem == MAP_FAILED) {
        close(f->fd);
        return -1;
    }
    fprintf(stderr, "fb: %dx%d %dbpp stride=%d (R@%d G@%d B@%d)\n", f->w, f->h, f->bpp,
            f->stride, f->r_off, f->g_off, f->b_off);
    return 0;
}

void fb_close(struct fb *f) {
    if (f->mem) munmap(f->mem, f->mem_len);
    if (f->fd >= 0) close(f->fd);
}

static inline uint32_t pack32(const struct fb *f, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << f->r_off) | ((uint32_t)g << f->g_off) | ((uint32_t)b << f->b_off);
}

void fb_blit_rgba(struct fb *f, const uint8_t *rgba, int w, int h, int dx, int dy) {
    int cw = (dx + w > f->w) ? f->w - dx : w;
    int ch = (dy + h > f->h) ? f->h - dy : h;
    if (cw <= 0 || ch <= 0) return;
    for (int y = 0; y < ch; y++) {
        const uint8_t *src = rgba + (long)y * w * 4;
        uint8_t *dst = f->mem + (long)(dy + y) * f->stride + (long)dx * (f->bpp / 8);
        if (f->bpp == 32) {
            uint32_t *d = (uint32_t *)dst;
            for (int x = 0; x < cw; x++, src += 4) d[x] = pack32(f, src[0], src[1], src[2]);
        } else if (f->bpp == 16) {
            uint16_t *d = (uint16_t *)dst;
            for (int x = 0; x < cw; x++, src += 4)
                d[x] = (uint16_t)(((src[0] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[2] >> 3));
        }
    }
}

void fb_save(struct fb *f, int y, int h, uint8_t *buf) {
    if (y < 0) y = 0;
    if (y + h > f->h) h = f->h - y;
    if (h > 0) memcpy(buf, f->mem + (long)y * f->stride, (long)h * f->stride);
}

void fb_restore(struct fb *f, int y, int h, const uint8_t *buf) {
    if (y < 0) y = 0;
    if (y + h > f->h) h = f->h - y;
    if (h > 0) memcpy(f->mem + (long)y * f->stride, buf, (long)h * f->stride);
}

void fb_fill(struct fb *f, uint8_t r, uint8_t g, uint8_t b) {
    if (f->bpp == 32) {
        uint32_t px = pack32(f, r, g, b);
        for (int y = 0; y < f->h; y++) {
            uint32_t *d = (uint32_t *)(f->mem + (long)y * f->stride);
            for (int x = 0; x < f->w; x++) d[x] = px;
        }
    } else {
        uint8_t row[4] = {r, g, b, 255};
        for (int y = 0; y < f->h; y++)
            for (int x = 0; x < f->w; x++) fb_blit_rgba(f, row, 1, 1, x, y);
    }
}
