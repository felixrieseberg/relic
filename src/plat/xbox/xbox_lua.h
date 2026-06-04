/* Embedded Lua for the Xbox plat backend; see xbox_lua.c. */
#ifndef XBOX_LUA_H
#define XBOX_LUA_H
void xbox_lua_init(void);
int xbox_lua_exec(const char *code, char *out, int cap);
#endif
