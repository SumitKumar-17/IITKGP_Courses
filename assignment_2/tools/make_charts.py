#!/usr/bin/env python3
"""Charts for the Part D report, drawn from partD/results/."""
import collections
import csv
import re
import statistics as st

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

R = "partD/results"
COL = {"single": "#555555", "global_lock": "#c0392b", "rwlock": "#e67e22", "rcu": "#1f77b4"}


def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def d1_charts():
    gen = collections.defaultdict(list)
    for r in csv.DictReader(open(f"{R}/d1.csv")):
        name = {"single_thread_nowriter": "single", "rcu": "rcu"}.get(r["config"], r["config"])
        if r["config"] == "rcu_partC":
            continue
        gen[(name, int(r["readers"]))].append(float(r["rps"]) / 1e6)
    har = collections.defaultdict(list)
    for line in open(f"{R}/d1b.txt"):
        if "result " in line:
            d = kv(line)
            har[(d["store"], int(d["readers"]))].append(float(d["ops_per_s"]) / 1e6)

    fig, ax = plt.subplots(1, 2, figsize=(9.2, 3.5), sharey=False)
    for a, data, title in ((ax[0], gen, "provided generator"), (ax[1], har, "histogram harness")):
        for store in ("single", "global_lock", "rwlock", "rcu"):
            xs = [1, 2, 4, 8]
            ys = [st.median(data[(store, n)]) for n in xs]
            a.plot(xs, ys, marker="o", color=COL[store], label=store + (" (no writer)" if store == "single" else ""))
        a.set_xscale("log", base=2)
        a.set_xticks([1, 2, 4, 8])
        a.set_xticklabels(["1", "2", "4", "8"])
        a.set_xlabel("reader threads")
        a.set_ylabel("point gets per second (millions)")
        a.set_title(title, fontsize=10)
        a.grid(alpha=0.3)
    ax[0].legend(fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(f"{R}/d1_scaling.png", dpi=160)


def d2_chart():
    runs = collections.defaultdict(list)
    for line in open(f"{R}/d2.txt"):
        if "result " in line:
            d = kv(line)
            runs[(d["scenario"], d["label"])].append(d)
    labels = ["batch1", "batch8", "batch64", "batch256", "batch1024", "batch4096", "none", "stw4096", "stw16384"]
    names = ["1", "8", "64", "256", "1024", "4096", "no\nreclaim", "STW\n4096", "STW\n16384"]
    fig, ax = plt.subplots(1, 2, figsize=(9.2, 3.5))
    sc = "bursty"
    got = [(n, l) for n, l in zip(names, labels) if (sc, l) in runs]
    x = range(len(got))
    cols = ["#1f77b4"] * 6 + ["#7f8c8d", "#c0392b", "#c0392b"]
    stall = [st.median([float(r["max_ns"]) for r in runs[(sc, l)]]) / 1000 for _, l in got]
    ax[0].bar(x, stall, color=cols[: len(got)])
    ax[0].set_yscale("log")
    ax[0].set_ylabel("longest reader stall (us, log scale)")
    ax[0].set_title("reader stall, bursty workload", fontsize=10)
    wp = [st.median([float(r["max_pass_us"]) for r in runs[(sc, l)]]) for _, l in got]
    ax[1].bar(x, wp, color=cols[: len(got)])
    ax[1].set_yscale("log")
    ax[1].set_ylabel("longest reclamation pass (us, log scale)")
    ax[1].set_title("pause the reclaimer causes", fontsize=10)
    for a in ax:
        a.set_xticks(list(x))
        a.set_xticklabels([n for n, _ in got], fontsize=7)
        a.set_xlabel("reclaim batch (versions) or reference")
        a.grid(alpha=0.3, axis="y")
    fig.tight_layout()
    fig.savefig(f"{R}/d2_batch.png", dpi=160)


d1_charts()
d2_chart()
print("charts written")
