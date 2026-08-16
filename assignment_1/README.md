# Part B — Sweep Granularity, Making It Tunable

## What changed

`lgc.c` had `#define GCSWEEPMAX 20`, read by `sweepstep()` to decide how
many objects one incremental sweep step frees before handing control back
to the interpreter. That define is now a file-scope `static int
gcsweepmax` (still defaulting to 20, kept as `GCSWEEPMAX_DEFAULT`), and
`GCSWEEPMAX` is redefined as a macro that expands to `gcsweepmax`. Every
site that used to read `GCSWEEPMAX` (the `sweeplist(...)` call inside
`sweepstep`, and the four `stepresult = GCSWEEPMAX` lines in `singlestep`)
keeps working unchanged, so there's no risk of missing one of them during
the edit.

A new function, declared in `lua.h` right next to `lua_gc` and defined in
`lgc.c` right after `luaC_fullgc`:

```c
LUA_API void lua_setsweepstep (lua_State *L, int n);
```

sets `gcsweepmax` to `n`. Values below 1 get clamped to 1, since a step
that sweeps zero objects per call would never make progress.

## Why a plain static variable

The assignment says the change has to live inside `lgc.c` itself, at file
scope, so it can't be threaded through `global_State`. That also happens
to match what `GCSWEEPMAX` already was: a compile-time constant shared by
the whole process, not something per-`lua_State`. Keeping it a plain
`static int` doesn't change that semantics, it just makes the value
settable at runtime instead of fixed at compile time.

## Usage

```c
lua_State *L = luaL_newstate();
luaL_openlibs(L);
lua_setsweepstep(L, 200);   /* sweep up to 200 objects per step instead of 20 */
lua_gc(L, LUA_GCINC);
luaL_dofile(L, "testbench.lua");
```

Call it once at startup, before the sweeping you want it to affect
happens. It's a global tuning knob, not something meant to change
mid-cycle. The `L` argument is there to match `lua_gc`'s calling
convention (and in case a future version wants to make this per-state),
but the current implementation doesn't touch it since `gcsweepmax` is
process-wide.

## Verification

- `make clean && make linux` rebuilds clean, no new warnings under
  `-Wall -Wextra`.
- `./lua -v` and a plain `print(1)` still work after the patch.
- `sweeptest.c` (a small standalone driver) calls `lua_setsweepstep(L,
  5)`, runs the full testbench under `LUA_GCINC`, then calls
  `lua_setsweepstep(L, -100)` to check the clamp doesn't crash. Clean
  under `valgrind --tool=memcheck --leak-check=full`.
- Benchmarked in Part D.2 at gcsweepmax = 5, 20, 50, 200 on the full
  testbench.

Diff against the original `lgc.c`/`lua.h`: `PART_B_DIFF.patch`.
