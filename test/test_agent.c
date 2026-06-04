/* End-to-end agent_run() against a scripted in-memory "TLS server".
 * Links real agent/http/conv/anth/jsonp/json_write/util; stubs everything
 * that touches the network, terminal, or tools. */

#include <stdarg.h>
#include "t.h"
#include "../src/core/agent.h"
#include "../src/core/conv.h"
#include "../src/core/http.h"
#include "../src/core/tools.h"
#include "../src/core/tls_client.h"
#include "../src/plat/plat.h"

/* --- fake TLS: writes -> g_sent, reads <- g_wire ---------------------- */

static char g_sent[16384];
static int g_sent_len;
static char g_wire[4096];
static int g_wire_len, g_wire_pos;
struct tls_conn {
    int dummy;
};
static struct tls_conn g_conn;

tls_conn *tls_open(const struct net_cfg *nc, const char *host)
{
    (void)nc;
    (void)host;
    return &g_conn;
}
int tls_write(tls_conn *c, const void *buf, int len)
{
    (void)c;
    if (g_sent_len + len > (int)sizeof g_sent) return -1;
    memcpy(g_sent + g_sent_len, buf, (size_t)len);
    g_sent_len += len;
    return len;
}
int tls_read(tls_conn *c, void *buf, int len)
{
    /* Serve one byte at a time so http_read_response's internal 1KB buffer
     * never reads past the current response into the next queued one. */
    (void)c;
    (void)len;
    if (g_wire_pos >= g_wire_len) return 0;
    ((char *)buf)[0] = g_wire[g_wire_pos++];
    return 1;
}
void tls_close(tls_conn *c) { (void)c; }
const char *tls_last_error_str(void) { return "stub"; }
int tls_last_error(void) { return 0; }

static void wire_reset(void)
{
    g_sent_len = 0;
    g_wire_len = 0;
    g_wire_pos = 0;
}
static void wire_add_resp(const char *json)
{
    g_wire_len +=
        snprintf(g_wire + g_wire_len, sizeof g_wire - (size_t)g_wire_len,
                 "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%s",
                 (int)strlen(json), json);
}

/* --- netcfg / ui stubs ------------------------------------------------ */
int g_verbose = 0;
void vtrace(const char *fmt, ...) { (void)fmt; }
void vdump(const char *l, const char *b, int n)
{
    (void)l;
    (void)b;
    (void)n;
}
void spin(const char *l) { (void)l; }
void spin_clear(void) {}
void errf(const char *fmt, ...) { (void)fmt; }

/* --- scroll stubs (capture transcript) -------------------------------- */
static char g_tx[4096];
static int g_tx_len;
void scroll_out(const char *s, int len)
{
    if (g_tx_len + len < (int)sizeof g_tx) {
        memcpy(g_tx + g_tx_len, s, (size_t)len);
        g_tx_len += len;
    }
}
void scroll_outz(const char *s) { scroll_out(s, (int)strlen(s)); }
void scroll_printf(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    scroll_outz(b);
}
int scroll_pager(const char *b, int n)
{
    (void)b;
    (void)n;
    return 0;
}

/* --- plat stubs (only what agent.c touches) --------------------------- */
char plat_dirsep(void) { return '/'; }
const char *plat_os_desc(void) { return "test-host"; }
const char *plat_charset_hint(void) { return ""; }
int plat_getcwd(char *out, int cap)
{
    (void)out;
    (void)cap;
    return 0;
}
int plat_con_raw(int on)
{
    (void)on;
    return 0;
}
int plat_getkey(void) { return 'n'; }
void plat_con_attr(int a) { (void)a; }
int plat_con_size(int *r, int *c)
{
    (void)r;
    (void)c;
    return 0;
}

/* --- tools stubs ------------------------------------------------------ */
static int g_tool_calls;
const char *tool_name(int idx)
{
    (void)idx;
    return "Read";
}
const char *tools_json(void)
{
    return "[{\"name\":\"Read\",\"description\":\"r\","
           "\"input_schema\":{\"type\":\"object\"}}]";
}
int tool_index(const char *name)
{
    return strcmp(name, "Read") ? -1 : TOOL_READ;
}
int tool_preview(int idx, const char *in, int ilen, char *out, int cap)
{
    (void)idx;
    (void)in;
    (void)ilen;
    (void)out;
    (void)cap;
    return 0;
}
int tool_dispatch(const char *name, const char *in, int ilen, char *out,
                  int cap)
{
    (void)in;
    (void)ilen;
    g_tool_calls++;
    snprintf(out, (size_t)cap, "stub-out for %s\n\"q\"", name);
    return 0;
}

/* --- helpers ---------------------------------------------------------- */

#define CONV_PATH "test_agent_conv.dat"

typedef struct {
    const char *p;
    int n, i;
} mr_t;
static int mr_read(void *ctx, char *buf, int len)
{
    mr_t *m = (mr_t *)ctx;
    int avail = m->n - m->i, take = len < avail ? len : avail;
    if (take <= 0) return 0;
    memcpy(buf, m->p + m->i, (size_t)take);
    m->i += take;
    return take;
}

/* g_sent holds "POST ...\r\n\r\n<chunked body>" for one or more requests.
 * Decode the first request's chunked body via http_read_response. */
static int decode_req_body(char *out, int cap)
{
    static char tmp[16384];
    const char *p = g_sent, *end = g_sent + g_sent_len;
    http_resp r;
    mr_t m;
    int hlen;
    while (p + 4 <= end && memcmp(p, "\r\n\r\n", 4) != 0)
        p++;
    if (p + 4 > end) return -1;
    p += 4;
    hlen = snprintf(tmp, sizeof tmp,
                    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    if (hlen + (end - p) > (long)sizeof tmp) return -1;
    memcpy(tmp + hlen, p, (size_t)(end - p));
    m.p = tmp;
    m.i = 0;
    m.n = hlen + (int)(end - p);
    r.body = out;
    r.body_cap = cap;
    if (http_read_response(mr_read, &m, &r) != 0) return -1;
    return r.body_len;
}

/* --------------------------------------------------------------------- */

static const char RESP_TEXT[] =
    "{\"type\":\"message\",\"role\":\"assistant\",\"content\":["
    "{\"type\":\"text\",\"text\":\"hello\"}],"
    "\"stop_reason\":\"end_turn\"}";

static const char RESP_TOOL[] =
    "{\"type\":\"message\",\"role\":\"assistant\",\"content\":["
    "{\"type\":\"text\",\"text\":\"ok\"},"
    "{\"type\":\"tool_use\",\"id\":\"tu_1\",\"name\":\"Read\","
    "\"input\":{\"path\":\"x\"}}],"
    "\"stop_reason\":\"tool_use\"}";

static const char RESP_TOOL_MAXTOK[] =
    "{\"type\":\"message\",\"role\":\"assistant\",\"content\":["
    "{\"type\":\"tool_use\",\"id\":\"tu_m\",\"name\":\"Read\","
    "\"input\":{}}],"
    "\"stop_reason\":\"max_tokens\"}";

static const char RESP_DONE[] =
    "{\"type\":\"message\",\"role\":\"assistant\",\"content\":["
    "{\"type\":\"text\",\"text\":\"done.\"}],"
    "\"stop_reason\":\"end_turn\"}";

int main(void)
{
    conv_t cv;
    static char body[16384];
    static relic_cfg cfg;
    static agent_scratch sc;
    int n;

    t_group("agent");

    strcpy(cfg.model, "test-model");
    strcpy(cfg.key, "sk-test");
    cfg.yolo = 1;
    cfg.max_tokens = 2048;

    /* --- text-only round-trip ---------------------------------------- */
    wire_reset();
    g_tx_len = 0;
    wire_add_resp(RESP_TEXT);
    t_int(conv_init(&cv, CONV_PATH, 0), 0, "text: conv_init");
    conv_push_text(&cv, CONV_ROLE_USER, "hi");
    t_int(agent_run(&cv, &cfg, &sc), 0, "text: agent_run rc");
    t_int(cv.count, 2, "text: conv count 2");
    g_tx[g_tx_len] = 0;
    t_has(g_tx, "hello", "text: transcript has reply");
    g_sent[g_sent_len] = 0;
    t_mem(g_sent, "POST /v1/messages HTTP/1.1\r\n", 28, "text: POST line");
    t_has(g_sent, "x-api-key: sk-test\r\n", "text: api key header");
    t_has(g_sent, "Transfer-Encoding: chunked\r\n", "text: chunked header");
    n = decode_req_body(body, (int)sizeof body);
    t_ok(n > 0, "text: req body decoded");
    body[n > 0 ? n : 0] = 0;
    t_ok(n > 0 && body[0] == '{' && body[n - 1] == '}', "text: body braces");
    t_has(body, "\"model\":\"test-model\"", "text: body model");
    t_has(body, "\"system\":", "text: body system");
    t_has(body, "\"tools\":[", "text: body tools");
    t_has(body, "\"messages\":[", "text: body messages");
    t_has(body, "\"hi\"", "text: body user msg");

    /* --- tool_use -> tool_result -> end_turn ------------------------- */
    wire_reset();
    g_tx_len = 0;
    g_tool_calls = 0;
    wire_add_resp(RESP_TOOL);
    wire_add_resp(RESP_DONE);
    t_int(conv_init(&cv, CONV_PATH, 0), 0, "tool: conv_init");
    conv_push_text(&cv, CONV_ROLE_USER, "go");
    t_int(agent_run(&cv, &cfg, &sc), 0, "tool: agent_run rc");
    t_int(g_tool_calls, 1, "tool: dispatch called once");
    t_int(cv.count, 4, "tool: conv count 4");
    t_int(cv.turns[1].role, CONV_ROLE_ASST, "tool: turn1 asst");
    t_ok(cv.turns[2].role == CONV_ROLE_USER && cv.turns[2].is_json,
         "tool: turn2 user/json");
    t_int(cv.turns[3].role, CONV_ROLE_ASST, "tool: turn3 asst");
    n = (int)conv_read(&cv, 2, body, (long)sizeof body);
    body[n > 0 ? n : 0] = 0;
    t_has(body, "\"tool_use_id\":\"tu_1\"", "tool: result id");
    t_has(body, "\"is_error\":false", "tool: result is_error");
    t_has(body, "stub-out for Read\\n\\\"q\\\"", "tool: result body escaped");
    g_tx[g_tx_len] = 0;
    t_has(g_tx, "[tool] Read", "tool: transcript header");
    t_has(g_tx, "stub-out for Read", "tool: transcript echo");
    t_has(g_tx, "done.", "tool: transcript final");

    /* --- stop_reason=max_tokens with tool_use: loop must continue ---- */
    wire_reset();
    g_tx_len = 0;
    g_tool_calls = 0;
    wire_add_resp(RESP_TOOL_MAXTOK);
    wire_add_resp(RESP_DONE);
    t_int(conv_init(&cv, CONV_PATH, 0), 0, "maxtok: conv_init");
    conv_push_text(&cv, CONV_ROLE_USER, "go");
    t_int(agent_run(&cv, &cfg, &sc), 0, "maxtok: agent_run rc");
    t_int(g_tool_calls, 1, "maxtok: tool still ran");
    t_int(cv.count, 4, "maxtok: did not bail");
    g_tx[g_tx_len] = 0;
    t_has(g_tx, "hit max_tokens", "maxtok: warning shown");
    t_has(g_tx, "done.", "maxtok: loop completed");

    /* --- conv window drop must not orphan a tool_result ------------- */
    t_int(conv_init(&cv, CONV_PATH, 0), 0, "drop: conv_init");
    conv_push_text(&cv, CONV_ROLE_USER, "go");
    {
        int bad = 0;
        for (n = 0; n < CONV_CAP; n++) {
            conv_push(&cv, CONV_ROLE_ASST, 1,
                      "[{\"type\":\"tool_use\",\"id\":\"x\"}]", -1);
            conv_push(&cv, CONV_ROLE_USER, 1,
                      "[{\"type\":\"tool_result\",\"tool_use_id\":\"x\"}]", -1);
            if (cv.turns[0].role == CONV_ROLE_USER && cv.turns[0].is_json)
                bad = 1;
        }
        t_int(bad, 0, "drop: head never orphan tool_result");
    }
    t_ok(cv.dropped > 0, "drop: window did wrap");
    t_ok(cv.count > CONV_CAP / 2, "drop: didn't cascade");

    /* --- API error path --------------------------------------------- */
    wire_reset();
    wire_add_resp(
        "{\"type\":\"error\",\"error\":{\"type\":\"x\",\"message\":\"nope\"}}");
    t_int(conv_init(&cv, CONV_PATH, 0), 0, "error: conv_init");
    conv_push_text(&cv, CONV_ROLE_USER, "hi");
    t_int(agent_run(&cv, &cfg, &sc), 1, "error: agent_run rc 1");
    t_int(cv.count, 1, "error: no asst turn recorded");

    remove(CONV_PATH);
    return t_done();
}
