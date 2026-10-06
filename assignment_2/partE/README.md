# NOVA-KV Part E (bonus): a write-ahead log

Sumit Kumar (22CS30056), Jatin Mahawar (22CS30032)

The log is in `wal.h` / `wal.cpp`; `kvstore.h` / `kvstore.cpp` are the Part C store with the log
wired into the commit path (`PART_E_DIFF.patch` is the diff against Part C). Everything before this
part is unchanged and still has no durability. With a log open, **committed writes survive a process
crash, and at the strongest level a power cut**, and a restart rebuilds the last committed state.

## 1. What it does

`KVStore::open_wal(path, options, &info, &error)` replays an existing log into the (empty) store,
drops a damaged tail, and from then on logs every commit: a single `put`, an `erase`, a whole
`Transaction`, and the creation of the index. One commit is one record, so a transaction is replayed
whole or not at all.

A record is `[24-byte header][operations]`. The header holds a magic number, the payload length, a
**log sequence number** and a **CRC32C over the header and the payload**. Replay checks, in order:
the magic, that the record fits in the file, the checksum, that the sequence number is exactly the
next one, and that the operations parse and add up to the length. It stops at the first record that
fails and drops everything after it. The file is preallocated (so the unused tail is zeros, which
means "end of log"), the directory entry is fsynced when the file is created, and a failed `write`
or `fdatasync` puts the log in a failed state where no further commit is accepted (we never retry an
`fsync` that reported an error, because after one the kernel may already have dropped the dirty data).

**The read path is untouched.** `get`, `index_get` and the snapshot `get` are byte-for-byte the same
functions as in Part C (checked with `diff` on the three function bodies), and the benchmark below
shows the same read rate with every log setting.

## 2. Where the fsync goes, and why

Every write goes through the same steps, under the writer lock:

1. check the transaction (first-committer-wins) and build the new version off to the side, as before;
2. **append the record to the log** (into a buffer, or straight to the file, depending on the level);
3. **publish** the version with the one atomic pointer store;
4. release the lock, then **wait until the record is as safe as the level promises** and only then
   return from `commit()`.

Logging comes before publishing so that the log order is exactly the publish order and a version can
never become visible with no record behind it; if the append fails, the version is never published.
The wait comes *after* the lock is released so that other commits can proceed and **share the same
sync**, which is the group commit. We chose not to hold publication back until the sync has finished:
that would either hold the writer lock for the whole 0.2 to 1.5 ms of an `fdatasync` (serialising all
commits, so no batching is possible) or delay what readers see by that long, and in this system
fresh data on the read path is the point. The price is a precise one: **a reader can see a commit
that a power cut then loses**, because visibility comes before the sync. Recovery always gives a
consistent prefix of the order in which versions were published, never a state that never existed.

Three levels, chosen when the log is opened:

| level | `commit()` returns after | survives `kill -9` | survives a power cut or OS crash | what can be lost |
|---|---|---|---|---|
| **Async** | the record is in the in-process buffer; a thread writes and `fdatasync`s every `flush_interval_ms` | no | no | the last flush interval plus any slow sync (measured below: 17 ms of commits in the recorded run, 60 ms in an earlier one when the disk was slow) |
| **Write** | the record is in the kernel's page cache (`write()` has returned); a thread `fdatasync`s every `flush_interval_ms` | **yes** | until the next background sync | nothing on a process crash; at most the last interval on power loss |
| **Sync** | `fdatasync` has returned for a group that includes the record | yes | **yes** | nothing |

`fdatasync` rather than `fsync`: the file is preallocated in 64 MB chunks, so an append changes no
size metadata and `fdatasync` has less to flush (231 us against 280 us measured on this machine's
disk). **Group commit** is leader and follower: the first committer to find no flush in progress
becomes the leader, optionally waits `group_delay_us` for more records to arrive, swaps the buffer,
writes it and syncs once for everyone; committers that appended meanwhile are covered by it or by the
next group. With a single committing thread there is nobody to share with, so one commit costs one sync.

**What we recommend.** For the market-data feed this store is built for, **Write with a 1 ms
background `fdatasync`**: it survives a process crash with no loss at all, loses at most about a
millisecond of commits to a power cut, and costs the writer well under a microsecond more per commit
(table below). Use Sync, with several committing threads and a group delay of 100 us or so, only
where each acknowledged commit must be on stable storage when it returns.

## 3. What it costs

Server: i9-13900K, NVMe SSD on ext4, medians of 3 runs of 5 s, the table loaded first and not logged.
The disk's `fdatasync` latency drifted between runs by a factor of several (a probe before each run read
228 us at best, 432 us at the median and 1,315 us at worst over the 120 runs), so every row shows the
average sync latency of its own run, the individual runs are listed, and group commit is judged by
*commits per sync*, which does not depend on how slow the disk is.

#### E1: the D.1 setup, 8 readers, writer asked for 50,000 puts/s

| log | reads/s (M) | read p99 (ns) | read p999 (ns) | writer puts/s | put p50 (us) | put p99 (us) | put max (us) | syncs/s | avg sync (us) |
|---|---|---|---|---|---|---|---|---|---|
| none | 120.9 | 160 | 268 | 50000 | 0.96 | 1.33 | 46 | 0 | 0 |
| async | 119.1 | 160 | 250 | 50000 | 1.02 | 1.39 | 40 | 636 | 515 |
| write | 119.3 | 160 | 260 | 50000 | 1.49 | 3.07 | 159 | 314 | 2130 |
| sync | 124.8 | 78 | 188 | 2535 | 225.28 | 1540.10 | 6871 | 2535 | 392 |

#### E2: one committer flat out, 0 readers running

| log | keys per txn | commits/s (k) | keys/s (k) | commit p50 (us) | commit p99 (us) | commit max (us) | syncs/s | avg sync (us) | reader reads/s (M) | runs: commits/s (k) |
|---|---|---|---|---|---|---|---|---|---|---|
| none | 1 | 3622.9 | 3622.9 | 0.23 | 0.51 | 30 | 0 | 0 | 0.0 | 3639.1, 3612.2, 3622.9 |
| none | 4 | 1644.3 | 6577.1 | 0.58 | 1.14 | 61 | 0 | 0 | 0.0 | 1644.6, 1638.9, 1644.3 |
| none | 16 | 363.3 | 5812.8 | 2.66 | 3.97 | 136 | 0 | 0 | 0.0 | 363.3, 363.8, 362.1 |
| async | 1 | 2844.6 | 2844.6 | 0.31 | 0.58 | 372 | 576 | 586 | 0.0 | 2844.6, 2838.0, 2847.5 |
| async | 4 | 1324.3 | 5297.0 | 0.72 | 1.28 | 1282 | 469 | 928 | 0.0 | 1324.3, 1327.5, 1320.8 |
| async | 16 | 298.1 | 4769.4 | 3.23 | 4.93 | 705 | 507 | 797 | 0.0 | 298.9, 298.0, 298.1 |
| write | 1 | 1554.1 | 1554.1 | 0.59 | 1.54 | 153 | 327 | 2003 | 0.0 | 1554.1, 1551.0, 1578.7 |
| write | 4 | 898.2 | 3592.8 | 1.02 | 2.18 | 363 | 341 | 1880 | 0.0 | 894.0, 898.2, 901.3 |
| write | 16 | 252.2 | 4035.0 | 3.71 | 6.34 | 133 | 597 | 620 | 0.0 | 251.0, 253.0, 252.2 |
| sync | 1 | 4.4 | 4.4 | 215.04 | 462.85 | 6884 | 4410 | 226 | 0.0 | 4.4, 3.6, 4.5 |
| sync | 4 | 2.5 | 9.8 | 215.04 | 2080.77 | 6878 | 2455 | 405 | 0.0 | 1.0, 4.3, 2.5 |
| sync | 16 | 0.9 | 13.8 | 1490.94 | 3112.96 | 6879 | 862 | 1140 | 0.0 | 0.9, 3.8, 0.9 |

#### E2: one committer flat out, 8 readers running

| log | keys per txn | commits/s (k) | keys/s (k) | commit p50 (us) | commit p99 (us) | commit max (us) | syncs/s | avg sync (us) | reader reads/s (M) | runs: commits/s (k) |
|---|---|---|---|---|---|---|---|---|---|---|
| none | 1 | 1074.8 | 1074.8 | 0.89 | 1.34 | 113 | 0 | 0 | 86.2 | 1091.9, 1074.8, 997.5 |
| none | 4 | 558.3 | 2233.0 | 1.68 | 2.85 | 145 | 0 | 0 | 91.2 | 564.1, 546.9, 558.3 |
| none | 16 | 162.0 | 2591.2 | 5.95 | 10.50 | 236 | 0 | 0 | 96.4 | 204.3, 162.0, 160.8 |
| async | 1 | 973.9 | 973.9 | 0.98 | 1.46 | 146 | 599 | 579 | 86.4 | 946.6, 985.5, 973.9 |
| async | 4 | 513.5 | 2054.0 | 1.84 | 3.10 | 228 | 604 | 548 | 90.5 | 510.5, 513.5, 516.2 |
| async | 16 | 145.4 | 2325.9 | 6.72 | 10.75 | 409 | 588 | 586 | 98.6 | 145.4, 145.4, 145.8 |
| write | 1 | 670.0 | 670.0 | 1.41 | 2.69 | 248 | 626 | 545 | 95.6 | 686.7, 670.0, 549.5 |
| write | 4 | 395.7 | 1582.6 | 2.30 | 4.67 | 147 | 622 | 555 | 96.5 | 396.7, 395.7, 322.8 |
| write | 16 | 126.3 | 2020.7 | 7.55 | 14.59 | 294 | 625 | 547 | 97.8 | 126.3, 129.4, 116.3 |
| sync | 1 | 4.2 | 4.2 | 227.33 | 503.81 | 6870 | 4215 | 234 | 122.2 | 3.4, 4.3, 4.2 |
| sync | 4 | 4.1 | 16.4 | 227.33 | 512.00 | 7053 | 4102 | 239 | 124.9 | 3.2, 4.2, 4.1 |
| sync | 16 | 3.4 | 53.8 | 233.47 | 1277.95 | 15776 | 3363 | 284 | 121.4 | 2.9, 3.5, 3.4 |

#### E3: group commit, Sync level, 1-key transactions, committers flat out

| group delay (us) | committers | commits/s (k) | syncs/s | commits per sync | avg sync (us) | commit p50 (us) | commit p99 (us) | aborted | runs: commits/s (k) |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 1 | 4.5 | 4498 | 1.0 | 221 | 214 | 461 | 0 | 4.5, 4.5 |
| 0 | 2 | 6.6 | 4422 | 1.5 | 224 | 215 | 659 | 0 | 6.6, 6.7 |
| 0 | 4 | 4.3 | 1741 | 2.5 | 681 | 977 | 3965 | 0 | 2.5, 6.1 |
| 0 | 8 | 4.4 | 991 | 4.5 | 996 | 1540 | 5276 | 0 | 4.4, 4.5 |
| 0 | 16 | 22.3 | 2331 | 9.6 | 697 | 1092 | 2900 | 4 | 36.5, 8.1 |
| 0 | 32 | 41.7 | 1874 | 22.3 | 869 | 1112 | 2728 | 16 | 68.2, 15.2 |
| 100 | 1 | 2.1 | 2094 | 1.0 | 326 | 369 | 1769 | 0 | 2.0, 2.2 |
| 100 | 2 | 3.5 | 1774 | 2.0 | 579 | 1004 | 1892 | 0 | 1.8, 5.3 |
| 100 | 4 | 6.9 | 1733 | 4.0 | 604 | 1004 | 1896 | 1 | 3.5, 10.3 |
| 100 | 8 | 13.1 | 1631 | 8.0 | 666 | 1012 | 1991 | 2 | 6.4, 19.7 |
| 100 | 16 | 25.0 | 1561 | 16.0 | 647 | 483 | 1937 | 6 | 13.5, 36.5 |
| 100 | 32 | 63.6 | 1986 | 32.0 | 330 | 573 | 631 | 23 | 63.4, 63.8 |
| 500 | 1 | 1.3 | 1297 | 1.0 | 220 | 762 | 1004 | 0 | 1.3, 1.3 |
| 500 | 2 | 2.6 | 1291 | 2.0 | 222 | 762 | 1016 | 0 | 2.6, 2.6 |
| 500 | 4 | 5.1 | 1275 | 4.0 | 231 | 766 | 1016 | 0 | 5.1, 5.1 |
| 500 | 8 | 9.9 | 1237 | 8.0 | 251 | 770 | 1032 | 0 | 9.9, 9.9 |
| 500 | 16 | 18.8 | 1176 | 16.0 | 282 | 791 | 1073 | 4 | 18.8, 18.9 |
| 500 | 32 | 33.6 | 1050 | 32.0 | 369 | 983 | 2073 | 14 | 32.2, 35.0 |

fdatasync probe before each run: min 228 us, median 432 us, max 1315 us over 120 runs

**Reading the numbers.**

- **Reads do not change.** 120.9M reads/s with no log, 119.1M with Async and 119.3M with Write, with the
  same p99 (160 ns): within 1.5%. (The Sync row reads faster, 124.8M, only because its writer is slow
  and so makes less copy-on-write work for the readers to share the machine with.)
- **At the D.1 write rate of 50,000 puts/s** Async and Write keep up exactly. The median put goes from
  0.96 us (no log) to 1.02 us (Async) and 1.49 us (Write), the p99 from 1.33 us to 1.39 us and 3.07 us.
  Write costs more than Async because it makes a `write()` system call on every commit.
- **Flat out, one committer, no readers**, single-key commits: 3.62M per second with no log, 2.84M with
  Async (22% less), 1.55M with Write (57% less), and about 4,400 with Sync. With 16 keys to a transaction
  the log costs less, since one record covers many keys: 363k, 298k (18% less), 252k (31% less). With 8
  readers running the picture is the same (1.08M, 0.97M, 0.67M).
- **Sync from one thread is bounded by the disk**: one commit per sync, so about 4,400 commits a second
  when a sync takes 226 us, and fewer in the periods when the disk was slower (the E1 median is 2,535).
  It cannot sustain 50,000 puts a second, which is why a single-threaded sync commit is the wrong fit for
  the feed.
- **Group commit works.** With a 500 us group delay the number of commits sharing each sync is
  exactly the number of committers (1, 2, 4, 8, 16, 32), throughput rises 26-fold from 1.3k to 33.6k
  commits a second, and the syncs per second *fall* from 1,297 to 1,050. With no delay the batching
  happens by itself as committers pile up behind a sync in progress (1.0, 1.5, 2.5, 4.5, 9.6 and 22.3
  commits per sync), and the throughput is as noisy as the disk (the runs at 16 committers gave 36.5k
  and 8.1k a second). **The delay costs its own length in latency**: for one committer the median commit
  goes from 0.21 ms with no delay to 0.76 ms with 500 us, and a delay helps only when there are other
  committers to wait for. A few transactions abort at high concurrency (first-committer-wins; up to 23
  in a run of about 300,000).
- **Replay is fast**: 1,000,000 records (66 MB) rebuild the store in 0.17 s, 5.9 million records a
  second, 385 MB/s (`partD/replay_bench.cpp`). Appending at the Write level costs 1.4 us a record
  against 0.33 us with no log.

## 4. Tests

| what | result |
|---|---|
| `wal_test`: round trip at every level (puts, overwrites, erases, heap-backed values, transactions, a refused transaction that must not be logged, index) | reopened state equals an oracle exactly |
| truncation: the file cut at every byte of its last 3 records and at 150 random places | **877 cuts**, each recovers exactly the complete records before the cut, and reports no valid records after it (a torn tail) |
| corruption: random bit flips anywhere in the records | **600 flips**, each stops replay at exactly the damaged record, leaves the state of the records before it, and reports the number of valid records it had to drop after it |
| sequence: a record removed, a record repeated | caught by the sequence numbers at every tried position |
| continue: recover from a torn tail, append, reopen | the new records are replayed, and the file is clean |
| group commit: 8 threads x 150 transactions at Sync | every commit present after reopen, syncs shared between commits |
| `crash_test`: a child process commits 8-key transactions flat out and is **killed with `SIGKILL`** at a random moment, 40 times per level, appending to the same log across kills | Write: 578,989 commits, **0 acknowledged commits lost**. Sync: 8,569 commits, **0 lost**. Async: lost acknowledged commits in 12 of 40 runs, at worst 4,993 commits (about 17 ms of work; an earlier run at a slow moment of the disk lost up to 16,535, about 60 ms), and 2 kills left a genuinely torn tail that recovery detected and dropped. In every run of every level the 8 keys came back whole, with one version |
| sanitizers | `make check`: AddressSanitizer and UBSan on the log tests, the transaction tests and the crash test; ThreadSanitizer on the log tests |

What these tests cannot show: **a power cut**. `kill -9` cannot take back what the kernel already has,
which is exactly why the Write level survives it, and we have no way to cut power or fail a disk here.
For power loss the evidence is the design (`fdatasync` before the acknowledgement, in a preallocated
file, with the directory entry synced) rather than a test. The truncation and bit-flip sweeps are the
stand-in for the torn and damaged writes a power cut can leave behind.

## 5. What is still missing before this could run on a real desk

- **No log compaction.** The log grows forever and restart time grows with it (385 MB/s of replay means
  a 100 GB log takes about four minutes). It needs periodic checkpoints (a snapshot of the table written
  out), then truncation or rotation of the log segments before them.
- **One machine, no replication.** If the disk or the box dies the data and the service are gone. A desk
  needs a replicated log with automatic failover, which is much of what TigerBeetle actually is.
- **Detection without repair.** A damaged record in the middle of the log makes us drop it *and every
  acknowledged commit after it*. We now report how many valid records that threw away, but a production
  system should refuse to start and repair from a replica or a second copy of the log, and should keep
  redundant headers and checksum every sector, not just every record.
- **Power loss and disk faults are untested**: no fault-injection rig, no test of lying or reordering
  disks, no `ENOSPC` handling beyond failing the log, no scrubbing of data at rest.
- **The first sync of a failing disk may already have lost data**; we stop but cannot recover from it.
- **Latency is not yet deterministic.** The writer allocates and frees memory on the hot path, the
  reclamation pass and the log's `posix_fallocate` can take milliseconds now and then, and nothing is
  pinned, isolated or prefaulted. A desk would pin and isolate cores, preallocate everything, and
  measure the tail under production load.
- **Operations**: no metrics or alerting, no tooling to inspect or repair a log, no schema or format
  versioning beyond one version byte, no access control, no tested restore procedure.

## 6. Files

`wal.h`, `wal.cpp`, `kvstore.h`, `kvstore.cpp`, `join.h`, `wal_test.cpp`, `crash_test.cpp`,
`kvstore_test.cpp`, `txn_test.cpp`, `stress_test.cpp`, `rcu.cpp`, `bench_wal.sh`, `Makefile` (`make test`,
`make check`), `PART_E_DIFF.patch`, `results/bench_wal.txt` (raw benchmark rows), `results/tests.txt`.
The benchmark harness is `../partD/bench.cpp` (`bench_wal`) and `../partD/replay_bench.cpp`.
