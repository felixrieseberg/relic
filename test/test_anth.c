#include "t.h"
#include "../src/core/anth.h" /* built with -DANTH_TEST_HELPERS */
#include "../src/core/conv.h"
#include "../src/core/json_write.h"

static char out[4096];

static int build(conv_t *cv, char *buf, int cap, const char *sys,
                 const char *tools)
{
    t_buf m;
    m.p = buf;
    m.cap = cap;
    m.n = 0;
    if (conv_send_request(t_buf_wr, &m, "mdl", 42, sys, tools, cv)) return -1;
    buf[m.n] = 0;
    return m.n;
}

#define CC ",\"cache_control\":{\"type\":\"ephemeral\"}"

int main(void)
{
    t_group("anth");

    /* json_escape_str smoke (full coverage in test_json_write) */
    {
        char buf[128];
        int off = 0;
        t_int(json_escape_str(buf, (int)sizeof buf, &off, "a\"b\\c\nd\te\001f"),
              0, "json_escape_str: ok");
        buf[off] = 0;
        t_str(buf, "a\\\"b\\\\c\\nd\\te\\u0001f", "json_escape_str: output");
    }

    /* conv_send_request: text + raw-json turns, system + tools.
     * cache_control lands on last tool def + last message. */
    {
        static conv_t cv;
        char buf[1024];
        remove("test_conv.dat");
        t_int(conv_init(&cv, "test_conv.dat", 0), 0, "send_req: conv_init");
        conv_push_text(&cv, CONV_ROLE_USER, "hi \"x\"");
        conv_push(&cv, CONV_ROLE_ASST, 1,
                  "[{\"type\":\"text\",\"text\":\"y\"}]", -1);
        t_ok(build(&cv, buf, (int)sizeof buf, "sys", "[{\"name\":\"t\"}]") > 0,
             "send_req: build ok");
        t_str(buf,
              "{\"model\":\"mdl\",\"max_tokens\":42,"
              "\"system\":\"sys\","
              "\"tools\":[{\"name\":\"t\"" CC "}],"
              "\"messages\":["
              "{\"role\":\"user\",\"content\":\"hi \\\"x\\\"\"},"
              "{\"role\":\"assistant\",\"content\":[{\"type\":"
              "\"text\",\"text\":\"y\"" CC "}]}]}",
              "send_req: tools+system+cache markers");
        /* push a 3rd user-text turn -> last-msg cache marker takes the
         * text-wrap path; previous asst goes back to verbatim. */
        conv_push_text(&cv, CONV_ROLE_USER, "go");
        t_ok(build(&cv, buf, (int)sizeof buf, NULL, NULL) > 0,
             "send_req: build2 ok");
        t_str(buf,
              "{\"model\":\"mdl\",\"max_tokens\":42,\"messages\":["
              "{\"role\":\"user\",\"content\":\"hi \\\"x\\\"\"},"
              "{\"role\":\"assistant\",\"content\":[{\"type\":"
              "\"text\",\"text\":\"y\"}]},"
              "{\"role\":\"user\",\"content\":[{\"type\":\"text\","
              "\"text\":\"go\"" CC "}]}]}",
              "send_req: text-wrap cache marker");
        fclose(cv.fp);
    }

    /* conv: persistence -- reopen rebuilds index from store */
    {
        static conv_t cv;
        char buf[1024], rd[64];
        t_int(conv_init(&cv, "test_conv.dat", 1), 0, "resume: reopen");
        t_int(cv.count, 3, "resume: count");
        t_ok(cv.turns[0].role == CONV_ROLE_USER && cv.turns[0].is_json == 0
                 && cv.turns[0].len == 6,
             "resume: turn0 meta");
        t_ok(cv.turns[1].role == CONV_ROLE_ASST && cv.turns[1].is_json == 1,
             "resume: turn1 meta");
        t_int(conv_read(&cv, 0, rd, (long)sizeof rd), 6, "resume: read len");
        t_mem(rd, "hi \"x\"", 6, "resume: read bytes");
        t_ok(build(&cv, buf, (int)sizeof buf, NULL, NULL) > 0,
             "resume: rebuild ok");
        t_str(buf,
              "{\"model\":\"mdl\",\"max_tokens\":42,\"messages\":["
              "{\"role\":\"user\",\"content\":\"hi \\\"x\\\"\"},"
              "{\"role\":\"assistant\",\"content\":[{\"type\":"
              "\"text\",\"text\":\"y\"}]},"
              "{\"role\":\"user\",\"content\":[{\"type\":\"text\","
              "\"text\":\"go\"" CC "}]}]}",
              "resume: byte-identical request");
        conv_reset(&cv);
        t_ok(cv.count == 0 && cv.fend == 0, "resume: reset clears");
    }

    /* conv: large turn (>6K old limit) round-trips; trim by byte budget */
    {
        static conv_t cv;
        static char big[20000], buf[32768], rd[20000];
        int n;
        t_int(conv_init(&cv, "test_conv.dat", 0), 0, "spill: init");
        memset(big, 'A', sizeof big - 1);
        big[sizeof big - 1] = 0;
        conv_push_text(&cv, CONV_ROLE_USER, big);
        conv_push(&cv, CONV_ROLE_ASST, 1,
                  "[{\"type\":\"text\",\"text\":\"r\"}]", -1);
        conv_push_text(&cv, CONV_ROLE_USER, "tail");
        t_int(cv.count, 3, "spill: count");
        t_int(cv.turns[0].len, (long)sizeof big - 1, "spill: big len");
        t_int(conv_read(&cv, 0, rd, (long)sizeof rd), (long)sizeof big - 1,
              "spill: read len");
        t_mem(rd, big, sizeof big - 1, "spill: read bytes");
        n = conv_trim(&cv, 20000);
        t_int(n, 2, "spill: trim drops 2");
        t_ok(cv.count == 1 && cv.turns[0].len == 19999, "spill: trim result");
        t_ok(build(&cv, buf, (int)sizeof buf, NULL, NULL) > 19999,
             "spill: big request builds");
    }
    remove("test_conv.dat");

    /* anth_parse: tool_use response */
    {
        const char *body =
            "{\"type\":\"message\",\"role\":\"assistant\",\"content\":["
            "{\"type\":\"text\",\"text\":\"running ls\"},"
            "{\"type\":\"tool_use\",\"id\":\"tu_1\",\"name\":\"shell\","
            "\"input\":{\"command\":\"ls -la\"}}],"
            "\"stop_reason\":\"tool_use\"}";
        anth_result r;
        t_int(anth_parse(body, (int)strlen(body), &r), 0, "parse: ok");
        t_int(r.is_error, 0, "parse: not error");
        t_str(r.stop_reason, "tool_use", "parse: stop_reason");
        t_int(r.nblocks, 2, "parse: nblocks");
        t_int(r.blocks[0].kind, ANTH_BLK_TEXT, "parse: blk0 text");
        t_mem(r.blocks[0].body, "running ls", 10, "parse: blk0 body");
        t_int(r.blocks[1].kind, ANTH_BLK_TOOL, "parse: blk1 tool");
        t_str(r.blocks[1].id, "tu_1", "parse: blk1 id");
        t_str(r.blocks[1].name, "shell", "parse: blk1 name");
        t_ok(r.blocks[1].body_len > 0 && r.blocks[1].body[0] == '{',
             "parse: blk1 input json");
        t_ok(r.content_json[0] == '['
                 && r.content_json[r.content_json_len - 1] == ']',
             "parse: content_json span");
    }

    /* anth_parse_response: success with escapes + \uXXXX */
    {
        const char *body =
            "{\"id\":\"m\",\"type\":\"message\",\"role\":\"assistant\","
            "\"content\":[{\"type\":\"text\",\"text\":"
            "\"hi\\nthere \\\"q\\\" \\u00e9\"}],"
            "\"model\":\"x\",\"stop_reason\":\"end_turn\"}";
        t_int(
            anth_parse_response(body, (int)strlen(body), out, (int)sizeof out),
            0, "parse_response: success rc");
        t_str(out, "hi\nthere \"q\" \xc3\xa9", "parse_response: text decoded");
    }

    /* anth_parse_response: error */
    {
        const char *body = "{\"type\":\"error\",\"error\":{\"type\":\"bad\","
                           "\"message\":\"nope\"}}";
        t_int(
            anth_parse_response(body, (int)strlen(body), out, (int)sizeof out),
            1, "parse_response: error rc");
        t_str(out, "nope", "parse_response: error message");
    }

    /* anth_parse_models */
    {
        const char *body = "{\"data\":[{\"type\":\"model\",\"id\":\"model-a\","
                           "\"display_name\":\"Model A\",\"created_at\":\"x\"},"
                           "{\"type\":\"model\",\"id\":\"model-b\","
                           "\"display_name\":\"Model B\"}],\"has_more\":false}";
        t_int(anth_parse_models(body, (int)strlen(body), out, (int)sizeof out),
              2, "parse_models: count");
        t_ok(strstr(out, "model-a") && strstr(out, "Model A"),
             "parse_models: row a");
        t_ok(strstr(out, "model-b") && strstr(out, "Model B"),
             "parse_models: row b");
        body = "{\"type\":\"error\",\"error\":{\"message\":\"bad key\"}}";
        t_int(anth_parse_models(body, (int)strlen(body), out, (int)sizeof out),
              -1, "parse_models: error rc");
        t_str(out, "bad key", "parse_models: error msg");
    }

    /* anth_parse_models: 20 models with deep capabilities tree (~1800 jsmn
     * tokens). Regression for MAX_TOK overflow. */
    {
        static char body[32768];
        int i, n, off = 0;
        const char *caps =
            "\"capabilities\":{\"batch\":{\"supported\":true},"
            "\"citations\":{\"supported\":true},"
            "\"code_execution\":{\"supported\":true},"
            "\"context_management\":{\"supported\":true,"
            "\"clear_tool_uses_20250919\":{\"supported\":true},"
            "\"clear_thinking_20251015\":{\"supported\":true},"
            "\"compact_20260112\":{\"supported\":true}},"
            "\"effort\":{\"supported\":true,\"low\":{\"supported\":true},"
            "\"medium\":{\"supported\":true},\"high\":{\"supported\":true},"
            "\"max\":{\"supported\":true}},"
            "\"image_input\":{\"supported\":true},"
            "\"pdf_input\":{\"supported\":true},"
            "\"structured_outputs\":{\"supported\":true},"
            "\"thinking\":{\"supported\":true,\"types\":{"
            "\"enabled\":{\"supported\":true},"
            "\"adaptive\":{\"supported\":true}}}},\"metadata\":{}";
        off += sprintf(body + off, "{\"data\":[");
        for (i = 0; i < 20; i++) {
            off += sprintf(body + off,
                           "%s{\"type\":\"model\",\"id\":\"m-%02d\","
                           "\"display_name\":\"Model %02d\","
                           "\"created_at\":\"2026-01-01T00:00:00Z\","
                           "\"max_input_tokens\":200000,"
                           "\"max_tokens\":64000,%s}",
                           i ? "," : "", i, i, caps);
        }
        off += sprintf(body + off, "],\"has_more\":true,\"first_id\":\"m-00\","
                                   "\"last_id\":\"m-19\"}");
        n = anth_parse_models(body, off, out, (int)sizeof out);
        t_int(n, 20, "parse_models: deep caps count");
        t_ok(strstr(out, "m-00") && strstr(out, "m-19"),
             "parse_models: deep caps rows");
    }
    return t_done();
}
