/* Relic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/anth.h"
#include "core/conv.h"
#include "core/cfg.h"
#include "core/agent.h"
#include "core/netcfg.h"
#include "core/tools.h"
#include "core/scroll.h"
#include "core/sess.h"
#include "core/slash.h"
#include "core/ui.h"
#include "core/util.h"
#include "plat/plat.h"

#define RELIC_VERSION "0.1"

/* Zero-init; nonzero defaults are set at the top of main(). No
 * designated initializers, and a long positional list silently breaks if
 * relic_cfg fields are ever reordered. */
static relic_cfg g_cfg;
static conv_t g_conv;
static agent_scratch g_scratch;

static void on_sigint(int sig)
{
    (void)sig;
    g_agent_interrupt = 1;
}

/* "default"/"acceptEdits"/"bypassPermissions" -> set agent flags. 0 / -1. */
static int set_permission_mode(const char *m)
{
    if (strcmp(m, "default") == 0)
        g_cfg.accept_edits = g_cfg.yolo = 0;
    else if (strcmp(m, "acceptEdits") == 0)
        g_cfg.accept_edits = 1;
    else if (strcmp(m, "bypassPermissions") == 0)
        g_cfg.yolo = 1;
    else
        return -1;
    return 0;
}

static void print_banner(int chat_only)
{
    char cwd[256];
    if (!plat_getcwd(cwd, (int)sizeof cwd)) strcpy(cwd, ".");
    scroll_printf("\n");
    scroll_printf(" %s   Relic v%s (%s)\n", plat_logo_line(0), RELIC_VERSION,
                  __DATE__);
    scroll_printf(" %s   %s . %s\n", plat_logo_line(1), g_cfg.model,
                  plat_os_desc());
    scroll_printf(" %s   %s\n", plat_logo_line(2), cwd);
    scroll_printf("\n");
    scroll_printf("  Type / to see available commands%s\n\n",
                  chat_only ? "  (chat-only mode)" : "");
    fflush(stdout);
}

/* Render the user-tweakable config lines (sc_status / sc_export). */
static int fmt_cfg(char *out, int cap)
{
    char proxy[80];
    int n;
    if (g_cfg.net.proxy_host[0])
        snprintf(proxy, sizeof proxy, "%s:%u", g_cfg.net.proxy_host,
                 (unsigned)g_cfg.net.proxy_port);
    else
        strcpy(proxy, "(direct)");
    n = snprintf(out, (size_t)cap,
                 "  model   : %s\n"
                 "  proxy   : %s\n"
                 "  ip      : %s\n"
                 "  verbose : %d\n"
                 "  yolo    : %s\n"
                 "  edits   : %s\n"
                 "  tools   : %s\n"
                 "  max_tokens : %d\n",
                 g_cfg.model, proxy,
                 g_cfg.net.host_ip[0] ? g_cfg.net.host_ip : "(DNS)", g_verbose,
                 g_cfg.yolo ? "ON" : "off",
                 g_cfg.accept_edits ? "auto-accept" : "ask",
                 g_cfg.chat_only ? "off (chat-only)" : "on", g_cfg.max_tokens);
    return (n < 0 || n >= cap) ? cap - 1 : n;
}

/* --- slash commands (dispatch + /help live in core/slash.c) ---------- */
static int sc_quit(const char *a)
{
    (void)a;
    return 1;
}
static int sc_reset(const char *a)
{
    (void)a;
    conv_reset(&g_conv);
    scroll_printf("(history cleared)\n");
    return 0;
}
static int sc_net(const char *a)
{
    (void)a;
    net_selftest(&g_cfg.net);
    return 0;
}
static int sc_test(const char *a)
{
    if (strcmp(a, "network") == 0 || strcmp(a, "net") == 0) return sc_net("");
    if (strcmp(a, "tools") == 0) {
        tools_selftest();
        return 0;
    }
    scroll_printf("usage: /test SUBCOMMAND\n"
                  "  network   run network self-test\n"
                  "  tools     run tools self-test (shell, Read/Write/Edit)\n");
    return 0;
}
static int sc_proxy(const char *a)
{
    net_set_proxy(&g_cfg.net, a);
    if (*a)
        scroll_printf("(proxy set to %s:%u)\n", g_cfg.net.proxy_host,
                      (unsigned)g_cfg.net.proxy_port);
    else
        scroll_printf("(proxy cleared)\n");
    return 0;
}
static int sc_model(const char *a)
{
    if (*a) {
        str_set(g_cfg.model, (int)sizeof g_cfg.model, a);
        scroll_printf("(model: %s)\n", g_cfg.model);
    } else {
        scroll_printf("(current model: %s)\n", g_cfg.model);
        agent_list_models(&g_cfg, &g_scratch);
        scroll_printf("  Use /model NAME to switch.\n");
    }
    return 0;
}
static int sc_verb(const char *a)
{
    g_verbose = *a ? atoi(a) : !g_verbose;
    scroll_printf("(verbose: %d)\n", g_verbose);
    return 0;
}
static int sc_ip(const char *a)
{
    str_set(g_cfg.net.host_ip, (int)sizeof g_cfg.net.host_ip, a);
    scroll_printf("(host ip: %s)\n",
                  *g_cfg.net.host_ip ? g_cfg.net.host_ip : "DNS");
    return 0;
}
static int sc_yolo(const char *a)
{
    g_cfg.yolo = *a ? atoi(a) : !g_cfg.yolo;
    scroll_printf("(yolo: %s)\n", g_cfg.yolo ? "ON" : "off");
    return 0;
}
static int sc_edits(const char *a)
{
    g_cfg.accept_edits = *a ? atoi(a) : !g_cfg.accept_edits;
    scroll_printf("(accept-edits: %s)\n", g_cfg.accept_edits ? "ON" : "off");
    return 0;
}
static int sc_maxtok(const char *a)
{
    if (*a && atoi(a) > 0) g_cfg.max_tokens = atoi(a);
    scroll_printf("(max_tokens: %d)\n", g_cfg.max_tokens);
    return 0;
}
static int sc_tools(const char *a)
{
    g_cfg.chat_only = *a ? !atoi(a) : !g_cfg.chat_only;
    scroll_printf("(tools: %s)\n", g_cfg.chat_only ? "off" : "on");
    return 0;
}
static int sc_scroll(const char *a)
{
    (void)a;
    if (!scroll_view())
        scroll_printf("(scrollback unavailable on this console)\n");
    return 0;
}
static int sc_help(const char *a);

static int sc_resume(const char *a)
{
    char snip[56], line[16];
    long sz;
    int want = *a ? atoi(a) : 0;
    if (!*a) {
        int last = sess_last(), id, shown = 0;
        scroll_printf("\n  -- Sessions in %s --\n", sess_dir());
        for (id = last; id >= 1; id--) {
            if (!sess_peek(id, snip, (int)sizeof snip, &sz)) continue;
            scroll_printf("  %3d %c %5ldk  %s\n", id,
                          id == sess_id() ? '*' : ' ', (sz + 1023) / 1024,
                          snip[0] ? snip : "(empty)");
            shown++;
        }
        if (!shown) {
            scroll_printf("  (none)\n\n");
            return 0;
        }
        scroll_printf("\n  Resume which? [number, blank=cancel]: ");
        fflush(stdout);
        if (!fgets(line, (int)sizeof line, stdin)) return 0;
        scroll_capture(line, (int)strlen(line));
        want = atoi(line);
    }
    if (want <= 0 || want > SESS_MAX) {
        scroll_printf("(cancelled)\n");
        return 0;
    }
    if (!sess_peek(want, snip, (int)sizeof snip, &sz)) {
        scroll_printf("No such session: %d\n", want);
        return 0;
    }
    if (sess_switch(&g_conv, want) != 0) {
        scroll_printf("Cannot open session %d\n", want);
        return 0;
    }
    scroll_printf("Resumed session %d (%d turn%s, %ld bytes).\n", sess_id(),
                  g_conv.count, g_conv.count == 1 ? "" : "s", g_conv.fend);
    return 0;
}

static int sc_export(const char *a)
{
    char path[128], buf[512], cwd[256];
    FILE *out;
    int i;
    if (*a)
        str_set(path, (int)sizeof path, a);
    else if (sess_id() > 0)
        snprintf(path, sizeof path, "RELIC%03d.TXT", sess_id());
    else
        strcpy(path, "EXPORT.TXT");
    out = fopen(path, "w");
    if (!out) {
        scroll_printf("cannot write %s\n", path);
        return 0;
    }
    fprintf(out, "== Relic session export ==\n");
    if (g_verbose) {
        if (!plat_getcwd(cwd, (int)sizeof cwd)) strcpy(cwd, ".");
        fprintf(out, "Version : v%s (%s)\n", RELIC_VERSION, __DATE__);
        fprintf(out, "OS      : %s\n", plat_os_desc());
        fprintf(out, "cwd     : %s\n", cwd);
        fmt_cfg(buf, (int)sizeof buf);
        fputs(buf, out);
        fprintf(out, "Session : %d  (%s, %ld bytes)\n", sess_id(), g_conv.path,
                g_conv.fend);
        fprintf(out, "History : %d/%d turns, %d dropped\n", g_conv.count,
                CONV_CAP, g_conv.dropped);
    }
    for (i = 0; i < g_conv.count; i++) {
        const conv_turn *t = &g_conv.turns[i];
        long left = t->len;
        if (g_verbose)
            fprintf(out, "\n-- %d: %s [%s, %ld bytes @%ld] --\n", i + 1,
                    t->role == CONV_ROLE_USER ? "user" : "assistant",
                    t->is_json ? "json" : "text", t->len, t->off);
        else
            fprintf(out, "\n-- %s --\n",
                    t->role == CONV_ROLE_USER ? "user" : "assistant");
        if (!g_conv.fp || fseek(g_conv.fp, t->off, SEEK_SET) != 0) {
            fprintf(out, "(read error)\n");
            continue;
        }
        while (left > 0) {
            long want = left < (long)sizeof buf ? left : (long)sizeof buf;
            size_t got = fread(buf, 1, (size_t)want, g_conv.fp);
            if (got == 0) break;
            fwrite(buf, 1, got, out);
            left -= (long)got;
        }
        fputc('\n', out);
    }
    fclose(out);
    scroll_printf("(exported %d turn%s to %s%s)\n", g_conv.count,
                  g_conv.count == 1 ? "" : "s", path,
                  g_verbose ? " with debug header" : "");
    return 0;
}

static const slash_cmd CFG_KEYS[] = {
    {"model", "", "", sc_model}, {"proxy", "", "", sc_proxy},
    {"ip", "", "", sc_ip},       {"verbose", "", "", sc_verb},
    {"yolo", "", "", sc_yolo},   {"edits", "", "", sc_edits},
    {"tools", "", "", sc_tools}, {"max_tokens", "", "", sc_maxtok}};

static int sc_status(const char *a)
{
    char cwd[256], b[512];
    int kl;
    if (*a) {
        /* /status KEY [VALUE] -> configure one setting */
        int i, n;
        const char *v;
        for (n = 0; a[n] && a[n] != ' '; n++) {}
        v = a[n] ? a + n + 1 : "";
        while (*v == ' ')
            v++;
        for (i = 0; i < (int)(sizeof CFG_KEYS / sizeof CFG_KEYS[0]); i++)
            if ((int)strlen(CFG_KEYS[i].name) == n
                && memcmp(a, CFG_KEYS[i].name, (size_t)n) == 0)
                return CFG_KEYS[i].fn(v);
        scroll_printf(
            "unknown setting: %.*s  "
            "(model, proxy, ip, verbose, yolo, edits, tools, max_tokens)\n",
            n, a);
        return 0;
    }
    if (!plat_getcwd(cwd, (int)sizeof cwd)) strcpy(cwd, ".");
    scroll_printf(
        "\n  -- Status -----------------------------------------------\n");
    scroll_printf("  Version : Relic v%s (%s)\n", RELIC_VERSION, __DATE__);
    scroll_printf("  OS      : %s\n", plat_os_desc());
    scroll_printf("  cwd     : %s\n", cwd);
    kl = (int)strlen(g_cfg.key);
    if (kl == 0)
        scroll_printf("  API key : (none)\n");
    else
        scroll_printf("  API key : %.*s...%s  (%d chars%s)\n",
                      kl < 14 ? kl : 14, g_cfg.key,
                      kl > 18 ? g_cfg.key + kl - 4 : "", kl,
                      kl < 80 ? " -- looks short?" : "");
    scroll_printf("  Session : %d  (%s, %ld bytes)\n", sess_id(), g_conv.path,
                  g_conv.fend);
    scroll_printf("  History : %d/%d turns, %d dropped\n", g_conv.count,
                  CONV_CAP, g_conv.dropped);
    scroll_printf(
        "  -- Config (change with /status KEY VALUE) ---------------\n");
    fmt_cfg(b, (int)sizeof b);
    scroll_outz(b);
    scroll_printf(
        "  scroll  : %d/%d lines buffered (PgUp at prompt, or /scroll)\n",
        scroll_lines(), scroll_capacity());
    scroll_printf("\n");
    return 0;
}

static const slash_cmd CMDS[] = {
    {"help", "", "show this list", sc_help},
    {"status", "[K [V]]", "show status / change a setting", sc_status},
    {"quit", "", "exit", sc_quit},
    {"exit", "", "", sc_quit},
    {"clear", "", "clear conversation history", sc_reset},
    {"reset", "", "", sc_reset},
    {"resume", "[N]", "list past sessions / switch to one", sc_resume},
    {"export", "[PATH]", "write transcript to file (verbose adds debug)",
     sc_export},
    {"model", "[NAME]", "list available models, or set one", sc_model},
    {"test", "SUB", "run a self-test (try: /test network)", sc_test},
    {"nettest", "", "", sc_net},
    {"proxy", "[H:P]", "set HTTP CONNECT proxy (blank=clear)", sc_proxy},
    {"scroll", "", "view scrollback (or press PgUp at prompt)", sc_scroll},
    {"verbose", "[N]", "toggle/set trace level", sc_verb}};
#define NCMDS ((int)(sizeof CMDS / sizeof CMDS[0]))

static int sc_help(const char *a)
{
    (void)a;
    slash_help(CMDS, NCMDS);
    return 0;
}

/* Read one line at "> ". PgUp before any typing opens scrollback.
 * Returns length, or -1 on EOF/Ctrl-D. Falls back to fgets if the platform
 * has no raw key input. */
static int read_prompt(char *buf, int cap)
{
    int n = 0, rows, cols = 0, col;
    if (!plat_con_size(&rows, &cols)) cols = 0;
    plat_con_attr(PLAT_ATTR_PROMPT);
    fputs("> ", stdout);
    col = 2;
    fflush(stdout);
    if (!plat_con_raw(1)) {
        plat_con_attr(PLAT_ATTR_RESET);
        if (!fgets(buf, cap, stdin)) return -1;
        n = (int)strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = 0;
        return n;
    }
    for (;;) {
        int k = plat_getkey();
        if (k == PLAT_KEY_PGUP && n == 0) {
            scroll_view();
            plat_con_attr(PLAT_ATTR_PROMPT);
            fputs("> ", stdout);
            col = 2;
            fflush(stdout);
            continue;
        }
        if (k >= PLAT_KEY_UP) continue;
        if (k == '\r' || k == '\n') {
            plat_con_attr(PLAT_ATTR_RESET);
            fputc('\n', stdout);
            buf[n] = 0;
            scroll_capture("> ", 2);
            scroll_capture(buf, n);
            scroll_capture("\n", 1);
            break;
        }
        if (k == 8 || k == 127) {
            if (n > 0) {
                n--;
                if (cols > 0 && col == 0) {
                    plat_con_backwrap(cols);
                    col = cols - 1;
                } else {
                    fputs("\b \b", stdout);
                    col--;
                }
                fflush(stdout);
            }
            continue;
        }
        if (k == 0 || (k == 4 && n == 0)) {
            plat_con_attr(PLAT_ATTR_RESET);
            n = -1;
            break;
        }
        if (k >= 32 && k < 256 && n < cap - 1) {
            buf[n++] = (char)k;
            fputc(k, stdout);
            col++;
            if (cols > 0 && col >= cols) {
                /* Force the deferred wrap (xterm holds the cursor in the
                 * last column until the next byte) so our column tracker
                 * matches the real cursor before any backspace arrives. */
                fputs(" \b", stdout);
                col = 0;
            }
            fflush(stdout);
        }
    }
    plat_con_raw(0);
    return n;
}

static void usage(void)
{
    scroll_outz(
        "usage: relic [options] [-p PROMPT]\n"
        "  -p PROMPT       one-shot: send PROMPT, run agent loop, exit\n"
        "  -r [N]          resume session N (or latest if N omitted)\n"
        "  -c              chat-only mode (no tools)\n"
        "  --dangerously-skip-permissions\n"
        "                  auto-approve all tool calls (DANGEROUS)\n"
        "  --permission-mode MODE\n"
        "                  default | acceptEdits | bypassPermissions\n"
        "  -m MODEL        override model (default " ANTH_DEFAULT_MODEL ")\n"
        "  --proxy H:P     HTTP CONNECT proxy (also: proxy=H:P in RELIC.CFG)\n"
        "  --ip A.B.C.D    bypass DNS for api.anthropic.com\n"
        "  -v              verbose (repeat: -v -v dumps headers + body)\n"
        "  -q              quiet (verbose off; default)\n"
        "  --nettest       network self-test (try first if connect fails)\n"
        "  -h              this help\n"
        "  (no -p)         interactive REPL; /reset, /quit\n");
}

int main(int argc, char **argv)
{
    static char line[4096];
    const char *prompt = NULL;
    char v[80];
    int do_nettest = 0;
    int resume = 0; /* 0=fresh, -1=latest, >0=that id */
    int i;

    plat_init();
    str_set(g_cfg.model, (int)sizeof g_cfg.model, ANTH_DEFAULT_MODEL);
    g_cfg.max_tokens = 8192;

    /* Unbuffer stdout so plat_con_attr() brackets are airtight: on Win9x
     * SetConsoleTextAttribute is out-of-band w.r.t. stdio, and Watcom's
     * console stdout is fully buffered, so colored bytes otherwise flush
     * after the attribute has already been reset. Harmless on POSIX. */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* RELIC.CFG may set model=/proxy=/host_ip= alongside api_key=.
     * permission_mode/proxy/host_ip are PLAT_CFG_TRUSTED: honoured only from
     * the home-directory file, never from a RELIC.CFG in the cwd, so a
     * checked-out repo can't silently disable prompts or reroute traffic. */
    if (plat_get_cfg("model", v, (int)sizeof v, 0))
        str_set(g_cfg.model, (int)sizeof g_cfg.model, v);
    if (plat_get_cfg("proxy", v, (int)sizeof v, PLAT_CFG_TRUSTED))
        net_set_proxy(&g_cfg.net, v);
    if (plat_get_cfg("host_ip", v, (int)sizeof v, PLAT_CFG_TRUSTED))
        str_set(g_cfg.net.host_ip, (int)sizeof g_cfg.net.host_ip, v);
    if (plat_get_cfg("max_tokens", v, (int)sizeof v, 0) && atoi(v) > 0)
        g_cfg.max_tokens = atoi(v);
    if (plat_get_cfg("permission_mode", v, (int)sizeof v, PLAT_CFG_TRUSTED)
        && set_permission_mode(v) != 0)
        fprintf(stderr, "RELIC.CFG: unknown permission_mode '%s'\n", v);
    if (plat_get_cfg("scroll", v, (int)sizeof v, 0) && atoi(v) > 0)
        scroll_init(atoi(v));
    if (plat_get_cfg("verbose", v, (int)sizeof v, 0) && atoi(v) >= 0)
        g_verbose = atoi(v);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "-c") == 0)
            g_cfg.chat_only = 1;
        else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--resume") == 0)
            resume =
                (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
                    ? atoi(argv[++i])
                    : -1;
        else if (strcmp(argv[i], "--dangerously-skip-permissions") == 0)
            g_cfg.yolo = 1;
        else if (strcmp(argv[i], "--permission-mode") == 0 && i + 1 < argc) {
            if (set_permission_mode(argv[++i]) != 0) {
                fprintf(stderr,
                        "unknown permission mode: %s "
                        "(default|acceptEdits|bypassPermissions)\n",
                        argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-v") == 0)
            g_verbose++;
        else if (strcmp(argv[i], "-q") == 0)
            g_verbose = 0;
        else if (strcmp(argv[i], "--nettest") == 0)
            do_nettest = 1;
        else if (strcmp(argv[i], "--proxy") == 0 && i + 1 < argc)
            net_set_proxy(&g_cfg.net, argv[++i]);
        else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc)
            str_set(g_cfg.net.host_ip, (int)sizeof g_cfg.net.host_ip,
                    argv[++i]);
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            prompt = argv[++i];
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            str_set(g_cfg.model, (int)sizeof g_cfg.model, argv[++i]);
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage();
            return 1;
        }
    }

    if (do_nettest) return net_selftest(&g_cfg.net);

    if (plat_get_api_key(g_cfg.key, (int)sizeof g_cfg.key) == 0) {
        fprintf(stderr,
                "No API key. Set ANTHROPIC_API_KEY or create RELIC.CFG next to "
                "the program with:  api_key=sk-ant-...\n");
        return 2;
    }

    {
        int rc = sess_open(&g_conv, resume);
        if (rc != 0) {
            if (resume)
                fprintf(stderr, "No session to resume.\n");
            else if (rc == -2)
                fprintf(stderr,
                        "Session store full (%d/%d). Delete old "
                        "RELIC*.DAT files in %s and lower the id in "
                        "RELIC.IDX to free slots.\n",
                        SESS_MAX, SESS_MAX, sess_dir());
            else
                fprintf(stderr, "Cannot create session store (cwd and "
                                "$TEMP/$TMP/$TMPDIR/$HOME all unwritable)\n");
            return 2;
        }
    }

    if (prompt) {
        g_cfg.noninteractive = 1;
        if (sess_commit(&g_conv) != 0) {
            fprintf(stderr, "Cannot create %s\n", g_conv.path);
            return 2;
        }
        conv_push_text(&g_conv, CONV_ROLE_USER, prompt);
        plat_on_sigint(on_sigint);
        return agent_run(&g_conv, &g_cfg, &g_scratch) == 0 ? 0 : 1;
    }

    print_banner(g_cfg.chat_only);
    if (g_conv.count)
        scroll_printf(
            "  Session %d: resumed %d turn%s from %s. /reset to clear.\n\n",
            sess_id(), g_conv.count, g_conv.count == 1 ? "" : "s", g_conv.path);
    else
        scroll_printf("  Session %d (%s). /resume to list past sessions.\n\n",
                      sess_id(), g_conv.path);
    for (;;) {
        int rc, n = read_prompt(line, (int)sizeof line);
        if (n < 0) break;
        if (n == 0) continue;
        rc = slash_dispatch(CMDS, NCMDS, line);
        if (rc == 1) break;
        if (rc == 0) continue;
        if (sess_commit(&g_conv) != 0) {
            scroll_printf("(cannot create %s)\n", g_conv.path);
            continue;
        }
        conv_push_text(&g_conv, CONV_ROLE_USER, line);
        plat_on_sigint(on_sigint);
        /* Raw mode so a lone ESC reaches plat_esc_poll() (cooked stdin
         * line-buffers it). No-op where plat_con_raw is unsupported. */
        plat_con_raw(1);
        agent_run(&g_conv, &g_cfg, &g_scratch);
        plat_con_raw(0);
        plat_on_sigint(NULL);
    }
    return 0;
}
