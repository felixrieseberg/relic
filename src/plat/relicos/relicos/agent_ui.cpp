#include "agent_ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include "core/anth.h"
#include "core/conv.h"
#include "core/json_write.h"
#include "core/jsonp.h"
#include "core/tls_client.h"
#include "core/tools.h"
#include "core/ui.h"
#include "core/util.h"
#include "core/xport.h"
#include "plat/plat.h"
}

/* ---- buffers (static; matches relic's no-malloc style) ------------------ */
enum { BUF = 512 * 1024, REQ = BUF + 1024, ARG_CAP = 256 * 1024, OUT_CAP = 64 * 1024 };
static char g_body[BUF], g_req[REQ], g_arg[ARG_CAP], g_out[OUT_CAP];
static char g_key[256], g_model[64] = ANTH_DEFAULT_MODEL;
static conv_t g_cv;

static const char *SYS_PROMPT =
    "You are root on RelicOS: a minimal network-connected machine where "
    "the agent loop is PID 1. Installed: busybox, tcc (C compiler), musl "
    "+ Linux headers. /data is a persistent ext4 disk; everything else is "
    "initramfs.\n"
    "\n"
    "The framebuffer /dev/fb0 (32bpp XRGB, stride = width*4) is yours "
    "edge to edge: rows 0..CANVAS_H-1, where CANVAS_H == FB_H. Nothing "
    "draws there unless a program you compile and run writes pixels "
    "(open /dev/fb0, mmap, write 0x00RRGGBB words).\n"
    "The chat log + input line is a drop-down overlay on the bottom rows; "
    "the human toggles it with `. While it's up the shell repaints over "
    "whatever is underneath and owns the keyboard; when hidden your pixels "
    "show through and any program you Spawn that opens /dev/input/event* "
    "receives keystrokes directly (` is reserved -- it reopens the "
    "console). Your plain-text replies and a one-line summary of every "
    "tool call appear in the console automatically -- you don't manage it.\n"
    "CANVAS_H, FB_W and FB_H are exported in your shell environment.\n"
    "\n"
    "Platform ABI: #include <relicos.h>. ros_open() mmaps the fb and "
    "reads the env vars; ros_window(&c,w,h,\"title\") picks a non-"
    "overlapping rect (left-to-right shelf) and records it in /tmp/windows "
    "so later programs can avoid it; ros_fill()/ros_put() draw clipped to "
    "the canvas. Read the header before use. There is no window manager -- "
    "you own layout. You can place rects yourself and just ros_claim() "
    "them, or ignore the registry entirely for full-canvas takeovers.\n"
    "\n"
    "Tools (root, unrestricted -- absolute paths are fine):\n"
    "  Bash        -- /bin/sh -c CMD; combined stdout+stderr returned "
    "(truncated past ~60 KB). BLOCKS until the command exits; do not use "
    "for long-running programs.\n"
    "  Read / Write / Edit / LS / Grep / Glob -- file tools. Write creates "
    "parent directories.\n"
    "  Spawn       -- start CMD in the background (own session, stdio -> "
    "/tmp/spawn.<pid>.log). Returns the pid. Use this for daemons or "
    "interactive things like a mouse cursor. Manage via Bash: "
    "`kill <pid>`, `tail /tmp/spawn.<pid>.log`.\n"
    "  Windows     -- list /tmp/windows (rect, pid, live?, title). Use "
    "this to see what's on the canvas before placing something new.\n"
    "  Agent       -- start a SUB-AGENT in the background: a fresh copy of "
    "yourself with the same tools, given one prompt, running to completion. "
    "Returns a small integer agent id immediately. Use this to fan work out "
    "(research, builds, parallel attempts). The sub-agent cannot ask you "
    "questions; give it everything it needs in the prompt.\n"
    "  Agents      -- list known sub-agents with status.\n"
    "  AgentResult -- read a sub-agent's output (final text + tool log). "
    "Says 'running' with a tail if not finished yet.\n"
    "\n"
    "Behaviour: this is a conversation. Reply in text when that's enough. "
    "When the human asks for something visual or runnable, BUILD it: "
    "write C, `tcc -o /data/bin/x x.c`, run it. The program should draw "
    "into the canvas region and exit (or keep running in the background "
    "if it's interactive). Chain as many tool calls per turn as needed. "
    "Persist anything worth keeping under /data.";

/* RelicOS-only tool schemas; appended to core tools_json() at init. */
static const char *OS_TOOLS_JSON =
    "{\"name\":\"Spawn\",\"description\":\"Run a shell command in the "
    "background (setsid, stdio -> /tmp/spawn.<pid>.log). Returns the pid "
    "and log path; does not wait.\",\"input_schema\":{\"type\":\"object\","
    "\"properties\":{\"cmd\":{\"type\":\"string\"}},\"required\":[\"cmd\"]}},"
    "{\"name\":\"Windows\",\"description\":\"List the canvas window "
    "registry (/tmp/windows): rect, pid, whether the pid is still alive, "
    "title.\",\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"Agent\",\"description\":\"Start a background sub-agent with "
    "the same tools. Returns its agent id immediately; use AgentResult to "
    "collect output.\",\"input_schema\":{\"type\":\"object\",\"properties\":"
    "{\"prompt\":{\"type\":\"string\"}},\"required\":[\"prompt\"]}},"
    "{\"name\":\"Agents\",\"description\":\"List sub-agents and status.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"AgentResult\",\"description\":\"Read a sub-agent's output. "
    "If still running, returns a tail and says so.\",\"input_schema\":"
    "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"}},"
    "\"required\":[\"id\"]}}";

static char g_tools_json[4096];

/* ---- transport ---------------------------------------------------------- */
/* core/xport.c does the keepalive + reconnect-once. Keep a 5x sleep-retry
 * wrapper here for the boot-time case where the network/DNS is still coming
 * up and the very first tls_open() fails. */
static int post_once(int *status, int *blen, const agent_cb &cb) {
    req_ctx rc;
    rc.model = g_model; rc.sys = SYS_PROMPT; rc.tools = g_tools_json;
    rc.max_tokens = 8192; rc.cv = &g_cv;
    for (int t = 0; t < 5; t++) {
        if (anth_post(0, g_key, &rc, g_body, (int)sizeof g_body, status, blen) == 0)
            return 0;
        cb.status(tls_last_error_str());
        sleep(1);
    }
    return -1;
}

/* ---- tool handling ------------------------------------------------------ */
static int j_field(const char *js, int len, const char *key, char *out, int cap) {
    jsmn_parser p; jsmntok_t toks[32];
    jsmn_init(&p);
    int n = jsmn_parse(&p, js, len, toks, 32);
    if (n < 1 || toks[0].type != JSMN_OBJECT) return -1;
    int v = jsonp_child(toks, 0, js, key);
    if (v < 0) return -1;
    if (toks[v].type == JSMN_PRIMITIVE) {           /* number/bool: raw copy */
        int w = toks[v].end - toks[v].start;
        if (w >= cap) w = cap - 1;
        memcpy(out, js + toks[v].start, w); out[w] = 0; return w;
    }
    return jsonp_unescape(js, &toks[v], out, cap);
}

/* RelicOS is PID 1; collect any exited Spawn'ed children / orphans. */
static void reap(void) { while (waitpid(-1, 0, WNOHANG) > 0) ; }

/* One-line summary of a core tool call for the log strip. */
static void core_summary(int idx, const char *in, int ilen, char *sum, int cap) {
    char pv[256];
    int n = tool_preview(idx, in, ilen, pv, (int)sizeof pv);
    if (n <= 0) { snprintf(sum, cap, "%.*s", ilen < cap - 1 ? ilen : cap - 1, in); return; }
    for (int i = 0; i < n; i++) if (pv[i] == '\n') pv[i] = ' ';
    snprintf(sum, cap, "%s", pv);
}

static int do_spawn(const char *cmd, char *out, int cap) {
    pid_t pid = fork();
    if (pid < 0) return snprintf(out, cap, "error: fork: %m");
    if (pid == 0) {
        setsid();
        int n = open("/dev/null", O_RDONLY);
        if (n >= 0) { dup2(n, 0); if (n > 2) close(n); }
        char log[64]; snprintf(log, sizeof log, "/tmp/spawn.%d.log", getpid());
        int f = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (f >= 0) { dup2(f, 1); dup2(f, 2); if (f > 2) close(f); }
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    return snprintf(out, cap, "spawned pid %d, log /tmp/spawn.%d.log", pid, pid);
}

/* ---- canvas window registry: /tmp/windows (see rootfs/.../relicos.h) --- */
#define WIN_REGISTRY "/tmp/windows"

static int do_windows(char *out, int cap) {
    FILE *f = fopen(WIN_REGISTRY, "r");
    if (!f) return snprintf(out, cap, "(no windows)\n");
    int o = snprintf(out, cap, "   x    y    w    h    pid live  title\n");
    int x, y, w, h, pid; char t[64];
    while (fscanf(f, "%d %d %d %d %d %63[^\n] ", &x, &y, &w, &h, &pid, t) == 6
           && o < cap - 80)
        o += snprintf(out + o, cap - o, "%4d %4d %4d %4d %6d  %c   %s\n",
                      x, y, w, h, pid, kill(pid, 0) == 0 ? 'y' : '-', t);
    fclose(f);
    return o;
}

void agent_windows_clear(void) { unlink(WIN_REGISTRY); }

/* ---- sub-agent registry: /tmp/agents/<N>/{pid,prompt,out} --------------- */
#define AGENTS_DIR "/tmp/agents"

static int subagent_next_id(void) {
    int hi = 0; DIR *d = opendir(AGENTS_DIR);
    if (d) { struct dirent *e;
        while ((e = readdir(d))) { int n = atoi(e->d_name); if (n > hi) hi = n; }
        closedir(d);
    }
    return hi + 1;
}

static int subagent_pid(int id) {
    char p[64]; snprintf(p, sizeof p, AGENTS_DIR "/%d/pid", id);
    FILE *f = fopen(p, "r"); if (!f) return -1;
    int pid = 0; fscanf(f, "%d", &pid); fclose(f); return pid;
}

static int do_agent(const char *prompt, char *out, int cap) {
    mkdir(AGENTS_DIR, 0755);
    int id = subagent_next_id();
    char dir[64], path[80];
    snprintf(dir, sizeof dir, AGENTS_DIR "/%d", id); mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/prompt", dir);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%.200s", prompt); fclose(f); }
    pid_t pid = fork();
    if (pid < 0) return snprintf(out, cap, "error: fork: %m");
    if (pid == 0) {
        setsid();
        snprintf(path, sizeof path, "%s/out", dir);
        int n = open("/dev/null", O_RDONLY); if (n >= 0) { dup2(n, 0); close(n); }
        int o = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (o >= 0) { dup2(o, 1); dup2(o, 2); close(o); }
        execl("/bin/relicos", "relicos", "--agent", prompt, (char *)0);
        _exit(127);
    }
    snprintf(path, sizeof path, "%s/pid", dir);
    f = fopen(path, "w"); if (f) { fprintf(f, "%d\n", pid); fclose(f); }
    return snprintf(out, cap, "agent %d started (pid %d). "
                    "Use agent_result %d to collect.", id, pid, id);
}

static int do_agents(char *out, int cap) {
    DIR *d = opendir(AGENTS_DIR);
    if (!d) return snprintf(out, cap, "(no agents)");
    int o = snprintf(out, cap, " id  status   prompt\n");
    struct dirent *e;
    while ((e = readdir(d)) && o < cap - 80) {
        int id = atoi(e->d_name); if (id <= 0) continue;
        int pid = subagent_pid(id);
        const char *st = (pid > 0 && kill(pid, 0) == 0) ? "running" : "done";
        char pp[80] = "", pr[201] = "";
        snprintf(pp, sizeof pp, AGENTS_DIR "/%d/prompt", id);
        FILE *f = fopen(pp, "r");
        if (f) { fgets(pr, sizeof pr, f); fclose(f); }
        for (char *c = pr; *c; c++) if (*c == '\n') *c = ' ';
        o += snprintf(out + o, cap - o, " %-3d %-8s %.60s\n", id, st, pr);
    }
    closedir(d);
    return o;
}

static int do_agent_result(int id, char *out, int cap) {
    int pid = subagent_pid(id);
    if (pid < 0) return snprintf(out, cap, "error: no such agent %d", id);
    int running = (pid > 0 && kill(pid, 0) == 0);
    char p[80]; snprintf(p, sizeof p, AGENTS_DIR "/%d/out", id);
    int o = snprintf(out, cap, "agent %d: %s\n---\n", id,
                     running ? "still running (output so far)" : "done");
    if (o >= cap) o = cap - 1;
    FILE *f = fopen(p, "r");
    if (f) { o += (int)fread(out + o, 1, cap - 1 - o, f); fclose(f); }
    out[o] = 0;
    return o;
}

static int build_tool_result(char *out, int cap, const char *id, const char *text) {
    int o = 0;
    o += snprintf(out + o, cap - o, "{\"type\":\"tool_result\",\"tool_use_id\":\"");
    json_escape_str(out, cap, &o, id);
    o += snprintf(out + o, cap - o, "\",\"content\":\"");
    json_escape_str(out, cap, &o, text);
    o += snprintf(out + o, cap - o, "\"}");
    return (o < cap) ? o : -1;
}

/* ---- public ------------------------------------------------------------- */
int agent_init(const char *conv_path, int fb_w, int fb_h, int canvas_h) {
    if (fb_w > 0) {   /* 0 = inherit (sub-agent --agent mode) */
        char v[16];
        snprintf(v, sizeof v, "%d", fb_w);     setenv("FB_W",     v, 1);
        snprintf(v, sizeof v, "%d", fb_h);     setenv("FB_H",     v, 1);
        snprintf(v, sizeof v, "%d", canvas_h); setenv("CANVAS_H", v, 1);
    }
    const char *core = tools_json();   /* core "[...]"; splice in OS extras */
    int n = core ? (int)strlen(core) : 0;
    if (n >= 2)
        snprintf(g_tools_json, sizeof g_tools_json, "%.*s,%s]", n - 1, core,
                 OS_TOOLS_JSON);
    else {
        fprintf(stderr, "agent_init: core tools_json unavailable\n");
        snprintf(g_tools_json, sizeof g_tools_json, "[%s]", OS_TOOLS_JSON);
    }
    if (plat_get_api_key(g_key, (int)sizeof g_key) == 0) return -1;
    if (const char *m = getenv("RELICOS_MODEL"))
        str_set(g_model, (int)sizeof g_model, m);
    return conv_init(&g_cv, conv_path ? conv_path : "/tmp/relicos.conv", 0);
}

void agent_reset(void) { conv_reset(&g_cv); }

int agent_list(char *out, int cap) { return do_agents(out, cap); }
int agent_windows(char *out, int cap) { return do_windows(out, cap); }

const char *agent_model(const char *set_to) {
    if (set_to && *set_to) {
        str_set(g_model, (int)sizeof g_model, set_to);
        g_model[sizeof g_model - 1] = 0;
    }
    return g_model;
}

int agent_turn(const char *user_text, const agent_cb &cb) {
    spin_clear();   /* elapsed-seconds counter spans the whole turn */
    if (conv_push_text(&g_cv, CONV_ROLE_USER, user_text) != 0) { cb.status("conv push failed"); return -1; }

    for (int iter = 0; iter < 24; iter++) {
        reap();
        int status = 0, blen = 0;
        if (post_once(&status, &blen, cb) != 0) return -1;

        anth_result R;
        if (anth_parse(g_body, blen, &R) != 0) { cb.status("bad response json"); return -1; }
        if (R.is_error) { cb.status(R.err_msg); return -1; }

        /* round-trip assistant content for the next request */
        conv_push(&g_cv, CONV_ROLE_ASST, 1, R.content_json, R.content_json_len);

        int ntool = 0;
        for (int i = 0; i < R.nblocks; i++) {
            const anth_block &b = R.blocks[i];
            if (b.kind == ANTH_BLK_TEXT) {
                if (cb.say && b.body_len > 0) {
                    int n = jsonp_unescape_span(b.body, b.body_len, g_out, (int)sizeof g_out);
                    g_out[n] = 0;
                    cb.say(g_out);
                }
                continue;
            }
            if (b.kind != ANTH_BLK_TOOL) continue;
            g_out[0] = 0;
            char sum[160] = "";
            int core = tool_index(b.name);
            if (core >= 0) {
                core_summary(core, b.body, b.body_len, sum, (int)sizeof sum);
                if (cb.tool) cb.tool(b.name, sum);
                tool_dispatch(b.name, b.body, b.body_len, g_out, (int)sizeof g_out);
            } else if (std::strcmp(b.name, "Spawn") == 0) {
                if (j_field(b.body, b.body_len, "cmd", g_arg, (int)sizeof g_arg) < 0)
                    strcpy(g_out, "error: missing 'cmd'");
                else {
                    snprintf(sum, sizeof sum, "%.150s", g_arg);
                    if (cb.tool) cb.tool(b.name, sum);
                    do_spawn(g_arg, g_out, (int)sizeof g_out);
                }
            } else if (std::strcmp(b.name, "Agent") == 0) {
                if (j_field(b.body, b.body_len, "prompt", g_arg, (int)sizeof g_arg) < 0)
                    strcpy(g_out, "error: missing 'prompt'");
                else {
                    snprintf(sum, sizeof sum, "%.150s", g_arg);
                    if (cb.tool) cb.tool(b.name, sum);
                    do_agent(g_arg, g_out, (int)sizeof g_out);
                }
            } else if (std::strcmp(b.name, "Windows") == 0) {
                if (cb.tool) cb.tool(b.name, "");
                do_windows(g_out, (int)sizeof g_out);
            } else if (std::strcmp(b.name, "Agents") == 0) {
                if (cb.tool) cb.tool(b.name, "");
                do_agents(g_out, (int)sizeof g_out);
            } else if (std::strcmp(b.name, "AgentResult") == 0) {
                if (j_field(b.body, b.body_len, "id", g_arg, (int)sizeof g_arg) < 0)
                    strcpy(g_out, "error: missing 'id'");
                else {
                    if (cb.tool) cb.tool(b.name, g_arg);
                    do_agent_result(atoi(g_arg), g_out, (int)sizeof g_out);
                }
            } else {
                snprintf(g_out, sizeof g_out, "error: unknown tool '%s'", b.name);
                if (cb.tool) cb.tool(b.name, "?");
            }
            int trn = build_tool_result(g_req, (int)sizeof g_req, b.id, g_out);
            if (trn <= 0) {
                g_out[512] = 0; /* truncate, retry; always fits now */
                trn = build_tool_result(g_req, (int)sizeof g_req, b.id, g_out);
            }
            if (ntool == 0) { conv_open(&g_cv, CONV_ROLE_USER, 1); conv_write(&g_cv, "[", 1); }
            else conv_write(&g_cv, ",", 1);
            conv_write(&g_cv, g_req, trn);
            ntool++;
        }
        if (ntool) { conv_write(&g_cv, "]", 1); conv_commit(&g_cv); }

        if (std::strcmp(R.stop_reason, "tool_use") != 0) {
            cb.status("done");
            return 0;
        }
        if (!ntool) { cb.status("tool_use stop with no tool block"); return -1; }
    }
    cb.status("too many tool iterations");
    return -1;
}
