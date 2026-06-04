#ifndef CORE_SCROLL_H
#define CORE_SCROLL_H

/* In-app scrollback for consoles with no native buffer (Win95).
 * scroll_out() writes to stdout AND captures into a line ring; scroll_view()
 * takes over the screen for arrow-key browsing. */

/* Write s[0..len) to stdout and capture into the ring (split on '\n',
 * hard-wrap at SCROLL_COLS). */
/* Ring-only (no stdout): use when bytes were already echoed another way. */
void scroll_capture(const char *s, int len);
/* stdout + ring. */
void scroll_out(const char *s, int len);
void scroll_outz(const char *s);
/* printf to stdout + ring. Truncates at 512 chars (vsnprintf). */
void scroll_printf(const char *fmt, ...);
/* Redirect scroll_out's write side (default: stdout). NULL restores default.
 * The ring still captures regardless. */
void scroll_set_sink(void (*write)(const char *, int));

/* Set ring capacity. Call once at startup before any output; resets the ring.
 * lines <= built-in default uses static storage; larger allocates once. */
void scroll_init(int lines);
/* Number of lines currently in the ring. */
int scroll_lines(void);
/* Current ring capacity in lines. */
int scroll_capacity(void);

/* Full-screen viewer: Up/Down/PgUp/PgDn/Home/End, q or Esc to exit.
 * Returns 0 if the platform has no raw console (caller should ignore Up). */
int scroll_view(void);

/* Full-screen pager over an arbitrary buffer (not the ring). Same keys as
 * scroll_view plus j/k/b/space. On exit, redraws the ring tail so the
 * conversation reappears. Returns 0 if raw console unavailable. */
int scroll_pager(const char *buf, int len);

#endif
