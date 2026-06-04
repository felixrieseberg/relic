#ifndef CORE_UI_H
#define CORE_UI_H

/* Single-line status spinner. label sets/changes the phase word; NULL keeps
 * the current label and just advances the glyph. No-op when g_verbose (vtrace
 * already narrates). spin_clear() erases the line and resets the timer. */
void spin(const char *label);
void spin_clear(void);
/* Replace the renderer (default: \r line on stderr). secs is wall-clock since
 * the current run of spin() calls began. NULL restores the default. */
void spin_set_sink(void (*sink)(const char *label, unsigned secs));

/* Error-message sink for core/. Default writes to stderr; embedders override.
 * Appends '\n'. */
void errf(const char *fmt, ...);
void errf_set_sink(void (*sink)(const char *line));

/* -v: trace each step. Kept global; read by vtrace() at many depths. */
extern int g_verbose;
void vtrace(const char *fmt, ...);
/* Dump a (possibly multi-line / unterminated) buffer with ".   " prefix per
 * line. No-op unless g_verbose >= 2. */
void vdump(const char *label, const char *buf, int len);

#endif
