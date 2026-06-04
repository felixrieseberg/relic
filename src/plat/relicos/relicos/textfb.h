#pragma once
#include "fb.h"

void text_fill(struct fb *F, int x, int y, int w, int h, uint32_t bg_xrgb);
/* Draws str at (x,y); returns pixel width drawn. */
int  text_draw(struct fb *F, int x, int y, const char *str, uint32_t fg_xrgb, uint32_t bg_xrgb);

enum { TEXT_CW = 8, TEXT_CH = 16 };
