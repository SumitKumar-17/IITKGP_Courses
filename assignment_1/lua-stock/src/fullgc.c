/*
** fullgc.c
** Runs testbench.lua with the collector forced into full,
** stop-the-world mode: automatic incremental/generational stepping is
** switched off and a complete collection cycle is triggered explicitly
** through LUA_GCCOLLECT at a fixed instruction cadence instead.
*/

#include <stdio.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define TESTBENCH  "testbench.lua"

/* instructions executed between forced full collections */
#define COLLECT_EVERY  100000

static void forcecollect (lua_State *L, lua_Debug *ar) {
  (void)ar;
  lua_gc(L, LUA_GCCOLLECT);
}

int main (int argc, char **argv) {
  const char *file = (argc > 1) ? argv[1] : TESTBENCH;  /* Part D.1 sweep */
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    fprintf(stderr, "fullgc: cannot create Lua state\n");
    return EXIT_FAILURE;
  }
  luaL_openlibs(L);
  lua_gc(L, LUA_GCSTOP);
  lua_sethook(L, forcecollect, LUA_MASKCOUNT, COLLECT_EVERY);
  if (luaL_dofile(L, file) != LUA_OK) {
    fprintf(stderr, "fullgc: %s\n", lua_tostring(L, -1));
    lua_close(L);
    return EXIT_FAILURE;
  }
  lua_gc(L, LUA_GCCOLLECT);
  lua_close(L);
  return EXIT_SUCCESS;
}
