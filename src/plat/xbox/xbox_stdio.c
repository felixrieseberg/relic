/* Override pdclib's _PDCLIB_flushbuffer so stdout/stderr (opened with
 * handle = INVALID_HANDLE_VALUE by nxdk's stdinit) route to our own
 * framebuffer writer. Real file streams keep the WriteFile path. Must
 * link before libpdclib.lib so the symbol shadows pdclib's.
 *
 * Why not nxdk's debugPrint(): it renders every byte -- including 0x08 --
 * via the unscii font, painting a literal "BS" glyph for each backspace
 * the line editor sends. Why not pb_print(): its GPU-push-buffer path
 * only flushes on a full frame swap, which pb_show_debug_screen alone
 * doesn't arrange, so nothing appears on screen.
 *
 * Our own drawChar + cursor does ~30 lines of work and supports a real
 * backspace. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h> /* DbgPrint -- routes to xemu's -serial */
#include "pdclib/_PDCLIB_glue.h"
#include "pdclib/_PDCLIB_int.h"

/* font_unscii_16.h is a bare byte-array fragment; wrap in braces the way
 * hal/debug.c does. FONT_WIDTH/FONT_HEIGHT macros land in this TU too. */
static const unsigned char font_unscii_16[] = {
#include "font_unscii_16.h"
};

int _PDCLIB_w32errno(DWORD werror);

/* --- framebuffer text renderer --------------------------------------- */
/* 640x480x32. Borders match hal/debug.c's "MARGIN" for visual parity. */
#define FB_W 640
#define FB_H 480
#define FB_MARGIN 20
#define GLYPH_W FONT_WIDTH
#define GLYPH_H FONT_HEIGHT
#define TEXT_COLS ((FB_W - 2 * FB_MARGIN) / GLYPH_W)
#define TEXT_ROWS ((FB_H - 2 * FB_MARGIN) / GLYPH_H)

static int g_col, g_row;
static int g_inited;
static unsigned g_fg = 0x00FFFFFFu;

static unsigned int *fb(void) { return (unsigned int *)XVideoGetFB(); }

/* Paint glyph c at (col, row) cell. Non-printables (including blank cells
 * after a backspace) draw as an all-black block, effectively erasing. */
static void cell_draw_fg(int col, int row, unsigned char c, unsigned fg)
{
    const unsigned char *glyph = font_unscii_16 + (int)c * FONT_HEIGHT;
    unsigned int *p =
        fb() + (FB_MARGIN + row * GLYPH_H) * FB_W + (FB_MARGIN + col * GLYPH_W);
    int y, x;
    for (y = 0; y < FONT_HEIGHT; y++) {
        unsigned b = glyph[y];
        for (x = 0; x < FONT_WIDTH; x++)
            p[x] = (b & (0x80 >> x)) ? fg : 0x00000000u;
        p += FB_W;
    }
}

static void fb_clear(void)
{
    unsigned int *p = fb();
    int i;
    for (i = 0; i < FB_W * FB_H; i++)
        p[i] = 0;
    g_col = g_row = 0;
}

static void fb_scroll_one(void)
{
    /* Text-area rows are contiguous scanlines -- copy the whole block in
     * one shot, then zero the newly-uncovered bottom row. Side margins
     * stay whatever they were (black on first boot). */
    unsigned int *p = fb();
    int top = FB_MARGIN * FB_W;
    int bot = (FB_MARGIN + (TEXT_ROWS - 1) * GLYPH_H) * FB_W;
    int text_bytes = (TEXT_ROWS - 1) * GLYPH_H * FB_W * (int)sizeof *p;
    memmove(p + top, p + top + GLYPH_H * FB_W, (size_t)text_bytes);
    memset(p + bot, 0, (size_t)(GLYPH_H * FB_W) * sizeof *p);
}

static void fb_newline(void)
{
    g_col = 0;
    if (++g_row >= TEXT_ROWS) {
        fb_scroll_one();
        g_row = TEXT_ROWS - 1;
    }
}

static void fb_putc(char ch)
{
    if (!g_inited) {
        fb_clear();
        g_inited = 1;
    }
    switch (ch) {
    case '\n': fb_newline(); return;
    case '\r': g_col = 0; return;
    case '\b':
        if (g_col > 0)
            g_col--;
        else if (g_row > 0) {
            g_row--;
            g_col = TEXT_COLS - 1;
        }
        return;
    case '\t': do { fb_putc(' ');
        } while (g_col % 8);
        return;
    default: break;
    }
    if ((unsigned char)ch < 0x20) return; /* drop other control codes */
    cell_draw_fg(g_col, g_row, (unsigned char)ch, g_fg);
    if (++g_col >= TEXT_COLS) fb_newline();
}

/* --- public drawing primitives for xbox_lua.c ------------------------ */

#include "xbox_fb.h"

void xbox_fb_dims(int *w, int *h, int *cols, int *rows)
{
    if (w) *w = FB_W;
    if (h) *h = FB_H;
    if (cols) *cols = TEXT_COLS;
    if (rows) *rows = TEXT_ROWS;
}

void xbox_fb_clear(void) { fb_clear(); }

void xbox_fb_set_fg(unsigned argb) { g_fg = argb ? argb : 0x00FFFFFFu; }

void xbox_fb_pixel(int x, int y, unsigned argb)
{
    if ((unsigned)x < FB_W && (unsigned)y < FB_H) fb()[y * FB_W + x] = argb;
}

void xbox_fb_rect(int x, int y, int w, int h, unsigned argb)
{
    unsigned int *p;
    int yy, xx;
    /* Clamp before any addition: args are model-supplied via Lua and may be
     * INT_MAX, so x+w must not overflow into a negative that defeats the
     * bounds check. */
    if (w <= 0 || h <= 0 || x >= FB_W || y >= FB_H) return;
    if (w > FB_W) w = FB_W;
    if (h > FB_H) h = FB_H;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    if (w <= 0 || h <= 0) return;
    p = fb() + y * FB_W + x;
    for (yy = 0; yy < h; yy++, p += FB_W)
        for (xx = 0; xx < w; xx++)
            p[xx] = argb;
}

void xbox_fb_text(int col, int row, const char *s, unsigned fg)
{
    if (!fg) fg = 0x00FFFFFFu;
    for (; *s && col < TEXT_COLS; s++, col++)
        if (col >= 0 && row >= 0 && row < TEXT_ROWS)
            cell_draw_fg(col, row, (unsigned char)*s, fg);
}

/* Override pdclib's null-stub getenv so the posix-style env fallbacks in
 * core/sess.c (TEMP/TMP/TMPDIR/HOME) resolve to the writable Xbox user
 * partition. When booted from DVD, D:\ is read-only. */
char *getenv(const char *name)
{
    if (name
        && (strcmp(name, "TEMP") == 0 || strcmp(name, "TMP") == 0
            || strcmp(name, "TMPDIR") == 0 || strcmp(name, "HOME") == 0))
        return "E:\\";
    return NULL;
}

/* Replacement for nxdk's pdclib _PDCLIB_open. The upstream "w+"/"a+" cases
 * OR together CreateFileA disposition constants (e.g. CREATE_ALWAYS |
 * TRUNCATE_EXISTING = 7) which the kernel rejects -- breaking fopen("",
 * "w+b"). conv.c's session-store path needs read-write on fresh files, so
 * we ship a corrected open instead of paying an upstream patch. Mirrors
 * the upstream structure; only the dispositions changed. */
_PDCLIB_fd_t _PDCLIB_open(const char *const filename, unsigned int mode)
{
    int access_flags = 0;
    int create_flags = 0;
    HANDLE handle;

    switch (
        mode
        & (_PDCLIB_FREAD | _PDCLIB_FWRITE | _PDCLIB_FAPPEND | _PDCLIB_FRW)) {
    case _PDCLIB_FREAD: /* "r"  */
        access_flags = GENERIC_READ;
        create_flags = OPEN_EXISTING;
        break;
    case _PDCLIB_FWRITE: /* "w"  */
        access_flags = GENERIC_WRITE;
        create_flags = CREATE_ALWAYS;
        break;
    case _PDCLIB_FWRITE | _PDCLIB_FAPPEND: /* "a"  */
        access_flags = GENERIC_WRITE;
        create_flags = OPEN_ALWAYS;
        break;
    case _PDCLIB_FREAD | _PDCLIB_FRW: /* "r+" */
        access_flags = GENERIC_READ | GENERIC_WRITE;
        create_flags = OPEN_EXISTING;
        break;
    case _PDCLIB_FWRITE | _PDCLIB_FRW: /* "w+" */
        access_flags = GENERIC_READ | GENERIC_WRITE;
        create_flags = CREATE_ALWAYS; /* CREATE_ALWAYS alone truncates */
        break;
    case _PDCLIB_FAPPEND | _PDCLIB_FRW: /* "a+" */
        access_flags = GENERIC_READ | GENERIC_WRITE;
        create_flags = OPEN_ALWAYS;
        break;
    default: return INVALID_HANDLE_VALUE;
    }
    handle =
        CreateFileA(filename, access_flags, FILE_SHARE_READ | FILE_SHARE_DELETE,
                    NULL, create_flags, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
        if (mode & _PDCLIB_FAPPEND) {
            LARGE_INTEGER pos;
            pos.QuadPart = 0;
            if (!SetFilePointerEx(handle, pos, &pos, FILE_END)) {
                *_PDCLIB_errno_func() = _PDCLIB_w32errno(GetLastError());
                CloseHandle(handle);
                return INVALID_HANDLE_VALUE;
            }
        }
        return handle;
    }
    *_PDCLIB_errno_func() = _PDCLIB_w32errno(GetLastError());
    return INVALID_HANDLE_VALUE;
}

int _PDCLIB_flushbuffer(struct _PDCLIB_file_t *stream)
{
    _PDCLIB_size_t written = 0;

    /* stdout/stderr path: paint via fb_putc (supports \b) and mirror to
     * DbgPrint for xemu -serial capture. DbgPrint needs NUL-terminated
     * input; chunk in BUFSIZ pieces so arbitrarily-long writes flush. */
    if (stream->handle == INVALID_HANDLE_VALUE) {
        _PDCLIB_size_t i, off, n = stream->bufidx;
        char chunk[BUFSIZ + 1];
        for (i = 0; i < n; i++)
            fb_putc(stream->buffer[i]);
        for (off = 0; off < n; off += BUFSIZ) {
            _PDCLIB_size_t m = n - off > BUFSIZ ? BUFSIZ : n - off;
            memcpy(chunk, stream->buffer + off, m);
            chunk[m] = 0;
            DbgPrint("%s", chunk);
        }
        stream->bufidx = 0;
        return 0;
    }

    for (;;) {
        DWORD amount_written;
        if (!WriteFile(stream->handle, stream->buffer + written,
                       stream->bufidx - written, &amount_written, NULL)) {
            *_PDCLIB_errno_func() = _PDCLIB_w32errno(GetLastError());
            stream->status |= _PDCLIB_ERRORFLAG;
            stream->bufidx -= written;
            memmove(stream->buffer, stream->buffer + written, stream->bufidx);
            return EOF;
        }
        written += (_PDCLIB_size_t)amount_written;
        stream->pos.offset += amount_written;
        if (written == stream->bufidx) {
            stream->bufidx = 0;
            return 0;
        }
    }
}
