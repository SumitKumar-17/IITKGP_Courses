/*
** sweepbench.c
** Runs testbench.lua under the incremental collector at a sweep-step
** value given as argv[1] (defaults to 20, the stock value). Used for
** Part D.2's sweep-granularity sweep.
*/

#include <stdio.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define TESTBENCH  "testbench.lua"

int main (int argc, char **argv) {
  int step = (argc > 1) ? atoi(argv[1]) : 20;
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    fprintf(stderr, "sweepbench: cannot create Lua state\n");
    return EXIT_FAILURE;
  }
  luaL_openlibs(L);
  lua_setsweepstep(L, step);
  lua_gc(L, LUA_GCINC);
  if (luaL_dofile(L, TESTBENCH) != LUA_OK) {
    fprintf(stderr, "sweepbench: %s\n", lua_tostring(L, -1));
    lua_close(L);
    return EXIT_FAILURE;
  }
  lua_close(L);
  return EXIT_SUCCESS;
}
