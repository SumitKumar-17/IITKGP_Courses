#!/usr/bin/env python3
"""Tables for the Part D report, straight from partD/results/.

usage: summarize_d.py d1|d2|d3
"""
import collections
import csv
import re
import statistics as st
import sys

R = "partD/results"


def med(xs):
    return st.median(xs) if xs else float("nan")


def kv(line):
    return {k: v for k, v in re.findall(r"(\w+)=(\S+)", line)}


def d1():
    rows = collections.defaultdict(list)
    for r in csv.DictReader(open(f"{R}/d1.csv")):
        rows[(r["config"], int(r["readers"]))].append(r)
    cfgs = ["single_thread_nowriter", "global_lock", "rwlock", "rcu", "rcu_partC"]
    print("| config | readers | reads/s (M) | p50 (us) | p99 (us) | p999 (us) | runs (M reads/s) |")
    print("|---|---|---|---|---|---|---|")
    thr = {}
    for c in cfgs:
        for n in (1, 2, 4, 8):
            rs = rows.get((c, n))
            if not rs:
                continue
            rps = [float(r["rps"]) for r in rs]
            thr[(c, n)] = med(rps)
            print(f"| {c} | {n} | {med(rps) / 1e6:.2f} | {med([float(r['p50_us']) for r in rs]):.3f} | "
                  f"{med([float(r['p99_us']) for r in rs]):.3f} | {med([float(r['p999_us']) for r in rs]):.3f} | "
                  f"{', '.join(f'{x / 1e6:.1f}' for x in rps)} |")
    print("\nScaling stops (first reader count whose next doubling gains under 10%):")
    for c in cfgs:
        seq = [(n, thr.get((c, n))) for n in (1, 2, 4, 8) if thr.get((c, n))]
        stop = None
        for (n, a), (m, b) in zip(seq, seq[1:]):
            if b < a * 1.10:
                stop = n
                break
        best = max(seq, key=lambda x: x[1])
        print(f"- {c}: {'keeps scaling to 8' if stop is None else f'stops at {stop}'}; best {best[1] / 1e6:.2f}M at {best[0]}")


def parse_result_file(path, prefix_keys=()):
    out = []
    for line in open(path):
        if "result " not in line:
            continue
        d = kv(line)
        out.append(d)
    return out


def d1b():
    runs = collections.defaultdict(list)
    for line in open(f"{R}/d1b.txt"):
        if "result " in line:
            d = kv(line)
            runs[(d["store"], int(d["readers"]))].append(d)
    print("| store | readers | reads/s (M) | p50 (ns) | p99 (ns) | p999 (ns) | max (us) | runs (M reads/s) |")
    print("|---|---|---|---|---|---|---|---|")
    for store in ("single", "global_lock", "rwlock", "rcu"):
        for n in (1, 2, 4, 8):
            rs = runs.get((store, n))
            if not rs:
                continue
            f = lambda k: [float(r[k]) for r in rs]
            print(f"| {store} | {n} | {med(f('ops_per_s')) / 1e6:.1f} | {med(f('p50_ns')):.0f} | {med(f('p99_ns')):.0f} | "
                  f"{med(f('p999_ns')):.0f} | {med(f('max_ns')) / 1000:.0f} | {', '.join(f'{x / 1e6:.0f}' for x in f('ops_per_s'))} |")


def d2():
    runs = collections.defaultdict(list)
    for d in parse_result_file(f"{R}/d2.txt"):
        runs[(d["scenario"], d["label"])].append(d)
    for sc in ("steady", "bursty"):
        print(f"\n### {sc}\n")
        print("| setting | reads/s (M) | p99 (ns) | p999 (ns) | longest reader stall (us) | worst across runs (us) | calls >100us | "
              "writer puts/s (k) | backlog peak | longest reclaim pass (us) | STW pause (us) | peak RSS (MB) |")
        print("|---|---|---|---|---|---|---|---|---|---|---|---|")
        order = ["batch1", "batch8", "batch64", "batch256", "batch1024", "batch4096", "none", "stw4096", "stw16384"]
        for lab in order:
            rs = runs.get((sc, lab))
            if not rs:
                continue
            f = lambda k: [float(r.get(k, 0)) for r in rs]
            print(f"| {lab} | {med(f('ops_per_s')) / 1e6:.1f} | {med(f('p99_ns')):.0f} | {med(f('p999_ns')):.0f} | "
                  f"{med(f('max_ns')) / 1000:.0f} | {max(f('max_ns')) / 1000:.0f} | {med(f('gt100us')):.0f} | "
                  f"{med(f('writer_ops_per_s')) / 1000:.0f} | {med(f('backlog_peak')):.0f} | {med(f('max_pass_us')):.0f} | "
                  f"{med(f('stw_max_us')):.0f} | {med(f('rss_peak_mb')):.0f} |")


def d3():
    joins = collections.defaultdict(list)
    commits = collections.defaultdict(list)
    for line in open(f"{R}/d3.txt"):
        if "result " not in line:
            continue
        d = kv(line)
        if d.get("part") == "join":
            joins[(d["outer"], d["writer"])].append(d)
        elif d.get("part") == "commit":
            commits[(d["store"], d["readers"], d["txn"])].append(d)
    print("### join, 8 readers\n")
    print("| outer rows per join | writer | joins/s | outer rows/s (k) | p50 (us) | p99 (us) | p999 (us) | max (us) |")
    print("|---|---|---|---|---|---|---|---|")
    for (outer, w), rs in sorted(joins.items(), key=lambda x: (int(x[0][0]), -int(x[0][1]))):
        f = lambda k: [float(r[k]) for r in rs]
        print(f"| {outer} | {'on (50k puts/s)' if w != '0' else 'paused'} | {med(f('ops_per_s')):.0f} | "
              f"{med(f('rows_per_s')) / 1000:.0f} | {med(f('p50_ns')) / 1000:.1f} | {med(f('p99_ns')) / 1000:.1f} | "
              f"{med(f('p999_ns')) / 1000:.1f} | {med(f('max_ns')) / 1000:.0f} |")
    print("\n### commits, writer flat out\n")
    print("| store | readers | keys/txn | commits/s (k) | keys/s (k) | commit p50 (us) | commit p99 (us) | commit max (us) | reader reads/s (M) |")
    print("|---|---|---|---|---|---|---|---|---|")
    for (store, readers, k), rs in sorted(commits.items(), key=lambda x: (x[0][0], int(x[0][1]), int(x[0][2]))):
        f = lambda key: [float(r[key]) for r in rs]
        print(f"| {store} | {readers} | {k} | {med(f('writer_ops_per_s')) / 1000:.0f} | {med(f('writer_keys_per_s')) / 1000:.0f} | "
              f"{med(f('writer_p50_ns')) / 1000:.2f} | {med(f('writer_p99_ns')) / 1000:.2f} | {med(f('writer_max_ns')) / 1000:.0f} | "
              f"{med(f('ops_per_s')) / 1e6:.1f} |")
    print("\n### one transaction of N keys\n")
    store = None
    rows = collections.defaultdict(dict)
    for line in open(f"{R}/d3.txt"):
        m = re.match(r"part=bigtxn store=(\w+)", line)
        if m:
            store = m.group(1)
            continue
        m = re.match(r"(\d+),([\d.]+),(\d+),([\d.]+)$", line.strip())
        if m and store:
            rows[int(m.group(1))][store] = (float(m.group(2)), int(m.group(3)), float(m.group(4)))
    print("| keys | rcu ms | rcu ns/key | rcu extra heap MB | rwlock ms | rwlock ns/key |")
    print("|---|---|---|---|---|---|")
    for n in sorted(rows):
        r = rows[n].get("rcu", (0, 0, 0))
        w = rows[n].get("rwlock", (0, 0, 0))
        print(f"| {n} | {r[0]:.3f} | {r[1]} | {r[2]:.2f} | {w[0]:.3f} | {w[1]} |")


if __name__ == "__main__":
    {"d1": d1, "d1b": d1b, "d2": d2, "d3": d3}[sys.argv[1]]()
