/* See lua_shell.h. Platform-agnostic: depends only on plat.h + the Lua C
 * API. Backends link this alongside liblua.a and call lua_shell_init from
 * plat_init, then forward plat_shell() to lua_shell_exec(). */
#include <stdio.h>
#include <string.h>

#include "lualib.h"
#include "lauxlib.h"

#include "lua_shell.h"
#include "plat.h"

static lua_State *g_L;

/* --- print() capture -------------------------------------------------- */
/* lua_shell_exec() points these at the caller's buffer for the duration of
 * one chunk; outside an exec they are NULL/0 and output is dropped. */
static char *g_out;
static int g_cap, g_off;

static void cap_puts(const char *s, size_t n)
{
    if (!g_out || g_off >= g_cap - 1) return;
    if ((int)n > g_cap - 1 - g_off) n = (size_t)(g_cap - 1 - g_off);
    memcpy(g_out + g_off, s, n);
    g_off += (int)n;
    g_out[g_off] = 0;
}

static int l_print(lua_State *L)
{
    int i, n = lua_gettop(L);
    for (i = 1; i <= n; i++) {
        size_t len;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1) cap_puts("\t", 1);
        cap_puts(s, len);
        lua_pop(L, 1);
    }
    cap_puts("\n", 1);
    return 0;
}

/* relic.log: write to the on-screen console (vs print() -> tool result). */
static int l_log(lua_State *L)
{
    fputs(luaL_checkstring(L, 1), stdout);
    fputc('\n', stdout);
    fflush(stdout);
    return 0;
}

/* --- world dir ------------------------------------------------------- */

static void path_join(char *dst, int cap, const char *a, const char *b)
{
    int n = snprintf(dst, (size_t)cap, "%s", a);
    if (n < 0 || n >= cap - 1) return; /* no room for sep+NUL: leave as-is */
    if (n > 0 && dst[n - 1] != plat_dirsep()) dst[n++] = plat_dirsep();
    snprintf(dst + n, (size_t)(cap - n), "%s", b);
}

static const char PRELUDE[] =
    "relic = {_cmds = {}, log = relic.log}\n"
    "function relic.register(name, fn, help)\n"
    "  relic._cmds[name] = {fn = fn, help = help or ''}\n"
    "  _G[name] = fn\n"
    "end\n"
    "function relic.help()\n"
    "  for k, v in pairs(relic._cmds) do print(k .. ' - ' .. v.help) end\n"
    "  if next(relic._cmds) == nil then print('(no registered commands)') end\n"
    "end\n";

void lua_shell_init(const char *world, lua_shell_bind_fn bind)
{
    lua_State *L;
    if (g_L) return;
    L = g_L = luaL_newstate();
    if (!L) return;
    luaL_openlibs(L);
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");
    /* relic.log seeded as a C func before PRELUDE rebuilds the table. */
    lua_newtable(L);
    lua_pushcfunction(L, l_log);
    lua_setfield(L, -2, "log");
    lua_setglobal(L, "relic");
    (void)luaL_dostring(L, PRELUDE);
    if (bind) bind(L);
    if (world) {
        char p[PLAT_PATH_MAX];
        char sep = plat_dirsep();
        plat_mkdir(world);
        path_join(p, (int)sizeof p, world, "lib");
        plat_mkdir(p);
        /* package.path = "<world>/lib/?.lua" using the native separator.
         * No autorun: dofile()ing world-dir scripts at startup turned a
         * Write (auto-approved under acceptEdits) into persistent code-exec
         * on the next launch, breaking the edit/shell permission split. */
        snprintf(p, sizeof p, "%s%clib%c?.lua", world, sep, sep);
        lua_getglobal(L, "package");
        lua_pushstring(L, p);
        lua_setfield(L, -2, "path");
        lua_pop(L, 1);
    }
}

static int traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(non-string error)", 1);
    return 1;
}

int lua_shell_exec(const char *code, char *out, int cap)
{
    lua_State *L = g_L;
    int rc, base;
    if (!L) {
        snprintf(out, (size_t)cap, "lua: not initialised");
        return -1;
    }
    g_out = out;
    g_cap = cap;
    g_off = 0;
    out[0] = 0;
    /* "return <expr>" first so a bare expression evaluates like a REPL. */
    lua_pushcfunction(L, traceback);
    base = lua_gettop(L);
    lua_pushfstring(L, "return %s", code);
    if (luaL_loadbuffer(L, lua_tostring(L, -1), lua_rawlen(L, -1), "=Lua")
        != LUA_OK) {
        lua_pop(L, 1);
        rc = luaL_loadbuffer(L, code, strlen(code), "=Lua");
    } else {
        rc = LUA_OK;
    }
    lua_remove(L, base + 1);
    if (rc == LUA_OK) rc = lua_pcall(L, 0, LUA_MULTRET, base);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        cap_puts("lua error: ", 11);
        cap_puts(err ? err : "?", err ? strlen(err) : 1);
        cap_puts("\n", 1);
    } else if (lua_gettop(L) > base) {
        lua_pushcfunction(L, l_print);
        lua_insert(L, base + 1);
        lua_pcall(L, lua_gettop(L) - base - 1, 0, 0);
    }
    lua_settop(L, base - 1);
    g_out = NULL;
    g_cap = g_off = 0;
    if (out[0] == 0) snprintf(out, (size_t)cap, "(ok, no output)\n");
    return rc == LUA_OK ? 0 : 1;
}
