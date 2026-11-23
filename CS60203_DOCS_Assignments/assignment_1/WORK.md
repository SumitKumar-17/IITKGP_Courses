# How This Assignment Was Actually Done

This is a step-by-step account of the whole process — not just what the
final code looks like, but what was tried, what broke, how each break
was found, and why each fix works. It's written so either of you could
read it cold and explain any part of it to someone else, including the
parts that went wrong before they went right.

Everything here maps to real files in this repo: `PART_A_REPORT.pdf`,
`README.md` (Part B), `PART_C1_DESIGN.md`, `PART_C2_DESIGN.md`,
`PART_D_REPORT.pdf`, the `.patch` diffs, and `results/`. This document
is the narrative; those are the deliverables.

## 0. Setup: why a separate VM, and what's on it

The assignment needs `make`, `valgrind`, `perf`, and eventually
`gprof2dot`/`graphviz` for call-graph images — none of that runs sandboxed
in the coding environment this was developed in, so a remote Ubuntu VM
(`t1000@10.5.31.157`) was used for every build, every run, every
`valgrind`/`perf` invocation. The local repo is where the source lives
and where the reports get written; the VM is purely execution. Every
code change was: edit locally → `tar`/`scp` to the VM → `make` there →
run/profile there → pull results back. That round trip happened
dozens of times over the course of this.

On the VM: `sudo apt-get install valgrind graphviz build-essential`,
`pip3 install gprof2dot`, and later `pandoc` + `texlive-xetex` (for
turning the reports into PDFs). `perf_event_paranoid` was already `-1`
on this box, so `perf stat` worked without extra permission setup.

## 1. Reading the assignment

The PDF was read fully before writing anything, since Parts B/C/D all
build directly on decisions made reading Part A. Two things stood out
immediately as needing a decision before coding:

- **The real `testbench.lua` doesn't match the PDF's printed Appendix A.**
  The file has `m = 1000, n = 1000` (1,000,000 iterations); the PDF's
  printed appendix shows `n = 100`. The file is what's actually provided
  "alongside this document" and is what all four parts say to use, so it
  was treated as authoritative, and this discrepancy is called out
  explicitly in `PART_A_REPORT.md` rather than silently picked one way.
- **Lua's API has no "full GC" mode to just select.** `LUA_GCINC` and
  `LUA_GCGEN` are real, persistent collector settings; `LUA_GCCOLLECT`
  is a one-shot forced cycle, not a mode. Realizing "full GC" as a
  running configuration (not a single call) needed a design decision —
  covered in Part A below.

## 2. Part A — reading the GC by watching it run

**Goal:** three C drivers, each pinning one GC configuration, profiled
with `valgrind --tool=callgrind`, read into a report.

**`incrementalgc.c` / `generationalgc.c`** are the simple ones:
`lua_gc(L, LUA_GCINC)` / `lua_gc(L, LUA_GCGEN)` before `luaL_dofile`.

**`fullgc.c`** needed the workaround from step 1: `lua_gc(L, LUA_GCSTOP)`
to kill automatic stepping, then `lua_sethook(L, forcecollect,
LUA_MASKCOUNT, 100000)` — a hook that fires `lua_gc(L, LUA_GCCOLLECT)`
every 100,000 VM instructions. That's a real stop-the-world collector:
periodic, complete, non-interleaved cycles. The honest cost of doing it
this way: the hook itself makes the VM check an instruction counter on
*every single instruction*, which shows up as its own ~9.8% overhead —
called out explicitly in the report rather than folded into "GC cost,"
since it isn't GC cost, it's the cost of *driving* GC this way.

**Running it:** each driver built with `-g -O2` against an unmodified
`liblua.a`, then:

```
valgrind --tool=callgrind --callgrind-out-file=NAME.callgrind --quiet ./NAME
callgrind_annotate --threshold=100 NAME.callgrind > NAME.annotate.txt
gprof2dot -f callgrind NAME.callgrind | dot -Tpng -o NAME.png
```

`callgrind_annotate`'s text output is what actually got read — a
`grep`/`python` pass over it pulls out, per function, how many
instructions (`Ir`) it's responsible for. "GC-attributable instructions"
was defined as the sum of `Ir` across every function *defined in
`lgc.c`* — a list built once with `grep -oE` over `lgc.c`'s function
signatures (saved as `results/gcfuncs.txt`) and reused for every
measurement in Parts A and D, so the definition of "GC cost" never
drifts between reports. `luaC_newobj`/`luaH_new` were deliberately
excluded from that sum: they measured *identically* across all three
configurations (proof they're allocation cost, not collection cost —
the same table costs the same to create no matter who reclaims it
later).

**What the numbers said** (see `PART_A_REPORT.md` for the full
walkthrough): full GC runs in 111 large bursts (`luaC_fullgc` called
111 times, each driving `singlestep`→`atomic`→`entersweep`→sweep to
completion); incremental spreads the identical underlying work across
over 1.1 million tiny `luaC_step` calls instead, which costs more in
total instructions purely from per-call dispatch overhead; generational
restructures the problem around the fact that 99% of this workload's
garbage dies within one loop iteration, and comes out cheapest overall
(4.77% GC-attributable vs. 5.90% full, 7.64% incremental). One number
matched across all three regardless: `freeobj` is called exactly
2,000,022 times everywhere, because the *total* garbage this program
ever creates doesn't depend on who's collecting it, only *when*.

**Call graphs:** `gprof2dot`'s output turned out to be very tall
(the actual PNGs are ~900×4600px) — legible, but not something that
belongs inlined into a 5-page PDF. They're kept as the literal
deliverable files (`results/*.png`) rather than force-fit into the
report text.

## 3. Part B — one #define into one runtime knob

**Goal:** `GCSWEEPMAX` (a `#define` in `lgc.c`) becomes settable at
runtime via `lua_setsweepstep(lua_State *L, int n)`.

The smallest correct diff here isn't rewriting every use site — it's
turning the constant into a variable *of the same name*:

```c
#define GCSWEEPMAX_DEFAULT	20
static int gcsweepmax = GCSWEEPMAX_DEFAULT;
#define GCSWEEPMAX	gcsweepmax
```

Every existing reference to `GCSWEEPMAX` (in `sweepstep()` and the four
`stepresult = GCSWEEPMAX` lines in `singlestep`) keeps compiling exactly
as before, reading the variable through the macro, with zero risk of
missing a site during the edit. `lua_setsweepstep` itself just clamps
to a minimum of 1 (a step that sweeps 0 objects per call would spin
forever) and assigns. Declared in `lua.h` right next to `lua_gc`, as
asked.

**Verifying it's actually wired, not just declared:** a tiny extra
driver, `sweeptest.c`, calls `lua_setsweepstep(L, 5)`, runs the real
testbench, then calls it again with `-100` to check the clamp doesn't
crash — clean under `valgrind --leak-check=full`. `git diff` (not
hand-written) produced `PART_B_DIFF.patch`.

## 4. Part C.1 — teaching the compiler to prove a table safe

This is the part where "read the spec, write the code" stopped being
enough, because Lua's parser has no AST — it's a single recursive-descent
pass that emits bytecode as it goes, with no tree left afterward to walk.
Everything had to be tracked *live*, during parsing.

**The core idea.** A candidate is exactly `local t = {...}` — one name,
one expression, that expression a literal table constructor. Two small
arrays were added to `FuncState` (the parser's per-function state,
`lparser.h`):

- `ctorpend` — every table constructor just built, keyed by the
  register it landed in. `constructor()` pushes onto this the moment
  it finishes building a table, *before* it's known whether that table
  will be bound to a name at all.
- `tcand` — confirmed candidates. `localstat` (the code that handles
  `local ... = ...`) checks, right after binding, whether the RHS was
  exactly one pending constructor matching the new local's register —
  if so, it's promoted from "pending" to "confirmed candidate,"
  starting at `escapes = 0`.

**The four rules, and where each one actually lives in the parser:**

1. **Return-escape** — `explist` (the function that parses
   comma-separated expression lists) checks every item *except the
   last* right before discharging it into the next register;
   `retstat` checks the trailing one itself. Both only fire while a new
   flag, `fs->escapectx`, is set — and `retstat` is what sets it, for
   exactly the duration of parsing its own return list.
2. **Capture-escape** — this one was mostly free. `singlevaraux` (name
   resolution) already has the exact line that detects "this name,
   found in an outer function, is being captured as an upvalue by an
   inner one" (`markupval`). One line next to it: if that captured
   local is a candidate, mark it escaping.
3. **Store-escape** — checked at `recfield` (fields inside a `{...}`
   literal) and the single-target fast path of `restassign` (a plain
   `t[k] = v` statement). If the value being stored is a bare candidate
   and the target is a plain variable (not indexed), that's aliasing —
   escape outright. If the target *is* indexed, check whether its
   container is itself a currently-non-escaping candidate: if not (a
   global, an upvalue, or just some other non-candidate variable),
   escape outright; if so, don't decide yet — record a dependency edge
   ("this one escapes if that one does") and leave it for later,
   because the container's own fate might not be known yet.
4. **Call-escape** — `suffixedexp` tracks, locally, whether the thing
   about to be called is still exactly the bare name `primaryexp` just
   produced (any `.`/`[`/`:` suffix clears this, so `f.x(t)` is never
   whitelisted regardless of `f`). A bare unshadowed global gets
   checked against a two-tier whitelist: `type`/`print` are safe
   anywhere (their results never alias the argument); `pairs`/`ipairs`
   are safe *only* as a generic-for loop's control expression
   (`fs->inforctl`, set by `forlist`), because unlike `type`/`print`
   they literally return the table as part of their result — safe only
   because that result can't leave the current frame there.

The dependency edges from rule 3 get resolved once, in `close_func`,
after the whole function body has been parsed: walk the edges,
propagate `escapes = 1` across them, repeat until nothing changes. For
a handful of candidates per function this converges immediately — it's
the "worklist... 1-2 passes" the spec describes, just phrased as a
small graph instead of a literal queue.

**Two real bugs, caught by testing every rule individually before ever
touching the real testbench** (`esctest.lua`, 14 functions, one per
rule plus a whitelist case and an aliasing case, each checked against
`LUA_ESCDEBUG=1` output):

- The whitelist never fired at all, for any function. Cause:
  `buildvar` rewrites a genuine global's expdesc *away* from `VGLOBAL`
  into an indexed `_ENV[name]` access before returning it — checking
  `v->k == VGLOBAL` at the call site (the obvious first attempt) can
  never be true. Fix: capture "was this a global" in `singlevar`,
  right after `buildvar` returns, from what it actually settled on.
- The fixed-point loop silently failed whenever a candidate's real
  variable index didn't happen to equal its position in the candidate
  list. Cause: the edges were recorded as array indices into `tcand`,
  but resolved by re-searching for a `vidx` — the wrong kind of value
  entirely. Fix: just use the array indices directly; they're stable
  for a single function's parse.

**Verified against the real testbench**, once the 14 rule-tests passed:
`survivors` escapes (returned), and — this is the actual finding, not a
bug — `obj` *also* escapes, because
`survivors[#survivors + 1] = obj` stores it into the already-escaping
`survivors`. The analyzer is conservative by design (spec: "fail
toward the GC, never away from correctness"), and it correctly refuses
to promote `obj` even though the store only happens 1% of the time at
runtime — a source-level property, not a runtime one. This one finding
shapes the whole of Part D.3.

## 5. Part C.2 — actually skipping the heap, and two real bugs found the hard way

**The mechanism, in one sentence:** an arena object is a completely
normal `GCObject` — same header, same white color a fresh
`luaC_newobj` would get — except its `next` field (the field every
other object uses to link into `allgc` for sweeping) is left `NULL`.
Since every sweep function only ever walks `next`-chains, an object
that's in none of them structurally cannot be reached by sweep. That's
the entire trick. `CallInfo` (one per active call frame) got one new
field, a simple bump-allocator (`ArenaBlock`: `next`, `used`, `size`),
and `OP_NEWTABLE` picks the arena path with one check —
`p->arenapc[pc]`, a bit baked in per-function by the C.1 analyzer —
made once, at the moment the table is created, never on any later
field access (kept separate deliberately, since the spec is explicit
that a per-access branch on the general path is not acceptable here).

**Bug #1 — real memory corruption, found by trying to break it on
purpose.** The very first version passed casual testing and still
corrupted memory the moment it was tested properly:
`arenatest.lua` deliberately stores a long string and a nested table
into a promoted local, forces `collectgarbage("collect")` *while the
frame is still alive*, then reads both back. Under
`valgrind --track-origins=yes`, the nested table came back pointing at
freed memory.

Finding the cause took real investigation, not a quick guess:
`fprintf`-based instrumentation was temporarily added at each layer
(object creation, mark, sweep, the write barrier) to trace exactly what
color the arena table was and when, across two GC cycles. What it
showed: sweeping isn't just "free the dead ones" — for every
*survivor*, it also resets that object's color back to white
(`(marked & ~maskgcbits) | white | G_NEW` in `sweeplist`). That reset
is what tells the *next* cycle's mark phase "this one hasn't been
re-examined yet, look at it again." An arena table never sweeps, so it
never gets that reset — once marked black in its first cycle, a plain
`if (iswhite(t))` check (exactly what the stack scan uses to decide
whether to re-traverse something) will never fire for it again, ever.
A write barrier can catch a mutation *while* the current cycle is still
draining its `grayagain` list, but a mutation that happens between
cycles can have that pending re-visit silently wiped by
`cleargraylists` before any cycle gets around to it — which is exactly
what was happening: the table got marked once, while still empty, by
an incremental step that ran moments after creation, and the write
that gave it real contents happened after that cycle had already
finished.

The fix (`restartcollection` in `lgc.c`) does manually, once per cycle,
exactly what sweep does for every other survivor: reset every live
arena object back to white right when a new cycle starts. Since the
only kind of object the arena ever holds is a `Table`, they're all the
same size, so every live block can be walked in fixed strides with zero
per-object bookkeeping — just the current thread's `CallInfo` chain.
With that back in place, the existing, *unmodified* write-barrier
machinery turned out to already be sufficient for everything else.

**Bug #2 — a plain memory leak, found while benchmarking Part D.3.**
A function doing nothing but building a table, using it, and returning
one value — leaked one whole arena block (~4KB) on *every single call*.
Cause: `luaD_poscall` is not actually where most function returns go.
`OP_RETURN0`/`OP_RETURN1` (returning zero or one value — the ordinary
case, what almost every real function does) have their own inlined
fast path directly in `luaV_execute` when no debug hook is active,
which duplicates `luaD_poscall`'s bookkeeping instead of calling it —
so the `luaC_freearena` hook, which only lived inside the real
function, silently never ran for the common case. A tail call has the
same shape of gap for a different reason: it reuses the *same*
`CallInfo` for the new function instead of pushing one, so the old
function's arena has to be freed right there, before it's overwritten
and lost. Both are now handled directly at the three sites (two inlined
return paths, one tail-call reuse point) instead of only in
`luaD_poscall`. A smaller, related leak surfaced at the same time: a
promoted table's own struct lives in the arena, but its array/hash
parts are ordinary heap blocks it only *points to* — discarding the
arena block never freed those, so `luaH_freecontents` (everything
`luaH_free` does except the final free of the struct itself) is now
called for every table in a block before that block is dropped or
recycled.

**A real, measured cost, not a bug:** the per-cycle reset that fixes
bug #1 walks every live frame's arena blocks once per cycle, so its
cost scales with how much arena memory a frame is holding *right now*.
A frame that returns quickly holds almost nothing, ever. A frame that
never returns while continuously promoting more tables into its own
arena accumulates an ever-larger set that the same walk has to revisit,
every cycle, for as long as that frame lives — Part D.3 measures this
directly (a purpose-built workload with exactly that shape runs ~80x
slower with promotion on than off) and explains it mechanistically
rather than treating it as noise.

## 6. Part D — benchmarking, and why some of the setup mattered as much as the numbers

**Reused directly:** Part A's methodology (same GC-attributable-Ir
definition, same driver binaries) and Part B's binary
(`lua_setsweepstep` already built and verified).

**New for D.1's parameter sweep:** the 3 driver `.c` files were given an
optional `argv[1]` filename override (default still `testbench.lua`),
and three parameter-variant copies of the testbench
(`testbench_m100_n100.lua` etc.) were generated by `sed`-substituting
`m`/`n` in the real file — never editing the canonical `testbench.lua`
itself, since Parts A-C all depend on it staying exactly as provided.

**A real measurement bug caught before it corrupted the perf numbers:**
this VM's CPU is a hybrid P-core/E-core design. An unpinned `perf stat`
run let the OS migrate the process across core types mid-measurement,
silently splitting every counter across two different microarchitectures
with different baseline IPC — not something you'd necessarily notice
without checking, since `perf` doesn't warn about it. Every D.1 perf
run was redone with `taskset -c 2` pinning both the wrapper and the
target process to the same physical core.

**D.3's "stock" comparison** needed a second, untouched build: a
pristine Lua 5.5.0 tree was exported straight from this repo's own git
history (the commit before any edits) into a sibling directory
(`lua-stock/`) and built there, so "stock vs. current" is a real A/B
between the original interpreter and this one, not a guess.

**The escape-analyzer benefit numbers needed two purpose-built
workloads**, not just the real testbench, because Part C.1 already
proved the real testbench's `obj` never promotes at all — reporting
only the real testbench would show a flat 0% and nothing about whether
the mechanism *works*. `promobench2.lua` (temporary tables in short-lived
per-call frames — the intended use case) shows the real win: ~1,000,000
fewer objects ever reach `freeobj`, GC-attributable instructions down
about a third in relative terms. `promobench.lua` (the same tables, but
accumulated inside one never-returning frame, matching testbench.lua's
own literal structure) shows the real regression from Part C.2's bug #3
(the reset-cost characteristic): the same million-object reduction in
sweep burden is present in both files — the benefit side of the ledger
is identical — but the cost of the periodic reset walk dwarfs it here,
which is the whole reason both numbers are reported side by side
instead of blended into one.

**Charts:** four PNGs (`results/partD/make_charts.py`, matplotlib) were
generated for the numbers that are easier to read as a trend than as a
table — GC% vs. problem size, the six perf counters, the sweep-step
scaling, and the three-workload promotion comparison. Colors are the
validated categorical slots (blue/orange/aqua) from this session's
data-visualization guidance, chosen so the series stay distinguishable
under color-vision deficiency, not picked by eye.

**Getting the PDFs to actually build** took one more real fix: pandoc's
LaTeX table output (`\toprule`/`\midrule`/`\bottomrule` from the
`booktabs` package) crashed `xelatex` and `pdflatex` identically with
an obscure `\cmrsideswitch` error on this specific TeXLive install, for
every table in the document. Rather than chase a package version
mismatch, the three rule commands were redefined to no-ops via a small
LaTeX header passed with `-H` — the tables render as clean aligned
columns with no visible rules, which reads fine and sidesteps the
broken macro entirely.

## 7. What's genuinely uncertain or a judgment call, stated plainly

- **"Full GC" as a hook-driven forced cadence** is an interpretation of
  an API that doesn't have a real "full" mode — a defensible one, but a
  choice, not something the spec spells out.
- **Rule 3's scope is narrower than it could be**: a candidate stored
  *positionally* into a sibling literal (`{a, b, candidate}`) isn't
  specially tracked, and a multi-target assignment with a mismatched
  value count falls back to conservatively escaping the whole list.
  Both are always safe (heap fallback), just not optimized — documented
  as a deliberate scope boundary in `PART_C1_DESIGN.md`, not an
  oversight.
- **Generational mode disables arena promotion entirely, on purpose**:
  minor collections don't do a fresh stack scan at all (they trust
  bookkeeping an arena object structurally can't be part of), so
  `OP_NEWTABLE` only takes the arena path when `gckind == KGC_INC`.
- **The reset-cost characteristic in Part C.2** is a real, inherent
  property of the "skip sweep, redo its reset by hand" design, not
  something patched around — a single long-lived frame that keeps
  promoting tables without returning will always pay this cost, and
  that's stated as a limitation, not hidden.
