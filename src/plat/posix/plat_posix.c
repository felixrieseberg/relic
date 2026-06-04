/* POSIX implementation of plat.h. Dev/reference build only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "../plat.h"

void plat_init(void)
{
    /* Server RST mid-send would otherwise SIGPIPE-kill us; the http layer
     * handles the EPIPE return and retries on a fresh connection. */
    signal(SIGPIPE, SIG_IGN);
}
char plat_dirsep(void) { return '/'; }

const char *plat_os_desc(void) { return "POSIX host"; }

const char *plat_shell_tool_name(void) { return "Bash"; }

const char *plat_shell_hint(void)
{
    return "The Bash tool runs /bin/sh on this host. The working directory "
           "persists across calls (and applies to Read/Write/LS/Grep/Glob "
           "too), but environment variables and other shell state do not -- "
           "set those inline in the same command.";
}

const char *plat_charset_hint(void)
{
    return "This terminal renders UTF-8, but avoid emoji.";
}

int plat_shell(const char *cmd, char *out, int cap)
{
    FILE *p;
    int n = 0, r, st;
    char *m;
    static char wrapped[4352];
    out[0] = 0;
    /* { } not ( ): a subshell would swallow any cd. Trailer is RS + $PWD so
     * we can chdir() this process to match; exit $_r preserves cmd's code. */
    if (snprintf(wrapped, sizeof wrapped,
                 "{ %s\n} 2>&1; _r=$?; printf '\\036%%s' \"$PWD\"; exit $_r",
                 cmd)
        >= (int)sizeof wrapped)
        return -1;
    p = popen(wrapped, "r");
    if (!p) return -1;
    while (n < cap - 1
           && (r = (int)fread(out + n, 1, (size_t)(cap - 1 - n), p)) > 0)
        n += r;
    out[n] = 0;
    st = pclose(p);
    /* Only honour the RS+$PWD trailer when the buffer didn't fill: if output
     * was truncated, the real trailer is gone and any 0x1E in cmd's own
     * output would be misread as the cwd. */
    m = (n < cap - 1) ? strrchr(out, '\036') : NULL;
    if (m) {
        if (m[1]) chdir(m + 1);
        *m = 0;
    }
    if (st == -1) return -1;
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int plat_list_dir(const char *path, char *out, int cap)
{
    DIR *d;
    struct dirent *e;
    int o = 0, cnt = 0;
    out[0] = 0;
    d = opendir(path);
    if (!d) return -1;
    while ((e = readdir(d))) {
        int l = (int)strlen(e->d_name);
        if (o + l + 3 >= cap) break;
        memcpy(out + o, e->d_name, (size_t)l);
        o += l;
        if (e->d_type == DT_DIR) out[o++] = '/';
        out[o++] = '\n';
        cnt++;
    }
    out[o] = 0;
    closedir(d);
    return cnt;
}

int plat_net_connect(const char *host, unsigned short port)
{
    struct hostent *he;
    struct sockaddr_in sa;
    int s;
    he = gethostbyname(host);
    if (!he || !he->h_addr) return PLAT_NET_EDNS;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return PLAT_NET_ECONN;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, he->h_addr, 4);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(s);
        return PLAT_NET_ECONN;
    }
    return s;
}
int plat_net_send(int h, const void *b, int n)
{
    return (int)send(h, b, (size_t)n, 0);
}
int plat_net_recv(int h, void *b, int n)
{
    return (int)recv(h, b, (size_t)n, 0);
}
int plat_net_wait(int h, int ms)
{
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(h, &rfds);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(h + 1, &rfds, NULL, NULL, &tv);
}
void plat_net_close(int h) { close(h); }

int plat_entropy(unsigned char *out, int len)
{
    int fd, r;
    fd = open("/dev/urandom", O_RDONLY);
    r = -1;
    if (fd >= 0) {
        r = (int)read(fd, out, (size_t)len);
        close(fd);
    }
    return r;
}

unsigned long plat_time_unix(void) { return (unsigned long)time(NULL); }

const char *plat_logo_line(int n)
{
    /* Plain-ASCII arcade ghost (see plat.h); same art as every other
     * target. ASCII outlines stay recognisable at 3 rows tall, where
     * block-glyph silhouettes turn into a blob. */
    static const char *L[3] = {"  .---.  ", " | o o | ", " |/\\/\\/| "};
    return L[n];
}

const char *plat_spinner(unsigned i)
{
    static const char *F[6] = {
        "\xC2\xB7",     /* ·  */
        "\xE2\x9C\xA2", /* ✢ */
        "\xE2\x9C\xB3", /* ✳ */
        "\xE2\x9C\xB6", /* ✶ */
        "\xE2\x9C\xB3", /* ✳ */
        "\xE2\x9C\xA2"  /* ✢ */
    };
    return F[i % 6];
}

int plat_getcwd(char *out, int cap)
{
    return getcwd(out, (size_t)cap) ? (int)strlen(out) : 0;
}

int plat_mkdir(const char *path)
{
    return (mkdir(path, 0700) == 0 || errno == EEXIST) ? 0 : -1;
}

unsigned long plat_file_mode(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (unsigned long)st.st_mode : 0;
}

int plat_set_file_mode(const char *path, unsigned long mode)
{
    if (mode == 0) return 0;
    return chmod(path, (mode_t)(mode & 07777)) == 0 ? 0 : -1;
}

void plat_on_sigint(void (*fn)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn ? fn : SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: let recv()/read() return EINTR */
    sigaction(SIGINT, &sa, NULL);
}

/* One-byte pushback shared by plat_esc_poll/plat_getkey: a tty fd has no
 * peek, so esc_poll must read() to inspect, then park a non-ESC byte here
 * for the next getkey() so type-ahead isn't lost. */
static int g_unget = -1;

int plat_esc_poll(void)
{
    fd_set rfds;
    struct timeval tv;
    unsigned char c;
    if (g_unget >= 0 || !isatty(0)) return 0;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(1, &rfds, NULL, NULL, &tv) <= 0) return 0;
    if (read(0, &c, 1) != 1) return 0;
    if (c == 0x1b) return 1;
    g_unget = c;
    return 0;
}

/* --- console ---------------------------------------------------------- */

int plat_con_size(int *rows, int *cols)
{
    struct winsize ws;
    if (!isatty(0) || ioctl(0, TIOCGWINSZ, &ws) != 0) return 0;
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 1;
}

void plat_con_attr(int a)
{
    static int tty = -1;
    const char *s;
    if (tty < 0) tty = isatty(1);
    if (!tty) return;
    switch (a) {
    case PLAT_ATTR_PROMPT: s = "\033[1m"; break;
    case PLAT_ATTR_TOOL: s = "\033[32m"; break;
    case PLAT_ATTR_DIM: s = "\033[2m"; break;
    default: s = "\033[0m"; break;
    }
    fputs(s, stdout);
}

int plat_con_clear(void)
{
    if (!isatty(1)) return 0;
    fputs("\033[2J\033[H", stdout);
    return 1;
}

void plat_con_backwrap(int cols)
{
    /* Up one row, jump to the last column, erase-to-EOL. EL clears the cell
     * without advancing the cursor, so we never re-trigger autowrap. */
    fprintf(stdout, "\033[A\033[%dG\033[K", cols);
}

static struct termios g_tio_old;
static int g_tio_depth;

static void tio_restore(void)
{
    if (g_tio_depth > 0) tcsetattr(0, TCSANOW, &g_tio_old);
}

int plat_con_raw(int on)
{
    static int hooked;
    struct termios raw;
    if (on) {
        if (g_tio_depth++ > 0) return 1;
        if (!isatty(0) || tcgetattr(0, &g_tio_old) != 0) {
            g_tio_depth = 0;
            return 0;
        }
        if (!hooked) {
            atexit(tio_restore);
            hooked = 1;
        }
        raw = g_tio_old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &raw) != 0) {
            g_tio_depth = 0;
            return 0;
        }
    } else if (g_tio_depth > 0 && --g_tio_depth == 0) {
        tcsetattr(0, TCSANOW, &g_tio_old);
    }
    return 1;
}

static int rd1(unsigned char *c)
{
    if (g_unget >= 0) {
        *c = (unsigned char)g_unget;
        g_unget = -1;
        return 1;
    }
    return read(0, c, 1) == 1;
}

int plat_getkey(void)
{
    unsigned char c;
    if (!rd1(&c)) return 0;
    if (c != 0x1b) return c;
    if (!rd1(&c)) return 27;
    if (c != '[' && c != 'O') {
        g_unget = c;
        return 27;
    }
    if (!rd1(&c)) return 27;
    switch (c) {
    case 'A': return PLAT_KEY_UP;
    case 'B': return PLAT_KEY_DOWN;
    case 'H': return PLAT_KEY_HOME;
    case 'F': return PLAT_KEY_END;
    case '5': (void)read(0, &c, 1); return PLAT_KEY_PGUP;
    case '6': (void)read(0, &c, 1); return PLAT_KEY_PGDN;
    default: return 27;
    }
}
