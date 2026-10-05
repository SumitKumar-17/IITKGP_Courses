# NOVA-KV Part B: RCU reads and bounded-pause reclamation

Sumit Kumar (22CS30056), Jatin Mahawar (22CS30032)

The change lives inside `KVStore` (`kvstore.h`, `kvstore.cpp`). Nothing was forked: the Part A
store was edited in place and `PART_B_DIFF.patch` is the diff against it. **There is still no
durability: if the process dies, all data is gone.**

## 1. How a version is built and published

The table is an immutable radix trie over the key's hash: inner nodes have 64 children, leaves
hold up to 8 entries as a key array followed by 48-byte value slots (values up to 40 bytes are
inline, longer ones are a separate heap block). A `Version` is a tiny record `{root, count}`, and
`KVStore::current_` points at the live one.

A write (`put` or `erase`) does this, under a writer mutex that readers never touch:

1. copy only the nodes on the path from the root to the changed leaf (about 3 inner nodes and one
   leaf, around 2 KB), leaving everything else shared with the old version;
2. allocate a new `Version` pointing at the new root;
3. **publish with one atomic store of `current_`**;
4. put every replaced object (old path nodes, old leaf, old `Version`, a replaced long value) on the
   retire list, stamped with the epoch of the swap.

A read (`get`) does this and takes no lock:

1. announce itself in the epoch table;
2. **load `current_` exactly once** and use only that version until it returns;
3. copy the value out and clear its announcement.

So a reader sees one complete snapshot, never half of a write, and the writer never waits for it.

## 2. Reclamation: epoch-based

Readers cannot be tracked individually by the writer, so each reader thread owns one cache-line
sized slot in a process-wide table. On entry it stores the current global epoch into its slot with
a full barrier (an `exchange`, which compiles to one locked instruction), then loads `current_`.
On exit it stores "idle". The writer, after each publish, stamps the retired objects with the
global epoch and bumps it.

A retired record may be freed when its stamp is **smaller than the oldest epoch any reader has
announced**. Why that is safe: a reader that can still see the old version announced its epoch
before it loaded the pointer, so its epoch is at most the stamp. Either the writer's scan sees that
announcement (and keeps the record), or the reader's load comes after the swap and sees the new
version. This is the usual store-then-load argument and it needs the full barrier on both sides:
the reader's `exchange` and the writer's `seq_cst` store plus fence before scanning.

Records are kept in publish order, so reclamation frees from the oldest end and stops at the first
record that is still pinned.

### Why epochs and not hazard pointers

We built both on identical code (`bench/kvstore_hp.cpp` is the same store with hazard pointers)
and measured `get` throughput with the Part A driver, 1 background writer, median of 3 runs:

| readers | epoch (M reads/s) | hazard pointers (M reads/s) |
|---|---|---|
| 1 | 14.9 | 14.9 |
| 2 | 23.7 | 23.4 |
| 4 | 43.5 | 40.1 |
| 8 | 74.0 | 69.8 |

**Reader speed barely separates them**: equal at 1 and 2 readers, epochs 4% to 8% ahead at 4 and 8 (the 8-reader runs overlap). The reasons we kept epochs:

- A hazard pointer reader has to publish the pointer and then re-read `current_` to check it did not
  change, so it loads the pointer twice. The assignment asks for exactly one load. Epochs need one.
- The usual advantage of hazard pointers is that a stalled reader only blocks the objects it points
  at. Here every version shares most of its nodes with its neighbours, so a retired object is only
  free once *every* older version is unpinned. Both schemes end up holding back everything newer than
  the oldest pinned version, so that advantage is gone.
- Part C needs a long-lived `Snapshot` handle. With epochs that is just a slot that stays
  announced; no extra bookkeeping.

## 3. The tunable: `set_reclaim_batch(int n)`

`batch_` (private) is the most retired **versions** one reclamation pass may free. A pass runs at
the end of every publish, on the writer thread:

- it scans the epoch table once, then frees at most `n` records from the oldest end, only those
  whose stamp is below the oldest announced epoch;
- it **never waits**: a record that is still pinned just stays for a later pass, so it cannot block
  a reader or the writer. A reader never runs reclamation at all;
- it does not walk the whole list. Cost per pass is bounded by `n` times the objects in a record
  (a handful) plus one scan of the epoch table.

`n = 0` (or negative) switches reclamation off: nothing is freed until the store is destroyed.
The default is 64. `reclaim_stats()` returns `retired`, `freed`, `backlog_peak`, `passes`,
`max_pass_freed` and `max_pass_ns`; the stress test and the Part D.2 experiments use them. A bigger
batch drains a backlog in fewer passes; a smaller one keeps each pass short. Part D.2 measures
that trade.

## 4. Choosing the version structure

Copying the whole table on every put is the obvious RCU design, so we measured it and three ways of
copying less (`bench/design_bench.cpp`, 8 readers, writer at 50,000 puts/s, 3 runs, medians; full
table in `results/design_sweep.txt`, summarised by `tools/summarize_design.py`):

| design | put cost, 100k keys | reads, 100k keys | put cost, 1M keys | reads, 1M keys |
|---|---|---|---|---|
| copy whole table | **about 5 ms (extrapolated)** | n/a | n/a | n/a |
| segment directory, inline slots | 3.9 us | 428M/s | 9.3 us | 169M/s |
| segment directory, value pointers | 3.1 us | 387M/s | 7.1 us | 113M/s |
| **64-way trie, 8 per leaf (chosen)** | **0.9 us** | 371M/s | **1.0 us** | 142M/s |

The read columns come from a bare read loop with no per-call timing, so they are higher than the
driver numbers and only comparable with each other. Whole-table copy cost 0.23 ms per put at 5,000 keys, 0.51 ms at 10,000 and 1.06 ms at 20,000
(`results/full_copy.txt`), growing in step with the table, so it cannot keep up with 50,000 puts per second at the workload's 100,000 keys. The segment
directory copies a root of `NSEG` pointers plus one segment, which is cheap at 100k keys but grows
with the table (the best segment count moves with table size, and growing it means rebuilding
everything). The trie copies a path whose length grows only with the logarithm of the table, uses
less memory (9 MB against 16 MB at 100k keys), builds its 1M-key table in 0.2 s against 2.7 s, and
shares its copied path between several writes in one transaction, which Part C uses. It reads about
13% to 16% slower than the segment directory in these runs. Every design here reads far faster than any
locked store (the chosen one does 75M reads/s at 8 readers in the Part A driver, against 21M
for the rwlock and 12M for the global lock, so 3.6x and 6.2x; Part D has the full tables), so we took the flat write cost.
Every design in the benchmark checks all keys and values after loading.

## 5. Tests

| what | how | result |
|---|---|---|
| unit test | `make test`: insert, overwrite, erase, 500-byte values, 200k keys, erase everything and reuse, reclaim-batch bookkeeping | all pass |
| **stress test, no torn rows** | `build/stress_test`: 6 readers `get` while the writer does `put` and `erase` on 5,000 keys for 5 s. Each row is a version number plus three words derived from it and the key; readers recompute them and also check a key's version never goes backwards | 5.5M writes, 682M reads, **0 torn rows, 0 regressions**, every retired version freed, no pass freed more than the batch (`results/tests.txt`) |
| sanitizers | `make check`: AddressSanitizer + UBSan on the unit and stress tests, ThreadSanitizer on the stress test | clean (ASan found a real `new`/`free` mismatch on the way, fixed) |
| does the test have teeth | build with `-DNOVA_BROKEN_RECLAIM`, which makes reclamation ignore readers, and run the stress test under ASan | `heap-use-after-free` in `find` within seconds |

One caveat: ThreadSanitizer does not model `atomic_thread_fence`, which the writer's scan uses, so
its clean result is weaker than the ASan one. The use-after-free control above is the stronger
evidence for the reclamation logic.

## 6. Assumptions and limits

- One writer at a time. A mutex serialises writers so a second one is safe, but is not fast.
- The epoch table holds 1,024 slots shared by every store in the process; more concurrent readers
  than that abort. Readers hold one slot per thread, plus one per live `Snapshot` in Part C.
- The epoch is process-wide, so a pinned reader on one store also delays reclamation on another.
  That is conservative, never unsafe.
- `Transaction` still applies its puts one by one, so it is not atomic yet. Part C replaces it.
- No durability. A crash loses everything.

## 7. Files

`kvstore.h`, `kvstore.cpp`, `kvstore_test.cpp`, `stress_test.cpp`, `rcu.cpp` (driver for Part D),
`Makefile`, `PART_B_DIFF.patch`, `compare_schemes.sh`, `bench/design_bench.cpp`, `bench/sweep.sh`,
`bench/kvstore_hp.cpp`, `results/design_sweep.txt`, `results/schemes.txt`.
