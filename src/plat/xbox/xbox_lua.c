/* xbox_lua.c -- Xbox-specific Lua bindings registered into the shared
 * lua_shell. The generic interpreter, print() capture, world dir and
 * relic.* live in src/plat/lua_shell.c; this file only adds the SMC/HAL/
 * kernel `xbox` table and the framebuffer `fb` table. */
#include <stdio.h>
#include <string.h>

#include <hal/led.h>
#include <hal/xbox.h> /* XReboot / XLaunchXBE */
#include <windows.h>  /* Sleep */
#include <xboxkrnl/xboxkrnl.h>

#include "lauxlib.h"

#include "xbox_lua.h"
#include "xbox_fb.h"
#include "xbox_kbd.h"
#include "../lua_shell.h"

#define SMC_SLAVE 0x20

/* --- xbox.* ----------------------------------------------------------- */

static XLEDColor led_of(lua_State *L, int idx)
{
    static const char *names[] = {"off", "green", "red", "orange", NULL};
    static const XLEDColor vals[] = {XLED_OFF, XLED_GREEN, XLED_RED,
                                     XLED_ORANGE};
    return vals[luaL_checkoption(L, idx, "off", names)];
}

/* xbox.led([t1,t2,t3,t4]) -- four-phase front-LED pattern; no args resets
 * to system control. */
static int l_led(lua_State *L)
{
    if (lua_gettop(L) == 0)
        XResetLED();
    else
        XSetCustomLED(led_of(L, 1), led_of(L, 2), led_of(L, 3), led_of(L, 4));
    return 0;
}

/* xbox.tray([open]) -- true ejects, false closes, no arg just reads.
 * Returns the SMC tray-state byte. */
static int l_tray(lua_State *L)
{
    ULONG st = 0;
    if (!lua_isnoneornil(L, 1))
        HalWriteSMBusValue(SMC_SLAVE, 0x0C, FALSE, lua_toboolean(L, 1) ? 0 : 1);
    HalReadSMCTrayState(&st, NULL);
    lua_pushinteger(L, (lua_Integer)st);
    return 1;
}

/* xbox.temp() -> cpu_degC, board_degC */
static int l_temp(lua_State *L)
{
    ULONG cpu = 0, mb = 0;
    HalReadSMBusValue(SMC_SLAVE, 0x09, FALSE, &cpu);
    HalReadSMBusValue(SMC_SLAVE, 0x0A, FALSE, &mb);
    lua_pushinteger(L, (lua_Integer)cpu);
    lua_pushinteger(L, (lua_Integer)mb);
    return 2;
}

/* xbox.fan([speed]) -- speed 0..50 sets manual; nil reverts to auto. */
static int l_fan(lua_State *L)
{
    ULONG cur = 0;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        int sp = (int)luaL_checkinteger(L, 1);
        if (sp < 0) sp = 0;
        if (sp > 50) sp = 50;
        HalWriteSMBusValue(SMC_SLAVE, 0x06, FALSE, (ULONG)sp);
        HalWriteSMBusValue(SMC_SLAVE, 0x05, FALSE, 1);
    } else if (lua_gettop(L) >= 1) {
        HalWriteSMBusValue(SMC_SLAVE, 0x05, FALSE, 0);
    }
    HalReadSMBusValue(SMC_SLAVE, 0x10, FALSE, &cur);
    lua_pushinteger(L, (lua_Integer)cur);
    return 1;
}

/* xbox.mem() -> total_bytes, avail_bytes */
static int l_mem(lua_State *L)
{
    MM_STATISTICS ms;
    ms.Length = sizeof ms;
    MmQueryStatistics(&ms);
    lua_pushinteger(L, (lua_Integer)ms.TotalPhysicalPages * 4096);
    lua_pushinteger(L, (lua_Integer)ms.AvailablePages * 4096);
    return 2;
}

/* xbox.eeprom() -> { mac=, serial_tail=, av_region=, game_region= }
 * HDD key / online key are deliberately not queried -- those are per-box
 * secrets and the tool result transits the API. */
static int l_eeprom(lua_State *L)
{
    unsigned char buf[16];
    ULONG type, n, r;
    lua_newtable(L);
    n = 0;
    if (NT_SUCCESS(ExQueryNonVolatileSetting(XC_FACTORY_ETHERNET_ADDR, &type,
                                             buf, sizeof buf, &n))
        && n >= 6) {
        char mac[13];
        snprintf(mac, sizeof mac, "%02x%02x%02x%02x%02x%02x", buf[0], buf[1],
                 buf[2], buf[3], buf[4], buf[5]);
        lua_pushstring(L, mac);
        lua_setfield(L, -2, "mac");
    }
    n = 0;
    if (NT_SUCCESS(ExQueryNonVolatileSetting(XC_FACTORY_SERIAL_NUMBER, &type,
                                             buf, sizeof buf, &n))
        && n >= 4) {
        lua_pushlstring(L, (char *)buf + n - 4, 4);
        lua_setfield(L, -2, "serial_tail");
    }
    if (NT_SUCCESS(ExQueryNonVolatileSetting(XC_FACTORY_AV_REGION, &type, &r,
                                             sizeof r, &n))) {
        lua_pushinteger(L, (lua_Integer)r);
        lua_setfield(L, -2, "av_region");
    }
    if (NT_SUCCESS(ExQueryNonVolatileSetting(XC_FACTORY_GAME_REGION, &type, &r,
                                             sizeof r, &n))) {
        lua_pushinteger(L, (lua_Integer)r);
        lua_setfield(L, -2, "game_region");
    }
    return 1;
}

/* Reboot/poweroff/launch never return, so atexit handlers (which normally
 * persist the forward-secured RNG seed) don't run. Flush explicitly. */
extern void entropy_flush(void);
static void preexit(void)
{
    fflush(NULL);
    entropy_flush();
}

static int l_reboot(lua_State *L)
{
    (void)L;
    preexit();
    XReboot();
    return 0;
}
static int l_poweroff(lua_State *L)
{
    (void)L;
    preexit();
    HalInitiateShutdown();
    return 0;
}

static int l_launch(lua_State *L)
{
    const char *p = luaL_checkstring(L, 1);
    preexit();
    XLaunchXBE(p);
    return luaL_error(L, "XLaunchXBE returned (path not found?)");
}

static int l_sleep(lua_State *L)
{
    Sleep((DWORD)luaL_checkinteger(L, 1));
    return 0;
}

static int l_ticks(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)KeTickCount);
    return 1;
}

/* xbox.getkey([timeout_ms]) -> int|nil. Pumps the USB HID poll loop so a
 * Lua program can run its own event loop without the REPL. timeout_ms < 0
 * blocks forever, 0 (default) is a single non-blocking poll. */
static int l_getkey(lua_State *L)
{
    int ms = (int)luaL_optinteger(L, 1, 0);
    DWORD t0 = KeTickCount;
    int k;
    for (;;) {
        (void)xbox_kbd_poll();
        k = xbox_kbd_get();
        if (k >= 0) {
            lua_pushinteger(L, k);
            return 1;
        }
        if (ms >= 0 && (int)(KeTickCount - t0) >= ms) break;
        Sleep(2);
    }
    lua_pushnil(L);
    return 1;
}

static int l_kbd(lua_State *L)
{
    lua_pushboolean(L, xbox_kbd_present());
    return 1;
}

static const luaL_Reg xboxlib[] = {
    {"led", l_led},       {"tray", l_tray},
    {"temp", l_temp},     {"fan", l_fan},
    {"mem", l_mem},       {"eeprom", l_eeprom},
    {"reboot", l_reboot}, {"poweroff", l_poweroff},
    {"launch", l_launch}, {"sleep", l_sleep},
    {"ticks", l_ticks},   {"getkey", l_getkey},
    {"kbd", l_kbd},       {NULL, NULL}};

/* --- fb.* ------------------------------------------------------------- */

static int l_fb_dims(lua_State *L)
{
    int w, h, c, r;
    xbox_fb_dims(&w, &h, &c, &r);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    lua_pushinteger(L, c);
    lua_pushinteger(L, r);
    return 4;
}
static int l_fb_clear(lua_State *L)
{
    (void)L;
    xbox_fb_clear();
    return 0;
}
static int l_fb_pixel(lua_State *L)
{
    xbox_fb_pixel((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                  (unsigned)luaL_checkinteger(L, 3));
    return 0;
}
static int l_fb_rect(lua_State *L)
{
    xbox_fb_rect((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                 (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
                 (unsigned)luaL_checkinteger(L, 5));
    return 0;
}
static int l_fb_text(lua_State *L)
{
    xbox_fb_text((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                 luaL_checkstring(L, 3), (unsigned)luaL_optinteger(L, 4, 0));
    return 0;
}

static const luaL_Reg fblib[] = {{"dims", l_fb_dims},   {"clear", l_fb_clear},
                                 {"pixel", l_fb_pixel}, {"rect", l_fb_rect},
                                 {"text", l_fb_text},   {NULL, NULL}};

/* --- registration ----------------------------------------------------- */

static void xbox_bind(lua_State *L)
{
    luaL_newlib(L, xboxlib);
    lua_setglobal(L, "xbox");
    luaL_newlib(L, fblib);
    lua_setglobal(L, "fb");
}

void xbox_lua_init(void)
{
    lua_shell_init("E:\\relic", xbox_bind);
    DbgPrint("[xbox_lua] %s ready, world=E:\\relic\n", LUA_RELEASE);
}

int xbox_lua_exec(const char *code, char *out, int cap)
{
    return lua_shell_exec(code, out, cap);
}
