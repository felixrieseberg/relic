/* ptyrun: run a program on a pseudo-terminal so its isatty() checks fire.
 *
 *   ptyrun [-t SECS] [-r ROWS] [-c COLS] -- prog [args...]
 *
 * stdin is read in full up front and fed to the pty master, so the child
 * sees a real tty on fd 0/1/2 with the scripted bytes queued in the line
 * discipline. Child output (stdout+stderr merged) is copied to our stdout
 * unmodified, including ANSI escapes, until the child exits or SECS elapse.
 * Exit status: child's, or 124 on timeout, 125 on harness error.
 *
 * POSIX-only (posix_openpt + select); not part of the C89 core build. */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int read_all_stdin(char **buf, size_t *len)
{
    size_t cap = 4096, n = 0;
    char *b = malloc(cap);
    ssize_t r;
    if (!b) return -1;
    while ((r = read(0, b + n, cap - n)) > 0) {
        n += (size_t)r;
        if (n == cap) {
            cap *= 2;
            b = realloc(b, cap);
            if (!b) return -1;
        }
    }
    *buf = b;
    *len = n;
    return 0;
}

int main(int argc, char **argv)
{
    int timeout = 60, rows = 24, cols = 80;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            timeout = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            rows = atoi(argv[++i]);
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            cols = atoi(argv[++i]);
        else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else
            break;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: ptyrun [-t s] [-r N] [-c N] -- prog args...\n");
        return 125;
    }

    char *in;
    size_t inlen;
    if (read_all_stdin(&in, &inlen) != 0) return 125;

    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) || unlockpt(m)) {
        perror("ptyrun: openpt");
        return 125;
    }
    const char *sn = ptsname(m);
    if (!sn) {
        perror("ptyrun: ptsname");
        return 125;
    }

    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;

    /* Put the line discipline in raw/-echo before queueing input so the
     * pty doesn't echo our script back interleaved with the child's own
     * output. The child's first tcgetattr() then captures these as its
     * "restore" state, which is harmless for relic. */
    struct termios tio;
    if (tcgetattr(m, &tio) == 0) {
        tio.c_lflag &= (unsigned)~(ECHO | ICANON);
        tio.c_oflag &= (unsigned)~ONLCR;
        tcsetattr(m, TCSANOW, &tio);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("ptyrun: fork");
        return 125;
    }
    if (pid == 0) {
        setsid();
        int s = open(sn, O_RDWR);
        if (s < 0) {
            perror("ptyrun: open slave");
            _exit(125);
        }
#ifdef TIOCSCTTY
        ioctl(s, TIOCSCTTY, 0);
#endif
        ioctl(s, TIOCSWINSZ, &ws);
        dup2(s, 0);
        dup2(s, 1);
        dup2(s, 2);
        if (s > 2) close(s);
        close(m);
        setenv("TERM", "xterm", 1);
        execvp(argv[i], &argv[i]);
        perror("ptyrun: exec");
        _exit(125);
    }

    /* Feed scripted input. The kernel pty buffer is large enough for any
     * scenario we'll write; if a write would block we just retry. */
    size_t off = 0;
    while (off < inlen) {
        ssize_t w = write(m, in + off, inlen - off);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        off += (size_t)w;
    }

    /* Drain output until child exits + master drains, or timeout. */
    int status = -1, reaped = 0, idle = 0;
    char buf[4096];
    while (idle < timeout) {
        fd_set rf;
        struct timeval tv;
        FD_ZERO(&rf);
        FD_SET(m, &rf);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int rc = select(m + 1, &rf, NULL, NULL, &tv);
        if (rc > 0) {
            ssize_t r = read(m, buf, sizeof buf);
            if (r > 0) {
                write(1, buf, (size_t)r);
                idle = 0;
                continue;
            }
            /* EIO/0 once the slave side fully closes. */
            if (!reaped && waitpid(pid, &status, 0) == pid) reaped = 1;
            break;
        }
        if (!reaped && waitpid(pid, &status, WNOHANG) == pid) reaped = 1;
        if (reaped) break;
        idle++;
    }
    if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        fprintf(stderr, "ptyrun: timeout after %ds\n", timeout);
        return 124;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 125;
}
