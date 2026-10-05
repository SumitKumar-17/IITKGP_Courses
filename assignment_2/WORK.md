# WORK.md: how we actually did Assignment 2

This is the private working log, not part of the submission (`submitter.sh` only packs
`common/` and `part*/`). It is here so that anyone who has to redo or explain a step later can
see what was done, why, with which command, and what was measured to justify it. The reports
have the polished version. This has the story.

---

## 0. Setup

**Goal of the assignment in one paragraph.** Build a small in-memory key-value store (NOVA-KV)
for a trading desk. Readers must never wait. Part A builds a plain locked store and measures
how bad locks are. Part B replaces locks with RCU (copy-on-write plus one pointer swap).
Part C adds multi-key transactions and an index join on top of RCU. Part D benchmarks all of
it. Part E is a bonus write-ahead log.

**Where things run.** Everything that is compiled, benchmarked, profiled or turned into a PDF
runs on the lab server `t1000@10.5.31.157` (passwordless ssh). The laptop only stores the
sources and results and builds the zip. The server is an i9-13900K: 8 performance cores with
hyperthreads (CPUs 0 to 15) plus 16 efficiency cores (CPUs 16 to 31). Because the machine is
hybrid, every benchmark is pinned with `taskset -c 0-15` so a reader thread never lands on a
slow efficiency core and skews the numbers.

**The scripts.**

| script | what it does |
|---|---|
| `./syncer.sh push` | rsync sources to `~/docs_asgn2` on the server (never sends `pass.txt` or results) |
| `./syncer.sh pull` | copy every `part*/results/` back to the laptop |
| `./syncer.sh run '<cmd>'` | push, then run a command in the server project dir |
| `./syncer.sh part A test` | push, then `make -C partA test` on the server |
| `tools/pdf.sh partA/REPORT.md` | build the PDF with pandoc and xelatex on the server, copy it back |
| `./submitter.sh` | rebuild all report PDFs, then zip `Asgn2_22CS30056_22CS30032.zip` |

**Server changes we made (all reversible).**

- Cloned Brendan Gregg's FlameGraph into `~/docs_asgn2/FlameGraph` (needed for the flame graphs).
- `kernel.kptr_restrict=0` while profiling, then back to `1`. Without it, perf cannot name kernel
  functions and the biggest part of the lock cost shows up as a useless `[unknown]` block.
  Command: `sudo sysctl -w kernel.kptr_restrict=0` (and `=1` afterwards).
- CPU governor and energy preference set to `performance` (it was `powersave` /
  `balance_performance`) to remove frequency ramping as a source of noise.
  Command: `echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
  and the same for `.../energy_performance_preference`. Undo by writing `powersave` and
  `balance_performance` back.
- Nothing was installed with apt. `perf`, `g++ 12.3`, `pandoc`, `xelatex`, `google-chrome` and
  `matplotlib` were already there.

---

## 1. Part A

### 1.1 What the spec wants (short)

A hash-table KV store with `put/get/erase`, a sanity test, three drivers that differ only in
synchronisation (none, one `std::mutex`, a `std::shared_mutex`), `perf` profiling of the two lock
versions at 1/2/4/8 readers with one background writer, flame graphs, an explanation of where
time is lost (lock waiting versus cache-line bouncing, with evidence), and a design note on
NSM versus DSM versus PAX backed by a microbenchmark.

### 1.2 Hash table: three designs compared first

Decision: which table layout to use. We did not just pick one. `partA/bench/table_bench.cpp`
implements three tables and times them single-threaded:

1. chained (one malloc'd node per key),
2. open addressing, 24-byte slots pointing at a heap value,
3. open addressing, one 64-byte cache-line slot with the value inline (48 bytes of room).

Command (on the server): `g++ -O2 -std=c++17 partA/bench/table_bench.cpp -o /tmp/table_bench &&
taskset -c 2 /tmp/table_bench`, run three times.

Result at 100k keys: get 11.0 ns / 9.4 ns / 8.9 ns. At 1M keys: 44.1 / 50.2 / 35.7 ns. Insert is
the other way round (inline is slowest, about 100 to 136 ns). We chose the inline slot because
the workload is almost all gets, and a hit then touches exactly one cache line.

Other choices in `kvstore.cpp` and why:

- Linear probing with backward-shift deletion, so no tombstones pile up.
- Load factor 0.5, table doubles. Keeps probe chains short.
- Hash is the splitmix64 finaliser. Keys from the generator are `i * 100000 + j`, which are very
  regular, and a weak hash would cluster them.
- Values over 48 bytes go to the heap, so the store still accepts any size even though the
  benchmark only uses 32 bytes.

### 1.3 Synchronisation chosen at compile time

The assignment says the three drivers must use the same data structure and differ only in
locking. `kvstore.h` therefore has `#if NOVA_SYNC == 0/1/2` which picks the lock member and the
guard type (`lock_guard<mutex>`, or `shared_lock` / `unique_lock<shared_mutex>`). The Makefile
builds each driver with its own `-DNOVA_SYNC=n`. The table code is literally the same file.

### 1.4 Things in the provided `workload_gen.cpp` that matter

- It calls `create_index`, `index_get` and `Transaction` even when no join is used, so even the
  Part A store has to declare them. We added stubs (`create_index` returns false, `Transaction`
  just batches puts and applies them on commit). They do nothing real until Part C.
- `workload_run` loads all trades with the writer **before** starting the readers. The spec asks
  for a background writer throughout the read phase, so `common/driver_main.h` starts its own
  writer thread. It waits until the last trade id is readable (that is how it knows loading is
  done), then overwrites random existing trades at 50,000 puts/s until the readers stop. Only
  one thread writes at any moment, which keeps the "single writer" assumption for Part B true.
- It times every `get` with `steady_clock::now()`. That costs about 20 to 25 ns per call and shows
  up in the flame graphs as `[vdso]` / `clock_gettime` (10% to 66% of samples). It is the same for
  every store, but it means lock time is a smaller slice than it really is for the store itself.
  We say this in the report.

### 1.5 Verification of the store

- `make test` on the server: `kvstore_test` passes.
- `make check` runs the test under AddressSanitizer plus UBSan for all three sync modes, and
  runs the rwlock driver under ThreadSanitizer. All clean: no memory errors, no data races.

### 1.6 Throughput sweep (no profiler)

`partA/scaling.sh` runs `single_thread` and both lock drivers at 1/2/4/8 readers, 3 repetitions,
pinned to CPUs 0 to 15. Output in `partA/results/scaling.txt`. Means:

| readers | global_lock | rwlock |
|---|---|---|
| 1 | 16.7M | 16.7M |
| 2 | 12.0M | 19.2M |
| 4 | 11.1M | 18.0M |
| 8 | 9.2M | 18.1M |

`single_thread` with one reader: 19.4M. (Side fact for Part D: `single_thread --readers 8`, which is
only legal with no writer, reaches about 94M reads/s, so unsynchronised read-only scales almost
linearly. That is the ceiling the RCU store is trying to approach.)

### 1.7 Profiling: how and why this way

`partA/profile.sh` does, for each of `global_lock` and `rwlock` at 1/2/4/8 readers:

1. start the driver in the background with a long enough duration,
2. `sleep 2` so the load phase is over and all readers run,
3. `perf stat -e task-clock,cycles,instructions,cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses,context-switches -p <pid> -- sleep 3`,
4. `perf record -F 997 --call-graph dwarf,16384 -p <pid> -- sleep 3`,
5. `perf script | stackcollapse-perf.pl > x.folded`, then `flamegraph.pl x.folded > x.svg`.

Two bugs we hit and fixed, worth remembering:

- Running `perf stat` on the whole program also counted the end-of-run merge and sort of about
  100M latency samples, which polluted the cache counters. Fix: attach to the running pid for a
  fixed window.
- The first version ran `perf stat` and `perf record` one after the other for 3 s each but the run
  lasted only 7 s, so the record window slipped into the final sort (`std::__unguarded_partition`
  showed up in the flame graph). Fix: run duration is `2 * window + 5`.

Frame pointers (`-fno-omit-frame-pointer`) were not enough because libc is built without them, so
`--call-graph dwarf` is used instead.

SVG to PNG for the report: headless Chrome,
`google-chrome --headless=new --no-sandbox --screenshot=x.png file://.../x.svg`.

`tools/summarize_partA.py` turns the stat files and folded stacks into the two tables in the
report (counters per thread count, and share of samples by category of the leaf function).

### 1.8 What the profile showed (the actual answer to the question)

The spec asks: lock acquisition or cache-line bouncing? The answer is that it is different for
the two locks.

- `global_lock`, 8 readers: 56% of samples in the kernel futex path
  (`native_queued_spin_lock_slowpath`, `futex_wait`, `futex_wake`, `lll_lock_wait`) and 447k context
  switches per second. Threads sleep and wake. That is waiting for the lock.
- `rwlock`, 8 readers: only 5.8% in the kernel, but 64% inside `pthread_rwlock_rdlock` and
  `rdunlock`. Those are atomic add/sub on one shared reader counter. IPC falls from 1.66 to 0.38
  and L1 misses per 1k instructions rise from 1.8 to 7.0, while last-level misses stay at about
  0.03 per 1k instructions. The cache line holding the counter moves between cores' private
  caches and never goes to memory. That is cache-line bouncing.
- The plain `cache-miss %` is misleading (it falls from 28% to 4% as threads grow) because its
  denominator, cache references, grows with thread count. Misses per instruction is the
  honest metric.

### 1.9 Layout microbenchmark (NSM vs DSM vs PAX)

`partA/bench/layout_bench.cpp` builds the same 32-byte `Trade` table three ways (NSM array of
structs, DSM one array per field, PAX with 16/64/256 rows per page) and measures:

- whole-row get (random row, next index depends on the data read, so it measures latency),
- symbol-only get (the join probe),
- full scan of the price column,
- 8-thread whole-row get throughput.

At 100k rows (the real workload, 3.2 MB) and at 4M rows (128 MB, out of cache). Three runs.

One mistake caught on the way: the first 8-thread loop only used `id` and `ts`, so the compiler
dropped the loads of the other fields and DSM looked faster. Fixed so every layout reads the whole
row, as the readers do.

Result: NSM wins whole-row gets (15.7 ns vs DSM 18.6, PAX-64 17.5 at 100k; 116 vs 150 and 140 at
4M) and 8-thread throughput by 20% to 30%. DSM wins single-field reads and scans, which the
workload does not do. PAX never wins a pattern outright. Decision: NSM, and the report says
what would change our mind (a column-scan workload).

### 1.10 Report

`partA/REPORT.md` (4 pages with the layout note). Key sentences are in bold. The rendered
PDF is `partA/REPORT.pdf`. Rebuilt with `tools/pdf.sh partA/REPORT.md`.

---

## 2. Part B

(Filled in as we go. See below.)
