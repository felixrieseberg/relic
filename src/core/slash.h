#ifndef CORE_SLASH_H
#define CORE_SLASH_H

/* Slash-command dispatcher shared by relic (TTY) and relicos (framebuffer).
 * Output goes through scroll_printf(); relicos redirects that via
 * scroll_set_sink(). Handlers stay per-binary -- the table, parser and /help
 * renderer are the shared part. */

/* Handler returns: 0 continue REPL, 1 quit. arg is "" if no argument. */
typedef int (*slash_fn)(const char *arg);

typedef struct {
    const char *name, *args, *help; /* help "" hides from /help listing */
    slash_fn fn;
} slash_cmd;

void slash_help(const slash_cmd *cmds, int ncmds);
/* -1 if line isn't a slash command; else handler's return (0 or 1). */
int slash_dispatch(const slash_cmd *cmds, int ncmds, const char *line);

#endif
