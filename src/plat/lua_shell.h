/* lua_shell -- shared "Lua as plat_shell()" glue for targets with no native
 * command interpreter. The generic layer owns the persistent lua_State,
 * print() capture, REPL-style exec, the relic.* prelude, and the world-dir
 * (lib/ on package.path). A backend supplies its world path and a callback
 * that registers its hardware tables. */
#ifndef LUA_SHELL_H
#define LUA_SHELL_H

#include "lua.h"

typedef void (*lua_shell_bind_fn)(lua_State *L);

/* world may be NULL (no persistent dir). bind may be NULL. */
void lua_shell_init(const char *world, lua_shell_bind_fn bind);
int lua_shell_exec(const char *code, char *out, int cap);

#endif
