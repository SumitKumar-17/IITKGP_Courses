# Part A: Understanding Lua's GC

Sumit Kumar (22CS30056), Jatin Mahawar (22CS30032)

## 1. Method

`fullgc.c`, `incrementalgc.c` and `generationalgc.c` each open a fresh Lua
state, pin a GC policy with `lua_gc`, then run `testbench.lua`. With the
file's own parameters (`m = n = 1000`, `survive_every = 100`) that's
1,000,000 outer-loop iterations, two table allocations per iteration (the
outer `obj` and the nested `{i, j}`), so 2,000,000 tables total, of which
about 10,000 end up in `survivors`.

`incrementalgc.c` and `generationalgc.c` are the easy two: `lua_gc(L,
LUA_GCINC)` and `lua_gc(L, LUA_GCGEN)` are real persistent collector
modes, so calling one before loading the file is enough.

`fullgc.c` needed more thought, because there is no persistent "full" mode
to select in the API. `LUA_GCCOLLECT` is a one-shot forced cycle
(`lapi.c` just does `case LUA_GCCOLLECT: luaC_fullgc(L, 0)`), not a
setting you turn on. To get "full GC" to mean something for the whole
run rather than a single call, we stop automatic stepping with
`LUA_GCSTOP` and install a count hook (`lua_sethook`,
`LUA_MASKCOUNT`, every 100,000 VM instructions) that calls `lua_gc(L,
LUA_GCCOLLECT)` on each tick. That gives a real stop-the-world
collector: no interleaving, periodic complete mark-and-sweep cycles.
Worth flagging honestly: the hook itself has a cost. Forcing
`luaG_traceexec`'s instruction-count check on every single VM instruction
shows up as roughly 9.8% of fullgc's total instructions, and that's
instrumentation overhead from *driving* the collector this way, not GC
work. It's kept separate rather than folded into the GC numbers below.

All three were built with `-g -O2` against the unmodified `liblua.a`,
profiled with `valgrind --tool=callgrind`, and read with
`callgrind_annotate` and `gprof2dot` (raw callgrind output and the call
graph PNGs are in `results/`).

Wherever this report says "GC-attributable instructions," it means Ir
(instructions retired) summed over functions that are defined in `lgc.c`
itself: `singlestep`, `luaC_step`, `atomic`, `entersweep`, `sweeplist`,
`sweep2old`, `propagatemark`, `traversearray`, `genlink`, `freeobj`,
`luaC_fullgc`, and so on. `luaC_newobj`/`luaH_new` are left out on
purpose: they measured identically across all three runs (42,005,439 /
28,000,308 Ir), which makes sense since creating a table costs the same
regardless of who eventually reclaims it. What actually differs between
the three configurations is the reclamation side, not the allocation
side.

| | Program Ir | GC-attributable Ir | GC % |
|---|---:|---:|---:|
| full | 3,836,359,340 | 226,332,129 | 5.90% |
| incremental | 3,381,722,138 | 258,425,215 | 7.64% |
| generational | 3,255,066,675 | 155,249,322 | 4.77% |

Before going configuration by configuration, one number is the same in
all three and is worth pointing out up front: `freeobj` is called
2,000,022 times, every time. `testbench.lua` allocates 2,000,000 tables;
the remaining ~22 are strings/closures/upvalues that back `test` itself.
Everything ever allocated gets freed at `lua_close`, no matter which
collector is running. The "10,000 survive" line in the testbench only
means those tables outlive intermediate cycles, not that they're
immortal. So the three configurations don't disagree on how much garbage
there is to collect, only on when it gets collected and by which internal
path, which is what the rest of this section is about.

## 2. Full GC

`luaC_fullgc` fires 111 times over the run (once per 100,000-instruction
hook tick). Each call drives the whole state machine to completion in
one go through `luaC_runtilstate` (mark, `atomic`, `entersweep`, sweep)
before returning control. `singlestep` is called 339 times across those
111 cycles, about 3 per cycle, which lines up with walking
`GCSpropagate -> GCSenteratomic -> GCSswpallgc -> GCSswpend`. `atomic`
and `entersweep` are each called 113 times (roughly one per cycle, plus
a couple at the boundaries), and `propagatemark` 1,108,692 times, close
to once per live/gray object, since a full cycle has no memory of
previous progress and re-walks the whole reachable graph from the roots
every time, survivors included.

What stands out in the call graph is that everything happens in a small
number of large bursts. `atomic` alone is 5.12% of total program
instructions despite only 113 calls. Each one is a complete
stop-the-world root rescan. `sweeplist`, called from inside those bursts,
frees 1,973,725 objects across just 339 calls, about 5,800 per call.
That's the direct cost of not keeping any partial sweep state between
cycles: every cycle restarts cold.

## 3. Incremental GC

This is Lua's actual default. `luaC_step` is called 1,159,341 times,
essentially once per allocation once the debt counter crosses its
threshold, with `singlestep` following it 1:1, so the collector is
spread thin across the program instead of showing up in bursts.
`sweeplist` is called much more often than under full GC (150,816
times), but each call frees far fewer objects: 1,948,330 objects over
150,816 calls is about 13 per call, against full GC's ~5,800 per call.
That's the whole idea behind incremental sweeping, small bounded chunks
of work (`GCSWEEPMAX`, left at its default here) instead of one long
pause. `propagatemark`'s total call count ends up close to what full GC
does (just over a million, split across two call sites), because in the
end every reachable object still gets traversed once per major cycle.
Incrementality only changes when that work happens, not how much of it
there is. `atomic`, by contrast, is called only 3,940 times, since most
marking has already happened incrementally by the time a cycle reaches
the atomic phase.

Incremental GC ends up with the highest GC-instruction share of the
three, 7.64%. Spreading the work out avoids long pauses, but it doesn't
avoid the work itself, and calling into `singlestep`'s dispatch loop
over a million times adds real per-call overhead on top of roughly the
same mark/sweep volume that full GC does in bulk.

## 4. Generational GC

Generational GC looks different in the call graph, not just in the
totals. `sweep2old`, which promotes surviving objects from the young
generation into the old one, is called 80,029 times and doesn't appear
at all in the other two configurations. `atomic2gen`, `entergen` and
`finishgencycle` are likewise unique to this run, confirming the
collector really is running a different state machine (shifting between
minor and major collections via `checkminormajor`/`checkmajorminor`),
not just an incremental collector with different constants.

The instruction share backs this up: 4.77% GC-attributable, the lowest
of the three, with `propagatemark` down to 9,882,020 Ir against roughly
64-69M under the other two, about a 7x drop. That matches what
generational GC is supposed to do well on a workload like this: almost
every `obj` allocated in the testbench dies within the same loop
iteration it was created in, so a minor collection that only looks at
the young generation can reclaim nearly everything without ever
re-walking the long-lived `survivors` table or re-marking the full heap.
The cost that does go up is `traversearray`, at 94,796,385 Ir (2.91%),
the single largest GC-attributable line in this configuration and
larger than the equivalent under incremental GC. Minor cycles run far
more often than major cycles do under the other two policies, and each
one re-scans the survivor array checking for old-to-young references.
Each scan is cheap; there are just a lot of them. Overall it's still a
win here: trading one expensive full scan for many cheap partial ones
comes out ahead when most of what you'd be scanning for is already dead
before you get to it.

## 5. Conclusion

All three configurations reclaim the same 2,000,000 tables in the end;
they differ in when and how. Full GC does it in 111 large, complete
bursts. Incremental GC spreads the same underlying work across over a
million small steps and pays extra per-call overhead for doing so.
Generational GC restructures the problem entirely around the fact that
almost none of this workload's garbage survives past one iteration, and
that restructuring is what gets it to the lowest GC-instruction share of
the three (4.77%, against 5.90% for full and 7.64% for incremental) on
this particular testbench.
