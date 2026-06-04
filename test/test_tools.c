#include "t.h"
#include <sys/stat.h>
#include "../src/core/tools.h"

static char out[16384];

static int run(const char *name, const char *in)
{
    return tool_dispatch(name, in, (int)strlen(in), out, (int)sizeof out);
}

static char pv[256];
static int prev(int idx, const char *in)
{
    return tool_preview(idx, in, (int)strlen(in), pv, (int)sizeof pv);
}

int main(void)
{
    t_group("tools");

    /* tools_json() is well-formed enough to be a non-empty array */
    {
        const char *tj = tools_json();
        t_ok(tj != NULL, "tools_json: not NULL");
        t_ok(tj && tj[0] == '[' && tj[strlen(tj) - 1] == ']',
             "tools_json: [..] array");
        t_ok(tj && strstr(tj, tool_name(TOOL_SHELL)) != 0,
             "tools_json: contains shell");
    }

    /* tool_name / tool_index round-trip */
    t_int(tool_index(tool_name(TOOL_SHELL)), TOOL_SHELL,
          "tool_index: shell round-trip");
    t_int(tool_index(tool_name(TOOL_READ)), TOOL_READ,
          "tool_index: read round-trip");
    t_int(tool_index("Nope"), -1, "tool_index: unknown -> -1");

    /* Write -> Read round-trip */
    t_int(
        run("Write",
            "{\"file_path\":\"test/_tmp.txt\",\"content\":\"hello\\nworld\"}"),
        0, "Write: ok");
    t_has(out, "wrote 11 bytes", "Write: byte count");
    t_int(run("Read", "{\"file_path\":\"test/_tmp.txt\"}"), 0, "Read: ok");
    t_str(out, "     1\thello\n     2\tworld\n", "Read: cat -n output");

    /* shell */
    t_int(run(tool_name(TOOL_SHELL), "{\"command\":\"echo retro\"}"), 0,
          "shell: ok");
    t_ok(strncmp(out, "retro", 5) == 0, "shell: stdout captured");
    t_has(out, "\n[cwd: ", "shell: cwd footer");

    /* shell: cwd persists across calls and applies to file tools */
    t_int(run(tool_name(TOOL_SHELL), "{\"command\":\"cd test\"}"), 0,
          "shell: cd ok");
    t_ok(strstr(out, "[exit code") == 0, "shell: cd exit 0");
    t_int(run("Read", "{\"file_path\":\"_tmp.txt\"}"), 0,
          "Read: relative under cwd");
    t_has(out, "hello", "Read: cwd applied");
    t_int(run(tool_name(TOOL_SHELL), "{\"command\":\"false\"}"), 0,
          "shell: false ok");
    t_has(out, "[exit code 1]", "shell: exit code reported");
    t_int(run(tool_name(TOOL_SHELL), "{\"command\":\"cd ..\"}"), 0,
          "shell: cd back");

    /* LS */
    t_int(run("LS", "{\"path\":\"test\"}"), 0, "LS: ok");
    t_has(out, "_tmp.txt", "LS: lists file");

    /* Grep: literal substring, recurses, skips binary */
    {
        FILE *f = fopen("test/_bin.tmp", "wb");
        fputc('A', f);
        fputc(0, f);
        fputs("hello\nworld", f);
        fclose(f);
        t_int(run("Grep", "{\"pattern\":\"hello\",\"path\":\"test\"}"), 0,
              "Grep: ok");
        t_has(out, "test/_tmp.txt:1: hello", "Grep: hit");
        t_ok(strstr(out, "_bin.tmp") == 0, "Grep: binary skipped");
        t_int(run("Grep", "{\"pattern\":\"HTTP/1.1\",\"path\":\"test\"}"), 0,
              "Grep: recurse ok");
        t_has(out, "test/fixtures/resp_200_cl.txt:1:", "Grep: recurse hit");
        t_int(
            run("Grep", "{\"pattern\":\"qz~nope\",\"path\":\"test/fixtures\"}"),
            0, "Grep: miss ok");
        t_has(out, "(no matches", "Grep: miss message");
        remove("test/_bin.tmp");
    }

    /* Glob: wildcard against full relative path */
    t_int(run("Glob", "{\"pattern\":\"test/_tmp.*\",\"path\":\"test\"}"), 0,
          "Glob: ok");
    t_has(out, "test/_tmp.txt\n", "Glob: literal-ish");
    t_int(run("Glob", "{\"pattern\":\"*resp_200*\",\"path\":\"test\"}"), 0,
          "Glob: recurse ok");
    t_has(out, "test/fixtures/resp_200_cl.txt", "Glob: hit cl");
    t_has(out, "test/fixtures/resp_200_chunked.txt", "Glob: hit chunked");
    t_ok(strstr(out, "resp_401") == 0, "Glob: miss excluded");
    t_int(run("Glob", "{\"pattern\":\"*.nope\",\"path\":\"test\"}"), 0,
          "Glob: no match ok");
    t_has(out, "(no matches", "Glob: no match message");

    /* Edit: unique replace, not-found, ambiguous, replace_all */
    t_int(run("Write", "{\"file_path\":\"test/_tmp.txt\","
                       "\"content\":\"foo bar\\nfoo baz\\n\"}"),
          0, "Edit: setup write");
    t_int(run("Edit", "{\"file_path\":\"test/_tmp.txt\","
                      "\"old_string\":\"bar\",\"new_string\":\"BAR!\"}"),
          0, "Edit: unique ok");
    t_has(out, "1 replacement", "Edit: unique count");
    t_int(run("Edit", "{\"file_path\":\"test/_tmp.txt\","
                      "\"old_string\":\"nope\",\"new_string\":\"x\"}"),
          1, "Edit: not found rc");
    t_has(out, "not found", "Edit: not found msg");
    t_int(run("Edit", "{\"file_path\":\"test/_tmp.txt\","
                      "\"old_string\":\"foo\",\"new_string\":\"FOO\"}"),
          1, "Edit: ambiguous rc");
    t_has(out, "appears 2 times", "Edit: ambiguous msg");
    t_int(run("Edit", "{\"file_path\":\"test/_tmp.txt\","
                      "\"old_string\":\"foo\",\"new_string\":\"FOO\","
                      "\"replace_all\":true}"),
          0, "Edit: replace_all ok");
    t_has(out, "2 replacements", "Edit: replace_all count");
    t_int(run("Read", "{\"file_path\":\"test/_tmp.txt\"}"), 0,
          "Edit: readback ok");
    t_str(out, "     1\tFOO BAR!\n     2\tFOO baz\n", "Edit: final content");

    /* tool_preview: compact, width-bounded, falls back on bad input */
    {
        int i, w;
        t_ok(prev(TOOL_SHELL, "{\"command\":\"echo hi\"}") > 0,
             "preview: shell ok");
        t_str(pv, "$ echo hi\n", "preview: shell line");
        t_ok(prev(TOOL_SHELL, "{\"command\":\"line1\\nline2\"}") > 0,
             "preview: shell multi ok");
        t_str(pv, "$ line1 (+1 more line)\n", "preview: shell +N");
        t_ok(prev(TOOL_WRITE,
                  "{\"file_path\":\"a/b.c\",\"content\":\"x\\ny\\nz\"}")
                 > 0,
             "preview: write ok");
        t_str(pv, "a/b.c\n> x (+2 more lines)\n", "preview: write");
        t_ok(prev(TOOL_EDIT, "{\"file_path\":\"f.c\",\"old_string\":\"aaa\","
                             "\"new_string\":\"bbb\",\"replace_all\":true}")
                 > 0,
             "preview: edit ok");
        t_str(pv, "f.c  (all)\n- aaa\n+ bbb\n", "preview: edit");
        /* every line <=76 cols even with long inputs */
        {
            char big[400];
            const char *pre = "{\"command\":\"";
            int n = (int)strlen(pre);
            memcpy(big, pre, (size_t)n);
            for (i = 0; i < 200; i++)
                big[n++] = 'X';
            big[n++] = '"';
            big[n++] = '}';
            t_ok(tool_preview(TOOL_SHELL, big, n, pv, (int)sizeof pv) > 0,
                 "preview: long ok");
            for (i = 0, w = 0; pv[i]; i++)
                if (pv[i] == '\n') {
                    if (w > 76) break;
                    w = 0;
                } else
                    w++;
            t_ok(pv[i] == 0, "preview: lines <= 76 cols");
            t_has(pv, "...", "preview: truncation marker");
        }
        t_int(prev(-1, "{\"command\":\"x\"}"), 0, "preview: bad idx -> 0");
        t_int(prev(TOOL_SHELL, "not json"), 0, "preview: bad json -> 0");
    }

    /* Edit on a CRLF file: old_string sent LF-only must still match */
    {
        FILE *f = fopen("test/_tmp.txt", "wb");
        fputs("one\r\ntwo\r\nthree\r\n", f);
        fclose(f);
        t_int(run("Edit", "{\"file_path\":\"test/_tmp.txt\","
                          "\"old_string\":\"one\\ntwo\",\"new_string\":\"X\"}"),
              0, "Edit CRLF: ok");
        t_has(out, "1 replacement", "Edit CRLF: count");
        t_int(run("Read", "{\"file_path\":\"test/_tmp.txt\"}"), 0,
              "Edit CRLF: read ok");
        t_str(out, "     1\tX\n     2\tthree\n", "Edit CRLF: result");
    }

    /* Read offset/limit */
    t_int(run("Write", "{\"file_path\":\"test/_tmp.txt\","
                       "\"content\":\"a\\nb\\nc\\nd\\ne\\n\"}"),
          0, "Read range: setup");
    t_int(run("Read",
              "{\"file_path\":\"test/_tmp.txt\",\"offset\":3,\"limit\":2}"),
          0, "Read range: ok");
    t_str(out, "     3\tc\n     4\td\n", "Read range: lines 3-4");
    t_int(run("Read", "{\"file_path\":\"test/_tmp.txt\",\"offset\":99}"), 0,
          "Read range: past end ok");
    t_ok(strstr(out, "past end") && strstr(out, "5 lines"),
         "Read range: past end msg");

    /* --- Edit preserves CRLF ----------------------------------------- */
    {
        FILE *f = fopen("test/_tmp.txt", "wb");
        char rb[32];
        int n;
        fwrite("a\r\nb\r\n", 1, 6, f);
        fclose(f);
        t_int(run("Edit", "{\"file_path\":\"test/_tmp.txt\","
                          "\"old_string\":\"a\",\"new_string\":\"X\"}"),
              0, "Edit CRLF: rc 0");
        f = fopen("test/_tmp.txt", "rb");
        n = (int)fread(rb, 1, sizeof rb, f);
        fclose(f);
        t_int(n, 6, "Edit CRLF: length preserved");
        t_mem(rb, "X\r\nb\r\n", 6, "Edit CRLF: CRLF preserved");
    }

    /* --- Edit preserves file mode ------------------------------------ */
    {
        FILE *f = fopen("test/_tmp.txt", "wb");
        struct stat st;
        fputs("a\n", f);
        fclose(f);
        chmod("test/_tmp.txt", 0755);
        t_int(run("Edit", "{\"file_path\":\"test/_tmp.txt\","
                          "\"old_string\":\"a\",\"new_string\":\"b\"}"),
              0, "Edit mode: rc 0");
        t_int(stat("test/_tmp.txt", &st), 0, "Edit mode: stat ok");
        t_int((int)(st.st_mode & 0777), 0755, "Edit mode: 0755 preserved");
    }

    /* --- preview/dispatch sanitize control bytes --------------------- */
    {
        char pv[256];
        const char *in = "{\"command\":\"ls\\u001b[2K\\u0007 -la\"}";
        tool_preview(TOOL_SHELL, in, (int)strlen(in), pv, (int)sizeof pv);
        t_ok(strchr(pv, 0x1b) == NULL && strchr(pv, 0x07) == NULL,
             "preview: ESC/BEL stripped");
        t_ok(strstr(pv, "ls [2K  -la") != NULL, "preview: bytes -> ' '");
    }

    remove("test/_tmp.txt");
    return t_done();
}
