#pragma once
#include "fb.h"

enum { LOG_LINES = 64 };

void log_set_cols(int cols);                     /* wrap width; call once from layout */
void log_push(char tag, const char *s);          /* tag: '>'=you  '<'=model  ':'=tool  ' '=info */
void log_pushf(char tag, const char *fmt, ...);
/* Paint the last rows that fit into (x,y,w,h). */
void log_paint(struct fb *F, int x, int y, int w, int h);
