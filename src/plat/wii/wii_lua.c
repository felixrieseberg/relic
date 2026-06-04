/* wii_lua.c -- Wii-specific Lua bindings registered into the shared
 * lua_shell. The generic interpreter, print() capture, world dir and
 * relic.* live in src/plat/lua_shell.c; this file only adds the libogc
 * `wii` table. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/si.h>

#include "lauxlib.h"

#include "wii_lua.h"
#include "wii_kbd.h"
#include "wii_fb.h"
#include "../lua_shell.h"

extern void entropy_flush(void);
extern const char *wii_net_ip(void);

static void preexit(void)
{
    fflush(NULL);
    entropy_flush();
}

/* wii.mem() -> mem1_free, mem2_free (bytes) */
static int l_mem(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)SYS_GetArena1Size());
    lua_pushinteger(L, (lua_Integer)SYS_GetArena2Size());
    return 2;
}

static int l_ip(lua_State *L)
{
    lua_pushstring(L, wii_net_ip());
    return 1;
}

static int l_ticks(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)ticks_to_millisecs(gettime()));
    return 1;
}

static int l_sleep(lua_State *L)
{
    usleep((useconds_t)luaL_checkinteger(L, 1) * 1000);
    return 0;
}

/* wii.getkey([timeout_ms]) -> int|nil */
static int l_getkey(lua_State *L)
{
    int ms = (int)luaL_optinteger(L, 1, 0);
    u64 t0 = gettime();
    int k;
    for (;;) {
        k = wii_kbd_get();
        if (k >= 0) {
            lua_pushinteger(L, k);
            return 1;
        }
        if (ms >= 0 && (int)ticks_to_millisecs(gettime() - t0) >= ms) break;
        VIDEO_WaitVSync();
    }
    lua_pushnil(L);
    return 1;
}

static int l_kbd(lua_State *L)
{
    lua_pushboolean(L, wii_kbd_present());
    return 1;
}

/* wii.si(chan) -> "keyboard"|"controller"|"none"|type-hex for the device on
 * GameCube port chan (0-3). */
static int l_si(lua_State *L)
{
    int ch = (int)luaL_checkinteger(L, 1);
    u32 t;
    if (ch < 0 || ch > 3) return luaL_error(L, "chan 0-3");
    t = SI_DecodeType(SI_GetType(ch));
    switch (t) {
    case SI_GC_KEYBOARD: lua_pushstring(L, "keyboard"); break;
    case SI_GC_CONTROLLER: lua_pushstring(L, "controller"); break;
    case SI_GC_WAVEBIRD: lua_pushstring(L, "wavebird"); break;
    case SI_ERROR_NO_RESPONSE:
    case 0: lua_pushstring(L, "none"); break;
    default: lua_pushfstring(L, "0x%08x", (unsigned)t); break;
    }
    return 1;
}

static int l_reset(lua_State *L)
{
    (void)L;
    preexit();
    SYS_ResetSystem(SYS_RESTART, 0, 0);
    return 0;
}
static int l_poweroff(lua_State *L)
{
    (void)L;
    preexit();
    SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    return 0;
}
static int l_menu(lua_State *L)
{
    (void)L;
    preexit();
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    return 0;
}

/* --- fb.* ------------------------------------------------------------- */

static int l_fb_dims(lua_State *L)
{
    int w, h, c, r;
    wii_fb_dims(&w, &h, &c, &r);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    lua_pushinteger(L, c);
    lua_pushinteger(L, r);
    return 4;
}
static int l_fb_clear(lua_State *L)
{
    wii_fb_clear((unsigned)luaL_optinteger(L, 1, 0));
    return 0;
}
static int l_fb_pixel(lua_State *L)
{
    wii_fb_pixel((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                 (unsigned)luaL_checkinteger(L, 3));
    return 0;
}
static int l_fb_rect(lua_State *L)
{
    wii_fb_rect((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
                (unsigned)luaL_checkinteger(L, 5));
    return 0;
}

static const luaL_Reg fblib[] = {{"dims", l_fb_dims},
                                 {"clear", l_fb_clear},
                                 {"pixel", l_fb_pixel},
                                 {"rect", l_fb_rect},
                                 {NULL, NULL}};

static const luaL_Reg wiilib[] = {
    {"mem", l_mem},     {"ip", l_ip},         {"ticks", l_ticks},
    {"sleep", l_sleep}, {"getkey", l_getkey}, {"kbd", l_kbd},
    {"si", l_si},       {"reset", l_reset},   {"poweroff", l_poweroff},
    {"menu", l_menu},   {NULL, NULL}};

static void wii_bind(lua_State *L)
{
    luaL_newlib(L, wiilib);
    lua_setglobal(L, "wii");
    luaL_newlib(L, fblib);
    lua_setglobal(L, "fb");
}

void wii_lua_init(void) { lua_shell_init(WII_APP_DIR, wii_bind); }
