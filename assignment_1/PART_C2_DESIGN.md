# Part C.2 — Frame-Arena Promotion, Design Note

## The mechanism

`CallInfo`'s Lua-function fields (`lstate.h`) get one new pointer:
`ArenaBlock *arena`, a simple bump allocator (`next`, `used`, `size`) that
chains to a new block when it runs out of room. `luaC_newarenaobj`
(`lgc.c`) bump-allocates a slot out of it and initializes a completely
ordinary `GCObject` header in it — same `tt`/`marked` fields, same white
color a freshly `luaC_newobj`'d object would get — except `next` is left
`NULL`. That's the entire mechanism for "bypassing the heap": every other
GCObject's `next` links it into `allgc` (or `finobj`, `tobefnz`, ...);
ours doesn't link into anything, so `sweeplist`/`sweep2old`/every sweep
function, which only ever walks those `next` chains, structurally cannot
reach it. `luaH_new_at` (`ltable.c`) then does the exact same field
initialization `luaH_new` does, just onto that pre-built header instead
of allocating one itself. `OP_NEWTABLE` (`lvm.c`) picks between the two
with one check — `p->arenapc[pc]`, baked in by the Part C.1 analyzer —
made once, at table-creation time, not on any later field access.

When the owning frame returns, `luaD_poscall` (`ldo.c`) calls
`luaC_freearena`, which drops the whole block chain at once: no
per-object free, no sweep. One block is kept per Lua state as a spare
(`global_State.arenafree`) so the common case (one table-ish frame
calling another) doesn't pay a malloc/free round trip. An error
longjmp'ing past `luaD_poscall` entirely (skipping straight from deep in
a call to a `pcall` boundary) would leak those blocks otherwise, so
`luaD_pcall`/`luaD_closeprotected` walk the frames being discarded and
free their arenas before dropping them (`freearenas` in `ldo.c`).

## The bug this design has to get right, and how it was found

The first version of this passed every quick manual test and still
corrupted memory the first time it was checked under conditions that
actually forced a second collection cycle. The test that caught it
(`arenatest.lua`) is deliberately not a quick smoke test: it stores a
long string and a nested table into a promoted local, forces
`collectgarbage("collect")` *while the frame is still alive*, then reads
both back. Under `valgrind --track-origins=yes`, the nested table came
back pointing at freed memory.

The mechanism: sweeping isn't just "free the dead ones" — for every
*survivor*, it also resets that object's color back to white
(`sweeplist`'s `curr->marked = (marked & ~maskgcbits) | white | G_NEW`).
That reset is what tells the *next* cycle's mark phase "this one hasn't
been re-examined yet." An arena table never sweeps, so it never gets
that reset: once marked black in its first cycle, it stays black
forever, and a plain `if (iswhite(t)) ...` check — exactly what the
stack scan uses to decide whether to re-traverse an object — will never
fire for it again. A write barrier can catch a mutation that happens
*while* a cycle is still processing that object's `grayagain` entry, but
one that happens between cycles can have its pending re-visit wiped by
`cleargraylists` before any cycle gets around to draining it — which is
exactly what was happening here: the table was marked once, while still
empty, by an incremental step that ran moments after it was created,
and the write that gave it real contents happened after that cycle had
already finished.

The fix (`restartcollection`, `lgc.c`) is to do manually, once per
cycle, exactly what sweep does for every other survivor: reset arena
objects back to white right when a new cycle starts. Since the only
kind of object ever placed in the arena is a `Table`, all of them are
the same size, so every live arena block can be walked in fixed strides
with no per-object bookkeeping — just the current thread's `CallInfo`
chain (`L->ci` down through `->previous`), and each frame's block chain.
With that reset in place, the stack scan finds these tables genuinely
white again every cycle, traverses them fresh, and the *existing*,
untouched write-barrier machinery is sufficient for anything that
changes mid-cycle — no changes to the barrier itself ended up being
necessary once the actual root cause was fixed.

## A second bug: the fast return paths never called `luaC_freearena` at all

The first bug was about correctness (a live object read back corrupted).
This second one showed up as a plain memory leak, caught while
benchmarking Part D.3's promotion-benefit numbers: a function that does
nothing but build a table, use it, and `return` a single value — the
single most ordinary shape of function there is — leaked one whole
arena block (about 4KB) on every single call, confirmed with
`valgrind --leak-check=full` down to `luaC_newarenaobj` as the
allocation site.

The cause: `luaD_poscall` is not actually where most function returns go.
`OP_RETURN0` and `OP_RETURN1` (returning zero or exactly one value — the
overwhelming common case, exactly what every function in this project's
own test scripts does) have their own inlined fast path in
`luaV_execute` for when no debug hook is active, which duplicates
`luaD_poscall`'s bookkeeping (`L->ci = ci->previous`, moving the result)
directly, and never calls `luaD_poscall` at all. My original
`luaC_freearena` hook only lived inside the real function, so every
return through either fast path silently skipped it. `luaD_pretailcall`
has the same shape of gap for a different reason: a tail call reuses the
*same* `CallInfo` for the new function instead of pushing one, so the
outgoing function's arena has to be freed right there, before the reuse,
or it's simply overwritten and lost.

Both are now handled: `luaC_freearena` is called from the non-hooked
branches of `OP_RETURN0`/`OP_RETURN1` and from `luaD_pretailcall` before
a `CallInfo` is repurposed, in addition to the original call in
`luaD_poscall` (which still handles the general `OP_RETURN` case and the
hooked/debug path for the other two). A related, smaller gap surfaced at
the same time: a promoted `Table`'s own struct lives in the arena, but
its array and hash parts are ordinary heap blocks it only points to
(grown by the exact same `luaH_resize`/`luaH_newkey` every table uses),
and discarding the arena block never touched those. `luaH_freecontents`
(`ltable.c`) — `luaH_free` minus the final free of the struct itself —
is now called for every table in a block before that block is dropped or
recycled.

The honest note here: earlier verification of this same code had
reported clean under `valgrind --leak-check=full`, which was wrong — not
a regression introduced later, but a gap in how thoroughly that pass was
checked. Both leaks are fixed now, and every scenario that originally
found them (including a direct before/after re-test of each one) is
clean on re-verification (see Verification).

## Generational mode is explicitly out of scope, and that's by design

`youngcollection` (a minor cycle) does not do a fresh stack scan at all
— its whole efficiency argument is that it doesn't need to, because
every reachable old object is supposed to be represented in
`old1`/`reallyold`/the "touched" remembered-set bookkeeping, so only
young objects and the (small) remembered set need visiting. An arena
table structurally can't be represented there (that bookkeeping is
sweep-adjacent, and arena tables skip sweep), so it would be invisible
to a minor collection's roots entirely, with no equivalent of the
per-cycle reset above to fall back on.

Rather than fight that, `OP_NEWTABLE` only takes the arena path when
`G(L)->gckind == KGC_INC` (checked at runtime, since a program can
switch collectors); under generational mode every candidate — even ones
the analysis proved non-escaping — bails to the exact same `luaH_new`
path as before. This is the same "bail to heap when it doesn't fit the
arena's assumptions" escape hatch the assignment already asks for
elsewhere, applied to a case the assignment's own text doesn't
anticipate: it isn't the table that fails to fit the arena's
assumptions here, it's the *collector configuration*.

## A real cost, found while benchmarking: long-lived frames

The per-cycle reset that fixes the first bug isn't free — it walks every
live frame's arena blocks once per GC cycle, so its cost scales with how
much arena memory a frame is holding *at that moment*. For a frame that
returns promptly, that's nearly nothing, and it stays nearly nothing all
the way through the run. For a single frame that keeps promoting more
tables into its own arena without ever returning — one function call,
running for the program's whole lifetime, continuously accumulating more
live promoted objects — the same walk revisits an ever-larger set on
every cycle, for as long as that frame stays alive. Part D.3
(`PART_D_REPORT.md`) measures this directly: a workload built to have
exactly that shape runs about 14x more instructions and 80x slower
wall-clock with promotion enabled than without it, and the callgrind
profile attributes over 93% of the run to this reset. A workload where
each promoted table's frame returns quickly shows the intended result
instead — measurably fewer instructions and a lower GC share. Both
numbers are real and both are reported, since which one a given program
gets depends entirely on whether it matches the second shape (one
frame, unboundedly many promotions, never returning) or the first (many
short-lived frames) — and that's a property of the *caller's* code, not
something the escape analyzer or the arena can detect or guard against
on their own.

## Verification

- `esctest.lua`'s 14 functions, `arenatest.lua` (string + nested-table
  survival across a forced mid-frame collection, 2000-iteration arena
  reuse/chaining, and a heap-path escaping table checked post-GC), a
  200-deep recursion test, and a 500-iteration `pcall`/`error` unwind
  test (exercising `freearenas`) — all clean under
  `valgrind --tool=memcheck --leak-check=full`, and clean when run 20
  times in a row.
- The real `testbench.lua`, under the incremental driver, clean under
  full memcheck at its actual 2,000,000-allocation scale.
- A generational-mode variant of the survival test, confirming the
  `gckind` guard correctly falls back to the heap path there and
  produces correct results (not just "doesn't crash" — the values read
  back are checked).
- The repros that exposed the fast-return leak (`local s = f(); total =
  total + s` vs. `total = total + f()`, and Part D's own
  `promobench2.lua` — all functions returning a single arithmetic value
  through `OP_RETURN1`) were confirmed leaking pre-fix and are clean
  post-fix, on otherwise identical builds.

Diff against the originals: `PART_C2_DIFF.patch`.
