#ifndef CORE_TOOLS_H
#define CORE_TOOLS_H

/* Tool indices. Order matches tool_name() and tools_json(). */
enum {
    TOOL_SHELL,
    TOOL_READ,
    TOOL_WRITE,
    TOOL_LIST,
    TOOL_GREP,
    TOOL_EDIT,
    TOOL_GLOB,
    NTOOLS
};

/* Display/API name of tool idx (TOOL_SHELL is platform-dependent). */
const char *tool_name(int idx);
/* JSON array of tool schemas, sent in every request. Built once. */
const char *tools_json(void);

/* TOOL_* index of name, or -1. */
int tool_index(const char *name);

/* Execute tool `name` with JSON object `input` (len bytes, raw span).
 * Writes result text to out (cap), NUL-terminated. Returns 0 on success,
 * 1 on tool-level error (out has the message; includes unknown name). */
int tool_dispatch(const char *name, const char *input, int len, char *out,
                  int cap);

/* Compact (<=3 lines, <=76 cols/line) human preview of tool input for the
 * permission prompt. Returns bytes written to out[cap], or 0 if no preview
 * is available (caller should fall back to raw JSON). */
int tool_preview(int idx, const char *input, int ilen, char *out, int cap);

/* Runtime self-test (/test tools): shell echo, Write -> Read -> Edit. */
int tools_selftest(void);

#endif
