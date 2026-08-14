# Part C.1 — Static Escape Analysis, Design Note

## What counts as a candidate

Only the exact form the spec names: `local t = {...}`, one variable, one
expression, and that expression is literally a table constructor. Lua's
parser has no AST to walk after the fact, so candidates are tracked live,
during parsing, in two small per-function tables added to `FuncState`
(`lparser.h`):

- `ctorpend`: every table constructor `constructor()` just built, keyed
  by the register it landed in, cleared at the start of every
  `local ...` statement. It's how a constructor and the local it gets
  bound to are connected, since by the time `localstat` knows which
  local a value belongs to, the constructor itself already finished
  parsing.
- `tcand`: confirmed candidates, one entry per `local t = {...}` site,
  each starting at `escapes = 0` and only ever moving to 1.

`localstat` confirms a candidate right after binding the local: if
exactly one variable was declared, exactly one expression was given, and
that expression's register matches the constructor pending from this
same statement, the pending entry becomes a real candidate. Anything
else (`local a, b = {}, {}`, `local t = f()`, `local t = {} + 0`, and
of course `local t` with no initializer) simply never becomes a
candidate and keeps allocating on the heap exactly as it does today —
correct by construction, since we never claimed to handle it.

## The four rules

**1. Return-escape.** `retstat` and `explist` both call `checkescape` on
every value in a return list, not just the last one — `explist` checks
each item right before discharging it into the next register, and
`retstat` checks the trailing item itself once `explist` returns.
Both are gated by `fs->escapectx`, a flag `retstat` sets for the
duration of parsing its expression list.

**2. Capture-escape.** This one was already almost fully implemented:
`singlevaraux` already detects exactly the moment an inner function
captures an outer local as an upvalue (that's what `markupval` is for).
Escape detection is one line next to it: if the captured variable is a
candidate in the owning function, it escapes.

**3. Store-escape.** Handled at the two places a value actually gets
written into a table field: `recfield` (inside a `{...}` literal) and
the single-target fast path of `restassign` (`t[k] = v` /
`t.k = v` as a statement). Both call `checkstoreescape(dst, src)`:

- if `src` isn't a bare candidate reference, nothing to do;
- if `dst` isn't an indexed target (i.e. this is `x = t`, a plain
  variable getting the candidate's value), `src` escapes outright —
  this is treated as aliasing, not tracked further;
- if `dst` is indexed and its container is *not* itself a tracked,
  currently-non-escaping candidate, `src` escapes (this is the
  "globals are always escaping" case, and also covers upvalues and any
  other local that isn't itself a `local t = {...}` candidate);
- if the container *is* another still-non-escaping candidate, `src`
  doesn't escape outright — a dependency edge is recorded instead
  (`src escapes if container escapes`), since the container's own fate
  might not be known yet.

Those edges are resolved by a small fixed point in `close_func`, run
once the whole function body has been parsed: repeatedly walk the
edges and propagate `escapes = 1` across them until nothing changes.
This is exactly the "worklist... typically converging in 1-2 passes"
the spec describes, just phrased as a dependency graph instead of a
literal worklist — for a handful of candidates per function it
converges immediately.

Two things this does *not* attempt, on purpose: a candidate stored
positionally into a sibling literal (`{a, b, candidate}`) isn't
specially tracked, because at the point that literal is being parsed
its own enclosing local (if any) hasn't been confirmed as a candidate
yet either, so there's nothing precise to check against; and a
multi-target assignment with a mismatched value count
(`a, b = f()`) falls back to conservatively escaping everything in the
list rather than trying to track individual pairings through
`adjust_assign`'s nil-padding and multiple-return logic. Both fall back
to the existing heap path, so both are safe, just not optimized.

**4. Call-escape.** `suffixedexp` tracks, locally, whether the value it's
about to call is still exactly the bare name `primaryexp` just produced
(`barename`) — any `.`/`[`/`:` suffix clears it, so `f.x(t)` or
`t:m()` are never whitelisted no matter what `f`/`m` are. A bare,
unshadowed global name (a plain `name(...)` where `name` isn't a local
or upvalue) gets checked against a two-tier whitelist:

- `type` and `print` are safe anywhere, since neither retains its
  argument in its return value;
- `pairs`/`ipairs` are only safe as the control expression of a generic
  `for` loop (`fs->inforctl`, set by `forlist` around its own
  `explist` call), because unlike `type`/`print` they literally return
  the table as part of their result (`next, t, nil`) — safe *only*
  because that result never leaves the current frame, which is exactly
  what for-loop control state is. `pairs(t)` used any other way (e.g.
  `return pairs(t)`, `local it = pairs(t)`) is guilt-by-default like any
  other unrecognized call, and is verified to behave that way.

One easy trap avoided here: `buildvar` rewrites a genuine global's
`expdesc` away from `VGLOBAL` into an indexed `_ENV[name]` access before
it ever gets back to the caller, so checking `v->k == VGLOBAL` at the
call site (the obvious first attempt) never actually fires. Whether a
name was global has to be captured in `singlevar`, right after
`buildvar` returns, by checking what it settled on instead.

## Verification

`esctest.lua` has one function per rule (14 total, including the
whitelist and an aliasing case: `local a = t; return a`), each named
after its expected verdict, run with a `LUA_ESCDEBUG=1` environment
variable that makes `close_func` print every candidate's final decision
to stderr. All 14 match their expected verdict. Two real bugs were
caught this way before ever touching the real testbench: the whitelist
check initially never fired at all (the `VGLOBAL` issue above), and the
fixed-point loop initially resolved edges by re-searching for a `vidx`
using what was actually already a `tcand` array index, silently failing
whenever a candidate's real variable index didn't coincide numerically
with its position in the candidate list.

The spec's own scope note says a table crossing a `coroutine.yield`
boundary only needs to be treated as an automatic escape, nothing more —
`corotest.lua` confirms that falls out of rule 4 for free: `t[1] = 42;
coroutine.yield(t)` marks `t` escaping with no coroutine-specific code
anywhere, since `coroutine.yield` is just an ordinary call that was
never whitelisted. Clean under memcheck, and correct either way `t`
gets read back (from the yielded value and from the resumed
coroutine's own return).

Running the analysis on the real `testbench.lua`: `survivors` escapes
(rule 1, it's returned), and `obj` also escapes too, not because of
anything about `obj` on its own, but because
`survivors[#survivors + 1] = obj` stores it into `survivors`, and
`survivors` escapes (rule 3). That store only happens on 1 in every
`survive_every` iterations at runtime, but the analysis works at the
source level, not per execution, so it doesn't get to assume the
uncommon branch won't run — which is exactly the spec's own "fail
toward the GC, never away from correctness" requirement. The consequence
for Part D is that the main testbench loop has nothing to promote,
which is a real, useful result on its own, discussed there together with
a smaller workload built specifically to show the mechanism firing.
