/* Framebuffer drawing primitives backed by xbox_stdio.c. The console text
 * renderer and these share the same 640x480x32 surface; text cells use a
 * 20-px margin, raw pixel calls do not. */
#ifndef XBOX_FB_H
#define XBOX_FB_H
void xbox_fb_dims(int *w, int *h, int *cols, int *rows);
void xbox_fb_clear(void);
void xbox_fb_pixel(int x, int y, unsigned argb);
void xbox_fb_rect(int x, int y, int w, int h, unsigned argb);
/* col/row in text cells; fg=0 means default white. */
void xbox_fb_text(int col, int row, const char *s, unsigned fg);
/* Set the ARGB foreground for subsequent stdout writes (plat_con_attr). */
void xbox_fb_set_fg(unsigned argb);
#endif
