#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "agent.h"
#include "anth.h"
#include "util.h"
#include "json_write.h"
#include "jsonp.h"
#include "ui.h"
#include "tools.h"
#include "scroll.h"
#include "xport.h"
#include "../plat/plat.h"

/* Request body is streamed (chunked) from the disk store, so this only
 * bounds upload size / model context, not any RAM buffer. */
#define CONV_BUDGET (192L * 1024)

static const char SYS_PROMPT_FMT[] =
    "You are Relic, an interactive coding agent running on a retro "
    "PC (%s). You help the user with software engineering tasks in the "
    "current working directory.\n"
    "\n"
    "# Tools\n"
    "- Prefer Read/Write/Edit/LS/Grep/Glob over shell equivalents (cat, "
    "echo >, sed -i, ls, grep -r, find). Reserve %s for commands with no "
    "dedicated tool (builds, tests, git).\n"
    "- Read prefixes each line with NUMBER<tab>. When calling Edit, match "
    "the file bytes exactly -- do NOT include that prefix, and preserve "
    "the original indentation.\n"
    "\n"
    "# Output\n"
    "The terminal is %d columns, plain text only. Do NOT use markdown (no "
    "**bold**, # headers, ``` fences, or tables). Keep replies short: a "
    "sentence before tool calls, a brief summary after. Skip preamble like "
    "'Certainly!' or restating the request. %s\n"
    "\n"
    "# Doing tasks\n"
    "- Find the relevant code, make the change, then verify (run the build "
    "or tests if available).\n"
    "- Report faithfully: if a command failed or you skipped a step, say "
    "so. Never claim success you did not observe.\n"
    "- Do not add comments, error handling, or abstractions beyond what "
    "the task requires.\n"
    "- For destructive or irreversible actions (rm -rf, git push --force, "
    "dropping data), ask the user first unless they explicitly authorized "
    "it.";

/* Walk cwd -> root reading CLAUDE.md files into buf[*off..cap), each
 * prefixed with its path. Returns count found. */
static int load_claude_md(char *buf, int cap, int *off)
{
    char path[PLAT_PATH_MAX];
    int found = 0;
    int n = plat_getcwd(path, (int)sizeof path);
    if (!n) return 0;
    for (;;) {
        FILE *f;
        char sep = plat_dirsep();
        char suf[12];
        int slen = 0;
        if (n && path[n - 1] != '/' && path[n - 1] != '\\'
            && path[n - 1] != sep)
            suf[slen++] = sep;
        memcpy(suf + slen, "CLAUDE.md", 10);
        slen += 9;
        if ((size_t)(n + slen) >= sizeof path) return found;
        memcpy(path + n, suf, (size_t)slen + 1);
        f = fopen(path, "rb");
        if (f) {
            int r;
            if (found++ == 0)
                sb_put(buf, cap, off,
                       "\n\nIMPORTANT: The following CLAUDE.md project "
                       "instructions OVERRIDE defaults. Follow them.\n",
                       -1);
            sb_put(buf, cap, off, "\n--- ", -1);
            sb_put(buf, cap, off, path, -1);
            sb_put(buf, cap, off, " ---\n", -1);
            r = (cap - 1 > *off)
                    ? (int)fread(buf + *off, 1, (size_t)(cap - 1 - *off), f)
                    : 0;
            if (r > 0) *off += r;
            fclose(f);
        }
        while (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\'
               && path[n - 1] != sep)
            n--;
        if (n <= 1) break; /* reached "/" or relative root */
        n--;
        if (n == 2 && path[1] == ':') break; /* reached "C:" */
    }
    buf[*off < cap ? *off : cap - 1] = 0;
    return found;
}

/* --- permission prompts ----------------------------------------------- */

/* Decide whether to run a tool call. Returns 1=run, 0=deny.
 * The caller has already printed the tool header + preview to the
 * transcript, so this only emits the y/n prompt itself. */
static int ask_permission(relic_cfg *cfg, int idx, const char *name,
                          const char *input, int ilen)
{
    char line[8], dname[32];

    /* The model supplies `name`; print a sanitised copy so a hallucinated
     * tool name can't carry escape bytes into the y/n prompt. */
    str_set(dname, (int)sizeof dname, name);
    tty_sanitize(dname, (int)strlen(dname));

    if (cfg->yolo) return 1;
    if (idx >= 0 && cfg->perm_always[idx]) return 1;
    /* Read-only tools auto-allow. */
    if (idx == TOOL_READ || idx == TOOL_LIST || idx == TOOL_GREP
        || idx == TOOL_GLOB)
        return 1;
    if (cfg->accept_edits && (idx == TOOL_WRITE || idx == TOOL_EDIT)) return 1;

    if (cfg->noninteractive) {
        printf("  (non-interactive: denied. Use %s to auto-approve.)\n",
               (idx == TOOL_WRITE || idx == TOOL_EDIT)
                   ? "--permission-mode acceptEdits"
                   : "--dangerously-skip-permissions");
        return 0;
    }
    for (;;) {
        int k, is_edit = (idx == TOOL_WRITE || idx == TOOL_EDIT);
        if (is_edit)
            printf("  Allow? [y]es / [n]o / [a]llow all edits / [v]iew: ");
        else
            printf("  Allow? [y]es / [n]o / [a]lways for '%s' / [v]iew: ",
                   dname);
        fflush(stdout);
        /* Single-key read via plat_getkey() -- same path as read_prompt().
         * Mixing conio getch() (read_prompt) with stdio fgets here left a
         * phantom newline in stdin on Win9x and double-printed the prompt. */
        if (plat_con_raw(1)) {
            k = plat_getkey();
            plat_con_raw(0);
            if (k > 32 && k < 127) fputc(k, stdout);
            fputc('\n', stdout);
        } else {
            if (!fgets(line, (int)sizeof line, stdin)) return 0;
            k = (unsigned char)line[0];
        }
        switch (k) {
        case 'y':
        case 'Y': return 1;
        case 'a':
        case 'A':
            if (is_edit) {
                cfg->accept_edits = 1;
                printf("  (accept-edits: ON for this session)\n");
            } else if (idx >= 0) {
                cfg->perm_always[idx] = 1;
            }
            return 1;
        case 'n':
        case 'N': return 0;
        case 'v':
        case 'V':
            if (!scroll_pager(input, ilen)) printf("    %.*s\n", ilen, input);
            break;
        case 0:
        case 4: return 0; /* read error / Ctrl-D: deny */
        default: break;   /* unrecognized: re-prompt */
        }
    }
}

int agent_list_models(const relic_cfg *cfg, agent_scratch *sc)
{
    int status, blen, n;
    if (anth_get(&cfg->net, cfg->key, ANTH_MODELS_PATH, sc->body,
                 (int)sizeof sc->body, &status, &blen)
        != 0)
        return -1;
    n = anth_parse_models(sc->body, blen, sc->out, (int)sizeof sc->out);
    if (n < 0) {
        scroll_printf("  (could not list models: HTTP %d: %s)\n", status,
                      sc->out);
        return -1;
    }
    scroll_printf("  Available models (%d):\n", n);
    scroll_outz(sc->out);
    return 0;
}

static int wr_conv(void *c, const char *s, int n)
{
    return conv_write((conv_t *)c, s, n);
}

/* Stream one tool_result block straight to the conversation store. */
static int write_tool_result(conv_t *cv, const char *id, int is_err,
                             const char *out)
{
    if (conv_write(cv, "{\"type\":\"tool_result\",\"tool_use_id\":\"", -1) < 0)
        return -1;
    if (conv_write(cv, id, -1) < 0) return -1;
    if (conv_write(cv,
                   is_err ? "\",\"is_error\":true,\"content\":\""
                          : "\",\"is_error\":false,\"content\":\"",
                   -1)
        < 0)
        return -1;
    if (json_escape_to(wr_conv, cv, out, (int)strlen(out)) < 0) return -1;
    return conv_write(cv, "\"}", 2);
}

static void sys_init(agent_scratch *sc)
{
    int n, rows = 0, cols = 0;
    if (sc->sys_len) return;
    if (!plat_con_size(&rows, &cols) || cols <= 0) cols = 80;
    (void)rows;
    sc->sys_len =
        snprintf(sc->sys, (int)sizeof sc->sys, SYS_PROMPT_FMT, plat_os_desc(),
                 tool_name(TOOL_SHELL), cols, plat_charset_hint());
    n = load_claude_md(sc->sys, (int)sizeof sc->sys, &sc->sys_len);
    if (n) errf("(loaded %d CLAUDE.md file%s)", n, n == 1 ? "" : "s");
}

#define AGENT_MAX_TURNS 64

int agent_run(conv_t *cv, relic_cfg *cfg, agent_scratch *sc)
{
    int turn, use_tools = !cfg->chat_only;
    sys_init(sc);
    g_agent_interrupt = 0;

    for (turn = 0; turn < AGENT_MAX_TURNS; turn++) {
        anth_result res;
        req_ctx rc;
        int status, blen, i, rl;
        int ntool = 0;

        if (g_agent_interrupt) {
            scroll_printf("(interrupted)\n");
            return 0;
        }
        if (conv_trim(cv, CONV_BUDGET))
            errf("(oldest turns dropped to fit request)");
        rc.model = cfg->model;
        rc.max_tokens = cfg->max_tokens;
        rc.sys = use_tools ? sc->sys : NULL;
        rc.tools = use_tools ? tools_json() : NULL;
        rc.cv = cv;

        if (anth_post(&cfg->net, cfg->key, &rc, sc->body, (int)sizeof sc->body,
                      &status, &blen)
            != 0) {
            if (g_agent_interrupt) {
                scroll_printf("(interrupted)\n");
                return 0;
            }
            return -1;
        }
        if (anth_parse(sc->body, blen, &res) != 0) {
            errf("JSON parse failed (status=%d)", status);
            errf("%.*s", blen < 200 ? blen : 200, sc->body);
            return -1;
        }
        if (res.is_error || status >= 400) {
            tty_sanitize(res.err_msg, (int)strlen(res.err_msg));
            errf("API error %d: %s", status, res.err_msg);
            return 1;
        }

        /* Record assistant content[] verbatim while sc->body still holds it. */
        if (conv_push(cv, CONV_ROLE_ASST, 1, res.content_json,
                      res.content_json_len)
            < 0)
            goto io_err;

        for (i = 0; i < res.nblocks; i++) {
            struct anth_block *b = &res.blocks[i];
            if (b->kind == ANTH_BLK_TEXT) {
                int n = jsonp_unescape_span(b->body, b->body_len, sc->out,
                                            (int)sizeof sc->out);
                if (n < 0) n = (int)strlen(sc->out); /* truncated; show fit */
                scroll_out(sc->out, n);
                scroll_out("\n", 1);
            } else if (b->kind == ANTH_BLK_TOOL && use_tools) {
                int idx = tool_index(b->name);
                int is_err;
                char pv[256], *p;
                plat_con_attr(PLAT_ATTR_TOOL);
                scroll_outz("[tool] ");
                scroll_outz(b->name);
                rl =
                    tool_preview(idx, b->body, b->body_len, pv, (int)sizeof pv);
                if (rl > 0) {
                    scroll_outz("  ");
                    for (p = pv; *p;) {
                        char *nl = strchr(p, '\n');
                        int n = nl ? (int)(nl - p) + 1 : (int)strlen(p);
                        scroll_out(p, n);
                        p += n;
                        if (*p) scroll_outz("       ");
                    }
                } else
                    scroll_out("\n", 1);
                plat_con_attr(PLAT_ATTR_RESET);
                fflush(stdout);
                if (!ask_permission(cfg, idx, b->name, b->body, b->body_len)) {
                    strcpy(sc->out,
                           "User denied permission for this tool call. "
                           "Do not retry it; ask the user or try a "
                           "different approach.");
                    is_err = 1;
                } else {
                    spin("Working");
                    is_err = (tool_dispatch(b->name, b->body, b->body_len,
                                            sc->out, (int)sizeof sc->out)
                              != 0);
                    spin_clear();
                }
                /* Echo first line of the tool result so the user sees it ran
                 * (the full text goes only to the model). */
                for (rl = 0; rl < 72 && sc->out[rl] && sc->out[rl] != '\n';
                     rl++)
                    ;
                plat_con_attr(PLAT_ATTR_DIM);
                scroll_out("  ", 2);
                scroll_out(sc->out, rl);
                if (sc->out[rl]) scroll_out("...", 3);
                scroll_out("\n", 1);
                plat_con_attr(PLAT_ATTR_RESET);
                if (ntool == 0) {
                    if (conv_open(cv, CONV_ROLE_USER, 1) < 0
                        || conv_write(cv, "[", 1) < 0)
                        goto io_err;
                } else if (conv_write(cv, ",", 1) < 0)
                    goto io_err;
                if (write_tool_result(cv, b->id, is_err, sc->out) < 0)
                    goto io_err;
                ntool++;
            }
        }
        if (ntool > 0)
            if (conv_write(cv, "]", 1) < 0 || conv_commit(cv) < 0) goto io_err;

        if (strcmp(res.stop_reason, "max_tokens") == 0)
            scroll_printf("(hit max_tokens=%d; response truncated -- raise "
                          "with /status max_tokens N)\n",
                          cfg->max_tokens);
        /* Continue whenever we sent tool_results -- covers both
         * stop_reason=tool_use and =max_tokens (truncated tool input; the
         * model gets the error result and can retry smaller). */
        if (ntool == 0 || !use_tools) return 0;
    }
    errf("(stopped after %d tool turns)", AGENT_MAX_TURNS);
    return 0;
io_err:
    errf("history I/O error");
    return -1;
}
