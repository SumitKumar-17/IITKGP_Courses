#!/usr/bin/env python3
"""Tables for the Part E write-up, from partE/results/bench_wal.txt."""
import collections
import re
import statistics as st

R = "partE/results"


def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def med(xs):
    return st.median(xs)


rows = [kv(l) for l in open(f"{R}/bench_wal.txt") if "result " in l]
by = collections.defaultdict(list)
for d in rows:
    if d["part"] == "E1":
        by[("E1", d["label"])].append(d)
    elif d["part"] == "E2":
        by[("E2", d["readers"], d["label"], int(d["txn"]))].append(d)
    elif d["part"] == "E3":
        by[("E3", int(d["delay"]), int(d["committers"]))].append(d)


def f(rs, k):
    return [float(r[k]) for r in rs]


print("### E1: the D.1 setup, 8 readers, writer asked for 50,000 puts/s\n")
print("| log | reads/s (M) | read p99 (ns) | read p999 (ns) | writer puts/s | put p50 (us) | put p99 (us) | put max (us) | syncs/s | avg sync (us) |")
print("|---|---|---|---|---|---|---|---|---|---|")
for mode in ("none", "async", "write", "sync"):
    rs = by.get(("E1", mode))
    if rs:
        print(f"| {mode} | {med(f(rs, 'ops_per_s')) / 1e6:.1f} | {med(f(rs, 'p99_ns')):.0f} | {med(f(rs, 'p999_ns')):.0f} | "
              f"{med(f(rs, 'writer_ops_per_s')):.0f} | {med(f(rs, 'writer_p50_ns')) / 1000:.2f} | "
              f"{med(f(rs, 'writer_p99_ns')) / 1000:.2f} | {med(f(rs, 'writer_max_ns')) / 1000:.0f} | "
              f"{med([float(r['wal_syncs']) / float(r['secs']) for r in rs]):.0f} | {med(f(rs, 'wal_sync_avg_us')):.0f} |")

for readers in ("0", "8"):
    print(f"\n### E2: one committer flat out, {readers} readers running\n")
    print("| log | keys per txn | commits/s (k) | keys/s (k) | commit p50 (us) | commit p99 (us) | commit max (us) | syncs/s | avg sync (us) | reader reads/s (M) | runs: commits/s (k) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for mode in ("none", "async", "write", "sync"):
        for k in (1, 4, 16):
            rs = by.get(("E2", readers, mode, k))
            if rs:
                print(f"| {mode} | {k} | {med(f(rs, 'writer_ops_per_s')) / 1000:.1f} | {med(f(rs, 'writer_keys_per_s')) / 1000:.1f} | "
                      f"{med(f(rs, 'writer_p50_ns')) / 1000:.2f} | {med(f(rs, 'writer_p99_ns')) / 1000:.2f} | "
                      f"{med(f(rs, 'writer_max_ns')) / 1000:.0f} | "
                      f"{med([float(r['wal_syncs']) / float(r['secs']) for r in rs]):.0f} | {med(f(rs, 'wal_sync_avg_us')):.0f} | "
                      f"{med(f(rs, 'ops_per_s')) / 1e6:.1f} | {', '.join(f'{x / 1000:.1f}' for x in f(rs, 'writer_ops_per_s'))} |")

print("\n### E3: group commit, Sync level, 1-key transactions, committers flat out\n")
print("| group delay (us) | committers | commits/s (k) | syncs/s | commits per sync | avg sync (us) | commit p50 (us) | commit p99 (us) | aborted | runs: commits/s (k) |")
print("|---|---|---|---|---|---|---|---|---|---|")
for delay in (0, 100, 500):
    for c in (1, 2, 4, 8, 16, 32):
        rs = by.get(("E3", delay, c))
        if rs:
            cps = med(f(rs, "writer_ops_per_s"))
            sps = med([float(r["wal_syncs"]) / float(r["secs"]) for r in rs])
            print(f"| {delay} | {c} | {cps / 1000:.1f} | {sps:.0f} | {cps / sps if sps else 0:.1f} | "
                  f"{med(f(rs, 'wal_sync_avg_us')):.0f} | {med(f(rs, 'writer_p50_ns')) / 1000:.0f} | "
                  f"{med(f(rs, 'writer_p99_ns')) / 1000:.0f} | {med(f(rs, 'aborted')):.0f} | "
                  f"{', '.join(f'{x / 1000:.1f}' for x in f(rs, 'writer_ops_per_s'))} |")

probes = [float(r["probe_us"]) for r in rows if "probe_us" in r]
if probes:
    print(f"\nfdatasync probe before each run: min {min(probes):.0f} us, median {med(probes):.0f} us, max {max(probes):.0f} us over {len(probes)} runs")
