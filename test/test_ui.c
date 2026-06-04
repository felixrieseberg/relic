#include "t.h"
#include "../src/core/ui.h"

/* plat stubs for spin() */
unsigned long plat_time_unix(void) { return 1000; }
const char *plat_spinner(unsigned i)
{
    (void)i;
    return "*";
}

static char g_err[256];
static int g_err_n;
static void cap_err(const char *line)
{
    g_err_n++;
    strncpy(g_err, line, sizeof g_err - 1);
    g_err[sizeof g_err - 1] = 0;
}

static char g_spin_label[32];
static unsigned g_spin_secs;
static int g_spin_n;
static void cap_spin(const char *label, unsigned secs)
{
    g_spin_n++;
    strncpy(g_spin_label, label, sizeof g_spin_label - 1);
    g_spin_secs = secs;
}

int main(void)
{
    t_group("ui");

    /* --- errf sink redirection --------------------------------------- */
    errf_set_sink(cap_err);
    g_err_n = 0;
    errf("hello %d", 42);
    t_int(g_err_n, 1, "errf: sink called once");
    t_str(g_err, "hello 42", "errf: formatted line");
    errf("%s", "second");
    t_str(g_err, "second", "errf: second call");
    errf_set_sink(NULL); /* restore default; can't easily test stderr */
    t_ok(1, "errf_set_sink: NULL restores default (no crash)");

    /* --- vtrace gated by g_verbose ----------------------------------- */
    /* Can only check it doesn't crash when off; output goes to stderr. */
    g_verbose = 0;
    vtrace("should not print");
    t_ok(1, "vtrace: g_verbose=0 no-op");
    vdump("lbl", "abc", 3);
    t_ok(1, "vdump: g_verbose<2 no-op");

    /* --- spin sink redirection --------------------------------------- */
    spin_set_sink(cap_spin);
    g_verbose = 0;
    g_spin_n = 0;
    spin("connecting");
    t_int(g_spin_n, 1, "spin: sink called");
    t_str(g_spin_label, "connecting", "spin: label set");
    t_int(g_spin_secs, 0, "spin: secs from t0");
    spin(NULL); /* keep label, advance */
    t_int(g_spin_n, 2, "spin: NULL advances");
    t_str(g_spin_label, "connecting", "spin: label retained");
    spin("sending");
    t_str(g_spin_label, "sending", "spin: label changed");

    /* g_verbose suppresses spin */
    g_verbose = 1;
    g_spin_n = 0;
    spin("x");
    t_int(g_spin_n, 0, "spin: suppressed when verbose");
    g_verbose = 0;

    spin_clear();
    g_spin_n = 0;
    spin("fresh");
    t_str(g_spin_label, "fresh", "spin_clear: resets for new run");

    spin_set_sink(NULL);
    t_ok(1, "spin_set_sink: NULL restores default");

    return t_done();
}
