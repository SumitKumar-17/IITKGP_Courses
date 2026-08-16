/*
** generationalgc.c
** Runs testbench.lua with Lua's generational collector, which
** collects short-lived objects in cheap minor cycles and falls back
** to a major cycle only when enough of them are surviving.
*/

#include <stdio.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define TESTBENCH  "testbench.lua"

int main (int argc, char **argv) {
  const char *file = (argc > 1) ? argv[1] : TESTBENCH;  /* Part D.1 sweep */
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    fprintf(stderr, "generationalgc: cannot create Lua state\n");
    return EXIT_FAILURE;
  }
  luaL_openlibs(L);
  lua_gc(L, LUA_GCGEN);
  if (luaL_dofile(L, file) != LUA_OK) {
    fprintf(stderr, "generationalgc: %s\n", lua_tostring(L, -1));
    lua_close(L);
    return EXIT_FAILURE;
  }
  lua_close(L);
  return EXIT_SUCCESS;
}
