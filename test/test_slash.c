#include "t.h"
#include <stdarg.h>
#include "../src/core/slash.h"

/* slash.c writes through scroll_printf(); capture it. */
static char g_out[1024];
static int g_out_len;
void scroll_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_out_len +=
        vsnprintf(g_out + g_out_len, sizeof g_out - (size_t)g_out_len, fmt, ap);
    va_end(ap);
}
static void cap_reset(void)
{
    g_out_len = 0;
    g_out[0] = 0;
}

static const char *g_arg;
static int h_cap(const char *a)
{
    g_arg = a;
    return 0;
}
static int h_quit(const char *a)
{
    g_arg = a;
    return 1;
}

static const slash_cmd cmds[] = {
    {"help", "", "show help", h_cap},
    {"model", "[ID]", "set model", h_cap},
    {"quit", "", "exit", h_quit},
    {"secret", "", "", h_cap} /* help "" -> hidden from listing */
};
#define NCMDS ((int)(sizeof cmds / sizeof cmds[0]))

int main(void)
{
    t_group("slash");

    /* --- not a slash command ----------------------------------------- */
    t_int(slash_dispatch(cmds, NCMDS, "hello"), -1, "dispatch: no slash -> -1");
    t_int(slash_dispatch(cmds, NCMDS, ""), -1, "dispatch: empty -> -1");

    /* --- bare "/" prints help ---------------------------------------- */
    cap_reset();
    t_int(slash_dispatch(cmds, NCMDS, "/"), 0, "dispatch: bare / -> 0");
    t_has(g_out, "/help", "dispatch: bare / lists help");
    t_has(g_out, "/model", "dispatch: bare / lists model");
    t_ok(strstr(g_out, "/secret") == 0, "dispatch: hidden cmd omitted");

    /* --- known command, no arg --------------------------------------- */
    g_arg = "?";
    t_int(slash_dispatch(cmds, NCMDS, "/help"), 0, "dispatch: /help -> 0");
    t_str(g_arg, "", "dispatch: no arg -> \"\"");

    /* --- with arg ---------------------------------------------------- */
    g_arg = 0;
    t_int(slash_dispatch(cmds, NCMDS, "/model model-x"), 0,
          "dispatch: /model arg -> 0");
    t_str(g_arg, "model-x", "dispatch: arg captured");

    /* --- leading-space arg trimmed ----------------------------------- */
    g_arg = 0;
    slash_dispatch(cmds, NCMDS, "/model    spaced");
    t_str(g_arg, "spaced", "dispatch: arg leading spaces trimmed");

    /* --- handler return value propagates ----------------------------- */
    t_int(slash_dispatch(cmds, NCMDS, "/quit"), 1, "dispatch: /quit -> 1");

    /* --- unknown ----------------------------------------------------- */
    cap_reset();
    t_int(slash_dispatch(cmds, NCMDS, "/nope"), 0, "dispatch: unknown -> 0");
    t_has(g_out, "unknown command: /nope", "dispatch: unknown message");
    t_has(g_out, "/help", "dispatch: unknown shows help");

    /* --- exact match only (no prefix) -------------------------------- */
    cap_reset();
    t_int(slash_dispatch(cmds, NCMDS, "/mod"), 0, "dispatch: prefix not match");
    t_has(g_out, "unknown", "dispatch: prefix -> unknown");
    cap_reset();
    t_int(slash_dispatch(cmds, NCMDS, "/modelx"), 0,
          "dispatch: superstring not match");
    t_has(g_out, "unknown", "dispatch: superstring -> unknown");

    /* --- slash_help direct ------------------------------------------- */
    cap_reset();
    slash_help(cmds, NCMDS);
    t_has(g_out, "show help", "help: includes desc");
    t_has(g_out, "[ID]", "help: includes args column");
    t_ok(strstr(g_out, "secret") == 0, "help: hides empty-help cmd");

    return t_done();
}
