/*
** parseonly.c
** Compiles testbench.lua repeatedly without ever running it, to isolate
** the compiler's own cost (Part D.3: cost of the C.1 escape analysis
** pass) from anything the script does at runtime. Iteration count is
** argv[1] (default 20000): a single parse is too fast to time reliably
** on its own.
*/

#include <stdio.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define TESTBENCH  "testbench.lua"

int main (int argc, char **argv) {
  int iters = (argc > 1) ? atoi(argv[1]) : 20000;
  const char *file = (argc > 2) ? argv[2] : TESTBENCH;
  int i;
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    fprintf(stderr, "parseonly: cannot create Lua state\n");
    return EXIT_FAILURE;
  }
  luaL_openlibs(L);
  for (i = 0; i < iters; i++) {
    if (luaL_loadfile(L, file) != LUA_OK) {
      fprintf(stderr, "parseonly: %s\n", lua_tostring(L, -1));
      lua_close(L);
      return EXIT_FAILURE;
    }
    lua_pop(L, 1);  /* discard the compiled chunk, never call it */
  }
  lua_close(L);
  return EXIT_SUCCESS;
}
