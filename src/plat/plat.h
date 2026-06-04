/* plat.h -- the porting contract.
 * Every target (posix, win32, macppc, xbox, wii) supplies one .c implementing
 * these.
 * core/ includes ONLY this header; never <unistd.h>, <winsock.h>, etc.
 */
#ifndef PLAT_H
#define PLAT_H

/* Upper bound on path lengths the agent handles. Retro filesystems are far
 * shorter; this just sizes char[] buffers consistently across core/. */
#define PLAT_PATH_MAX 512

/* One-time platform setup; called first thing in main(). May chdir, register
 * atexit handlers, etc. No-op on platforms that need none. */
void plat_init(void);

enum {
    PLAT_NET_EDNS = -2, /* hostname resolution failed */
    PLAT_NET_ECONN = -3 /* TCP connect() failed */
};

/* TCP: connect to host:port (blocking). Returns opaque handle >=0, or one of
 * the PLAT_NET_E* codes on failure. */
int plat_net_connect(const char *host, unsigned short port);
/* Human-readable detail (stage + native errno) for the last net failure. */
const char *plat_net_errdetail(void);
/* Send up to len bytes. Returns bytes sent (>0) or -1 on error. */
int plat_net_send(int h, const void *buf, int len);
/* Recv up to len bytes. Returns bytes read (>0), 0 on EOF, -1 on error. */
int plat_net_recv(int h, void *buf, int len);
/* Wait up to ms for h to become readable. Returns >0 ready, 0 timeout,
 * <0 error/interrupt. */
int plat_net_wait(int h, int ms);
void plat_net_close(int h);

/* Fill out[0..len) with the best entropy the platform has. Returns bytes. */
int plat_entropy(unsigned char *out, int len);

/* Seconds since 1970-01-01, best effort (may be wildly wrong on retro RTC). */
unsigned long plat_time_unix(void);

/* Fetch API key into out (NUL-terminated). Returns strlen, or 0 if absent. */
int plat_get_api_key(char *out, int cap);

/* Read key=value from RELIC.CFG. Returns strlen of value or 0 if absent.
 * PLAT_CFG_TRUSTED restricts the lookup to the home-directory file (skipping
 * the cwd fallback) so a checked-out repo can't supply security-relevant
 * keys. */
#define PLAT_CFG_TRUSTED 1
int plat_get_cfg(const char *key, char *out, int cap, int flags);

/* Run cmd via the platform shell, capture combined stdout+stderr into out
 * (NUL-terminated, cap-bounded). Returns process exit code, or -1.
 * The process working directory PERSISTS: if cmd changes directory, this
 * function chdir()s the relic process to match before returning, so the
 * next plat_shell (and Read/LS/Grep with relative paths) start there. */
int plat_shell(const char *cmd, char *out, int cap);

/* List entries in path, one per line, into out. Directory entries are
 * suffixed with plat_dirsep(). Returns count or -1. */
int plat_list_dir(const char *path, char *out, int cap);

/* Native path separator ('/' POSIX, '\\' Win9x, ':' classic Mac). core/
 * accepts all three on input but joins with this when building paths. */
char plat_dirsep(void);

/* Short OS description for the system prompt. */
const char *plat_os_desc(void);
/* Name to expose the shell tool under ("Bash" where it really is bash,
 * "Shell" otherwise). */
const char *plat_shell_tool_name(void);
/* One sentence for the system prompt clarifying what the shell tool actually
 * invokes here (e.g. COMMAND.COM caveats on Win95). */
const char *plat_shell_hint(void);
/* One sentence for the system prompt describing what character encoding the
 * console can actually render (UTF-8 on POSIX, CP437/ASCII on Win95). */
const char *plat_charset_hint(void);

/* 3 lines of the mascot logo: a little arcade ghost (domed head, two eyes,
 * zigzag skirt). Plain 7-bit ASCII so every console renders it identically;
 * 9 columns per line so the banner text stays aligned. */
const char *plat_logo_line(int n);

/* Spinner frame i (wraps internally). One visual column, codepage-encoded. */
const char *plat_spinner(unsigned i);

/* Current working directory into out (NUL-terminated). */
int plat_getcwd(char *out, int cap);

/* Create directory (no parents). Returns 0 on success or already-exists. */
int plat_mkdir(const char *path);

/* Permission bits of an existing path, for restoring after a
 * write-temp-then-rename. Returns the platform's mode word, or 0 if the
 * path doesn't exist or the platform has no concept of file modes. */
unsigned long plat_file_mode(const char *path);
/* Apply mode (from plat_file_mode) to path. No-op when mode==0 or the
 * platform has no file modes. Returns 0 on success/no-op, -1 on error. */
int plat_set_file_mode(const char *path, unsigned long mode);

/* Install fn as the Ctrl+C handler such that blocking I/O is interrupted
 * (no SA_RESTART). fn=0 restores the default. */
void plat_on_sigint(void (*fn)(int));

/* Non-blocking: drain any pending console keystrokes; return 1 if ESC was
 * among them, else 0 (also 0 if stdin is not a tty / no raw input). Called
 * from the network poll loop so ESC can abort an in-flight request. */
int plat_esc_poll(void);

/* --- console (for in-app scrollback; all may return 0 = unsupported) --- */
enum {
    PLAT_KEY_UP = 0x100,
    PLAT_KEY_DOWN,
    PLAT_KEY_PGUP,
    PLAT_KEY_PGDN,
    PLAT_KEY_HOME,
    PLAT_KEY_END
};
/* Enter/leave raw (unbuffered, no-echo) keyboard mode. Returns 1 if
 * supported (and now active/restored), 0 if not (e.g. stdin not a tty). */
int plat_con_raw(int on);
/* Blocking single-key read; call only between plat_con_raw(1)/(0). Returns
 * ASCII (1..255), one of PLAT_KEY_*, or 0 on read error. */
int plat_getkey(void);
/* Fill *rows,*cols with console size. Returns 1 if known, 0 if not. */
int plat_con_size(int *rows, int *cols);
/* Clear screen and home the cursor. Returns 1 if done, 0 if unsupported. */
int plat_con_clear(void);
/* Move the cursor to the last column of the previous physical line and
 * clear that cell. Called by the line editor when backspace must cross a
 * soft-wrap boundary (BS is a no-op at column 0 on most terminals). */
void plat_con_backwrap(int cols);

/* Set the text attribute for subsequent stdout writes. No-op when stdout is
 * not a tty or the platform has no notion of color. RESET must be called
 * before the process exits or the user's shell inherits the last color. */
#define PLAT_ATTR_RESET 0
#define PLAT_ATTR_PROMPT 1 /* the "> " and user-typed text */
#define PLAT_ATTR_TOOL 2   /* "[tool] Name  preview" header lines */
#define PLAT_ATTR_DIM 3    /* secondary chrome (banners, status) */
void plat_con_attr(int a);

#endif
