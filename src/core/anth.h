#ifndef CORE_ANTH_H
#define CORE_ANTH_H

#define ANTH_HOST "api.anthropic.com"
#define ANTH_PATH "/v1/messages"
#define ANTH_MODELS_PATH "/v1/models"
#define ANTH_VERSION "2023-06-01"
#define ANTH_DEFAULT_MODEL "claude-sonnet-4-6"

#define ANTH_MAX_BLOCKS 32

enum { ANTH_BLK_TEXT = 'T', ANTH_BLK_TOOL = 'U' };

/* One block from a parsed response content[] array. */
struct anth_block {
    unsigned char kind; /* ANTH_BLK_* */
    char id[48];        /* tool_use id (kind=='U') */
    char name[68];      /* tool name (kind=='U'); API caps at 64 chars */
    const char *body;   /* raw span into the parsed body (NOT unescaped) --
                           T: JSON-string body of "text"; jsonp_unescape_span()
                           U: JSON span of the "input" object */
    int body_len;
};

typedef struct {
    int is_error; /* 1 -> err_msg set; nblocks=0 */
    char err_msg[256];
    char stop_reason[24]; /* "end_turn" / "tool_use" / ... */
    int nblocks;
    struct anth_block blocks[ANTH_MAX_BLOCKS];
    /* Verbatim span of the "content":[...] array in the original body, for
     * round-tripping the assistant turn back into the next request. */
    const char *content_json;
    int content_json_len;
} anth_result;

/* Parse a /v1/messages response. Returns 0 on parse OK (check is_error),
 * -1 on malformed JSON. body must outlive *r (spans point into it). */
int anth_parse(const char *body, int body_len, anth_result *r);

#ifdef ANTH_TEST_HELPERS
/* Test-only: extract first text block (unescaped, or error.message) into
 * out. 0=ok, 1=api-error, -1=parse-fail. */
int anth_parse_response(const char *body, int body_len, char *out, int cap);
#endif

/* Parse a /v1/models response into a printable list in out:
 *   "  id  -- display_name\n" per model.
 * Returns model count (>=0), or -1 (parse fail / API error; out=message). */
int anth_parse_models(const char *body, int body_len, char *out, int cap);

#endif
