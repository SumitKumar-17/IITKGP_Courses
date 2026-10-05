# Assignment 2: NOVA-KV (DOCS, IIT KGP, Autumn 2026)

Team: Sumit Kumar (22CS30056), Jatin Mahawar (22CS30032). Deadline 11:59 PM, 25 Oct 2026.
Zip name: `Asgn2_22CS30056_22CS30032.zip`, one submission on Moodle. 200 marks + 20 bonus.
Spec: `assignment_2.pdf`. Provided: `common/workload_gen.{h,cpp}` (do not modify).

## Working rules
- Write like a human student: plain prose, no em dashes, no AI-sounding phrasing, no filler
  comments in code or reports. Comments only where the why is not obvious.
- All building, benchmarking and profiling happens on the server `t1000@10.5.31.157`
  (passwordless ssh, project dir `~/docs_asgn2`). Local copy is the source of truth for code.
  sudo password is in `pass.txt` (git-ignored, never commit, never sync). Ask the user before
  any sudo / apt install.
- `./syncer.sh push|pull|run|part|sh` moves files. `./submitter.sh` builds PDFs + the zip.
- Work part by part, one branch (`assignment-2-nova-kv`), small commits. Do the long, careful
  way: try several designs, measure, pick the best by data. No shortcuts.
- C++17, no external concurrent hash-map libraries.

## Layout
common/ (workload gen) | partA..partE/ each with its own kvstore.{h,cpp}, drivers, Makefile,
results/, REPORT.md (+PDF). Each part copies the previous part's store so the diff is real.

## Key requirements per part
A (50): hash table KVStore put/get/erase; kvstore_test.cpp; single_thread.cpp, global_lock.cpp
(std::mutex), rwlock.cpp (std::shared_mutex), same data structure, only sync differs.
Profile global_lock + rwlock at 1/2/4/8 readers with one background writer: perf stat (cache
misses, IPC) and perf record -> FlameGraph (stackcollapse-perf.pl, flamegraph.pl), 4 graphs each.
Say whether lock acquire time or cache-line bouncing dominates, with evidence.
Layout note (<=3 of 5 pages): NSM vs DSM vs PAX, argue one, with a microbenchmark of
point-get under two layouts. Report <=5 pages. (20 core + 20 layout + 10 profiling)
B (40): RCU inside the same KVStore class. Writer builds new version/buckets off to the side,
publishes with one atomic pointer swap; reader does exactly one atomic load per op.
Epoch-based or hazard-pointer reclamation (state which and why). Private member for batch size
+ public `void set_reclaim_batch(int n)`. A reclaim pass frees at most n retired versions,
never blocks reader or writer. README on design, diff vs Part A, stress test for no torn rows.
C.1 (30): Transaction (put stages, commit publishes with one pointer swap, dtor discards).
Atomic: reader sees none or all. Design note: snapshot isolation, explain write skew (doctors
on call), what is lost without durability.
C.2 (30): secondary hash index or B+tree on a non-PK field of the Instrument table
(create_index, index_get). Snapshot handle (move-only) acquired ONCE per join, passed to
keys(), index_get(snap,..), get(snap,..). Outer loop iterates keys(). Index-nested-loop join.
Snapshot pins version through reclamation. Design note + diff vs Part B.
D (50): D.1 RPS + p50/p99/p999 for single, global lock, rwlock, RCU at 1/2/4/8 readers with a
background writer; scaling-stop thread count per lock config; does RCU hit 100k+ RPS at 8
readers (if not, flame graph for bottleneck). D.2 reclaim batch at >=4 values (8,64,256,1024)
plus no-reclaim and naive stop-the-world reference; report RPS and longest reader stall.
D.3 join throughput/latency at 8 readers with writer on vs paused; txn commit throughput for
1/4/16 keys per txn; bounded vs stop-the-world worst pause; explain when COW hurts.
E (+20): WAL, replay on restart, fsync/group commit policy, no read-path overhead, rerun D.1
write numbers with/without WAL, half page on what is missing for a real desk.

## API (Appendix A.1), exact
KVStore: ctor, dtor, copy deleted, put(long,const void*,size_t)->bool,
get(long,void*,size_t) const->bool, erase(long)->bool; B: set_reclaim_batch(int);
C: create_index(size_t off,size_t len)->bool, index_get(const void*,size_t,long*,int) const->int,
class KVStore::Snapshot (move-only), acquire_snapshot() const, get(snap,...), index_get(snap,...),
keys(snap,long*,size_t) const->size_t. Transaction(KVStore&) explicit, copy deleted,
put(long,const void*,size_t), bool commit(). get copies value out (no pointers into storage).

## Gotchas
- Provided workload_run loads all data with the writer BEFORE readers start; D.1 wants a
  background writer throughout, so drivers need their own writer thread running during reads.
- Trade is 32 bytes (long, char[8], double, long); Instrument keys 0..49, index on symbol.
- Default workload: m=1000, n=100 (100k trades), hot_every=100, 8 readers, 5 s.
- State plainly that no crash durability is provided in A to D.
- Server: i9-13900K, 32 threads, perf 6.8, g++ 12.3, pandoc/xelatex present, no gnuplot,
  FlameGraph scripts not installed yet. perf_event_paranoid = -1.

## Status
- Part A: done (store, test, 3 drivers, profiling, layout bench, REPORT.md/pdf). Store is an
  open-addressing table, 64-byte inline slots; NOVA_SYNC=0/1/2 picks none/mutex/shared_mutex.
  Drivers take --readers N --seconds S --writer-rate R (default 50k puts/s). Benchmarks are
  pinned with `taskset -c 0-15` (P-cores). Measured: single 19.3M rps, global_lock 16.8M->9.5M,
  rwlock peaks 19.5M at 2 readers, 18.0M at 8. Kernel frames in flame graphs need
  `sudo sysctl kernel.kptr_restrict=0` (set back to 1 afterwards).
- Part B: done (64-way path-copy trie, epoch reclamation, README, stress test). Store keeps
  Version{root,count}; global epoch table of 1024 slots; batch default 64; set_reclaim_batch(0)
  disables reclaim; reclaim_stats() exposes counters. Driver partB/rcu.cpp.
- Part C: done (Transaction one-build-one-publish with first-committer-wins, index trie in Version,
  Snapshot with lazy multi-store binding, keys(), join.h). Tests in partC/txn_test.cpp.
- Part D, E: not started.
