#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main (void) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  lua_setsweepstep(L, 5);
  lua_gc(L, LUA_GCINC);
  if (luaL_dofile(L, "testbench.lua") != LUA_OK) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    return 1;
  }
  printf("sweepstep=5 run completed ok\n");
  lua_setsweepstep(L, -100);
  printf("clamp-to-1 call did not crash\n");
  lua_close(L);
  return 0;
}
