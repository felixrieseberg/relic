#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>
#include "agent_ui.h"
#include "evdev.h"
#include "fb.h"
#include "logview.h"
#include "textfb.h"
extern "C" {
#include "core/scroll.h"
#include "core/slash.h"
#include "core/ui.h"
}

/* ---------- layout ------------------------------------------------------ */
/* The canvas is the FULL framebuffer. The console (log strip + input bar)
 * is an overlay on the bottom rows: when shown we snapshot what's under it
 * and paint on top; when hidden (`) we restore the snapshot and stop
 * touching the fb. Model programs draw edge-to-edge; the console always
 * wins z-order while up because we repaint it every idle tick. */
struct layout {
    struct fb *F;
    int log_y, log_h, in_y, in_h, con_y, con_h, pad;
    int con;                /* console shown? */
    uint8_t *save;          /* con_h * stride bytes under the console */
};

static void lay_init(layout &L, struct fb *F) {
    L.F     = F;
    L.in_h  = TEXT_CH + 12;
    L.log_h = TEXT_CH * 10 + 8;
    L.log_y = F->h - L.in_h - L.log_h;
    L.con_y = L.log_y - 1;                   /* include the 1px top divider */
    L.con_h = F->h - L.con_y;
    L.in_y  = F->h - L.in_h;
    L.pad   = 12;
    L.con   = 1;
    L.save  = (uint8_t *)malloc((long)L.con_h * F->stride);
    log_set_cols((F->w - 2 * L.pad) / TEXT_CW - 2);
}

static void lay_chrome(layout &L) {
    text_fill(L.F, 0, L.log_y - 1, L.F->w, 1, 0xcccccc);
    text_fill(L.F, 0, L.in_y  - 1, L.F->w, 1, 0xcccccc);
}

static void lay_canvas_clear(layout &L) {
    text_fill(L.F, 0, 0, L.F->w, L.F->h, 0x35332e);
    int x = (L.F->w - 58 * TEXT_CW) / 2, y = L.con_y / 2 - TEXT_CH;
    text_draw(L.F, x, y, "  This is /dev/fb0. Anything you ask for that draws    ", 0x707070, 0x35332e);
    text_draw(L.F, x, y + TEXT_CH + 4,
              "  lands here. Press ` to hide or show this console.    ", 0x707070, 0x35332e);
    if (L.save) fb_save(L.F, L.con_y, L.con_h, L.save);   /* fresh underlay */
    fprintf(stderr, "blit ok\n");
}

static void lay_log(layout &L) {
    if (!L.con) return;
    text_fill(L.F, 0, L.log_y, L.F->w, L.log_h, 0xf6f5f1);
    log_paint(L.F, L.pad, L.log_y + 4, L.F->w - 2 * L.pad, L.log_h - 8);
    lay_chrome(L);
}

static void lay_input(layout &L, const std::string &t, const char *status) {
    if (!L.con) return;
    text_fill(L.F, 0, L.in_y, L.F->w, L.in_h, 0xffffff);
    int x = L.pad, y = L.in_y + (L.in_h - TEXT_CH) / 2;
    text_draw(L.F, x, y, "> ", 0x999999, 0xffffff); x += 2 * TEXT_CW;
    if (t.empty()) text_draw(L.F, x, y, status, 0x999999, 0xffffff);
    else {
        int w = text_draw(L.F, x, y, t.c_str(), 0x2b2b2b, 0xffffff);
        text_fill(L.F, x + w, y, TEXT_CW, TEXT_CH, 0x2b2b2b);
    }
}

static void con_hide(layout &L) {
    if (!L.con) return;
    if (L.save) fb_restore(L.F, L.con_y, L.con_h, L.save);
    L.con = 0;
    fprintf(stderr, "console: hidden\n");
}

static void con_show(layout &L) {
    if (L.con) return;
    if (L.save) fb_save(L.F, L.con_y, L.con_h, L.save);
    L.con = 1;
    fprintf(stderr, "console: shown\n");
}

/* ---------- slash commands ---------------------------------------------- */
/* Dispatcher + /help live in core/slash.c (shared with relic). We supply a
 * table of handlers that make sense as PID 1 on /dev/fb0, and route handler
 * output (scroll_printf) into the log strip via scroll_set_sink().
 * Dropped vs relic: /resume /proxy /scroll /verbose /test. */
static layout *g_L;
static int     g_have_agent;

static void sink_spin(const char *label, unsigned secs) {
    char buf[48];
    if (secs) snprintf(buf, sizeof buf, "%s... (%us)", label, secs);
    else      snprintf(buf, sizeof buf, "%s...", label);
    lay_input(*g_L, "", buf);
}

static void sink_log(const char *s, int len) {  /* split on '\n' -> log rows */
    static char acc[256]; static int n;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\n' || n == (int)sizeof acc - 1) {
            acc[n] = 0; if (n) log_push(' ', acc); n = 0;
            if (c == '\n') continue;
        }
        acc[n++] = c;
    }
}

static int sc_help(const char *);
static int sc_reset (const char *a) { (void)a; if (g_have_agent) agent_reset();
                                      scroll_printf("(history cleared)\n"); return 0; }
static int sc_clear (const char *a) { (void)a; lay_canvas_clear(*g_L);
                                      agent_windows_clear();
                                      scroll_printf("(canvas cleared)\n"); return 0; }
static int sc_model (const char *a) { scroll_printf("(model: %s)\n",
                                      agent_model(*a ? a : nullptr)); return 0; }
static int sc_agents(const char *a) { (void)a; char b[2048];
                                      agent_list(b, (int)sizeof b);
                                      scroll_printf("%s", b); return 0; }
static int sc_wins  (const char *a) { (void)a; char b[2048];
                                      agent_windows(b, (int)sizeof b);
                                      scroll_printf("%s", b); return 0; }
static int sc_status(const char *a) { (void)a;
    scroll_printf("display %dx%d, full-screen canvas; console overlay rows %d..%d\n",
                  g_L->F->w, g_L->F->h, g_L->con_y, g_L->F->h - 1);
    scroll_printf("model %s; api key %s\n", agent_model(nullptr),
                  g_have_agent ? "set" : "missing");
    return 0; }
static int sc_quit  (const char *a) { (void)a; scroll_printf("powering off...\n");
                                      lay_log(*g_L); sync(); reboot(RB_POWER_OFF);
                                      return 1; }

static const slash_cmd CMDS[] = {
    {"help",   "",       "show this list",               sc_help},
    {"clear",  "",       "clear conversation history",   sc_reset},
    {"reset",  "",       "",                             sc_reset},
    {"reset-canvas", "", "wipe the canvas + window list", sc_clear},
    {"model",  "[NAME]", "show or set model",            sc_model},
    {"agents", "",       "list background sub-agents",   sc_agents},
    {"windows","",       "list canvas windows",          sc_wins},
    {"status", "",       "show display + session info",  sc_status},
    {"quit",   "",       "power off",                    sc_quit},
    {"exit",   "",       "",                             sc_quit}};
#define NCMDS ((int)(sizeof CMDS / sizeof CMDS[0]))

static int sc_help(const char *a) { (void)a; slash_help(CMDS, NCMDS); return 0; }

/* ---------- modes -------------------------------------------------------- */
static void wire_cb(agent_cb &cb, layout &L) {
    cb.status = [&L](const char *m) {
        log_push(' ', m); lay_log(L);
        fprintf(stderr, "agent: %s\n", m);
    };
    cb.say = [&L](const char *t) {
        log_push('<', t); lay_log(L);
        fprintf(stderr, "agent says: [%zu chars]\n", strlen(t));
    };
    cb.tool = [&L](const char *name, const char *sum) {
        log_pushf(':', "%s  %s", name, sum); lay_log(L);
        fprintf(stderr, "agent: tool: %s\n", name);
    };
}

/* Headless sub-agent: own conv file, stdout = final text + tool log. Used by
 * the `Agent` tool (fork+exec relicos --agent). FB env vars are inherited. */
int run_agent(const char *prompt) {
    char conv[64]; snprintf(conv, sizeof conv, "/tmp/agent.%d.conv", getpid());
    if (agent_init(conv, 0, 0, 0) != 0) { fprintf(stderr, "no API key\n"); return 2; }
    spin_set_sink([](const char *, unsigned) {});   /* quiet in log files */
    agent_cb cb;
    cb.status = [](const char *m) { fprintf(stderr, "[%s]\n", m); };
    cb.say    = [](const char *t) { fputs(t, stdout); fputc('\n', stdout); fflush(stdout); };
    cb.tool   = [](const char *n, const char *s) { fprintf(stderr, ": %s  %s\n", n, s); };
    int rc = agent_turn(prompt, cb);
    unlink(conv);
    return rc == 0 ? 0 : 1;
}

int run_once(const char *fbdev, const char *prompt) {
    struct fb F;
    if (fb_open(&F, fbdev) != 0) { fprintf(stderr, "fb_open failed\n"); return 1; }
    layout L; lay_init(L, &F);
    if (agent_init("/tmp/relicos.conv", F.w, F.h, F.h) != 0) {
        fprintf(stderr, "no API key\n"); return 2;
    }
    lay_canvas_clear(L); lay_log(L); lay_input(L, "", "");
    agent_cb cb; wire_cb(cb, L);
    log_push('>', prompt); lay_log(L);
    fprintf(stderr, "submit: %s\n", prompt);
    int rc = agent_turn(prompt, cb);
    fprintf(stderr, rc == 0 ? "RELICOS_ONCE_PASS\n" : "RELICOS_ONCE_FAIL\n");
    for (;;) pause();
}

int run_ui(const char *fbdev) {
    struct fb F;
    if (fb_open(&F, fbdev) != 0) { fprintf(stderr, "fb_open(%s) failed\n", fbdev); return 1; }
    int kfd = ev_open_keyboard();
    if (kfd < 0) fprintf(stderr, "no keyboard; display-only\n");

    layout L; lay_init(L, &F);
    int have_agent = (agent_init("/tmp/relicos.conv", F.w, F.h, F.h) == 0);
    if (!have_agent) fprintf(stderr, "agent: no API key (set ANTHROPIC_API_KEY)\n");
    g_L = &L; g_have_agent = have_agent;
    scroll_set_sink(sink_log); spin_set_sink(sink_spin);

    lay_canvas_clear(L);
    log_pushf(' ', "/dev/fb0 %dx%d 32bpp; full-screen canvas; ` toggles this console.",
              F.w, F.h);
    log_push(' ', have_agent ? "ready." : "no API key set; offline.");
    lay_log(L);

    std::string typed;
    const char *hint = "type, then enter   (/help, esc clears history, ` hides console)";
    lay_input(L, typed, hint);

    agent_cb cb; wire_cb(cb, L);
    if (kfd < 0) for (;;) pause();

    auto apply = [&](int k) -> int {
        if (k == EVK_ESC)      { typed.clear(); if (have_agent) agent_reset();
                                 log_push(' ', "history cleared"); }
        else if (k == EVK_BKSP){ if (!typed.empty()) typed.pop_back(); }
        else if (k == EVK_ENTER) return 1;
        else if (k > 0)          typed += (char)k;
        return 0;
    };

    for (;;) {
        /* Wake periodically even with no input: virtio-gpu only repaints the
         * host window on guest-driven flushes, so an idle process can leave
         * the QEMU window blank after a resize/expose until the next write. */
        int k = ev_wait_key(kfd, 250);
        if (k == 0) {
            while (waitpid(-1, 0, WNOHANG) > 0) ;   /* PID 1: reap orphans */
            lay_log(L); lay_input(L, typed, hint); continue;
        }
        if (k == '`') {                             /* Quake-style toggle */
            if (L.con) { con_hide(L); ev_grab(kfd, 0); }
            else { ev_grab(kfd, 1); con_show(L); lay_log(L); lay_input(L, typed, hint); }
            continue;
        }
        if (!L.con) continue;   /* hidden: keys go to model programs, not us */
        int submit = apply(k);
        while (!submit && (k = ev_poll_key(kfd)) != 0) {
            if (k == '`') { con_hide(L); ev_grab(kfd, 0); break; }
            submit = apply(k);
        }
        if (!L.con) continue;

        if (submit && !typed.empty()) {
            fprintf(stderr, "submit: %s\n", typed.c_str());
            log_push('>', typed.c_str()); lay_log(L);
            if (slash_dispatch(CMDS, NCMDS, typed.c_str()) >= 0) {
                lay_log(L);
            } else {
                lay_input(L, "", "...");
                if (have_agent) agent_turn(typed.c_str(), cb);
                else { log_push(' ', "no API key; cannot send"); lay_log(L); }
            }
            typed.clear();
        }
        lay_input(L, typed, hint);
    }
}
