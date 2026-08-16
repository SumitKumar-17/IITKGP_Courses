/*
** nogc.c
** Runs testbench.lua with the collector stopped entirely
** (LUA_GCSTOP), for Part D.1's perf baseline: what the workload costs
** with no collector running at all.
*/

#include <stdio.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define TESTBENCH  "testbench.lua"

int main (int argc, char **argv) {
  const char *file = (argc > 1) ? argv[1] : TESTBENCH;
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    fprintf(stderr, "nogc: cannot create Lua state\n");
    return EXIT_FAILURE;
  }
  luaL_openlibs(L);
  lua_gc(L, LUA_GCSTOP);
  if (luaL_dofile(L, file) != LUA_OK) {
    fprintf(stderr, "nogc: %s\n", lua_tostring(L, -1));
    lua_close(L);
    return EXIT_FAILURE;
  }
  lua_close(L);
  return EXIT_SUCCESS;
}
