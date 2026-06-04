/* Framebuffer drawing primitives backed by plat_wii.c. The Wii's external
 * framebuffer is YUYV (two horizontal pixels share one Cb/Cr pair); these
 * take 0xRRGGBB and convert. The libogc text console renders into the same
 * surface, so printf() and fb.* draw on top of each other. */
#ifndef WII_FB_H
#define WII_FB_H
void wii_fb_dims(int *w, int *h, int *cols, int *rows);
void wii_fb_clear(unsigned rgb);
void wii_fb_pixel(int x, int y, unsigned rgb);
void wii_fb_rect(int x, int y, int w, int h, unsigned rgb);
#endif
