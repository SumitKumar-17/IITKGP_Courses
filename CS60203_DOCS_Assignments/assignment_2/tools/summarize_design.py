#!/usr/bin/env python3
"""Median of the repeated design_bench runs, grouped by table size and writer rate."""
import collections
import re
import statistics as st
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "partB/results/design_sweep.txt"
head = None
rows = collections.OrderedDict()
for line in open(path):
    if line.startswith("#"):
        head = line[2:].strip()
        continue
    m = re.match(r"(\S+)\s+param=(\d+)\s+leaf=(\d+)\s+keys=\d+\s+load=\s*([\d.]+)s\s+put=\s*(\d+) ns\s+"
                 r"writer=\s*(\d+)/s\s+readers=\s*([\d.]+) Mops/s\s+peak_retired=(\d+)\s+heap=([\d.]+)MB", line)
    if m:
        name = m[1] if m[1] != "trie" else f"trie-{1 << int(m[2])}way/leaf{m[3]}"
        if m[1].startswith("dirseg"):
            name = f"{m[1]}/{m[2]}seg"
        rows.setdefault(head, collections.OrderedDict()).setdefault(name, []).append(
            [float(m[i]) for i in (4, 5, 6, 7, 9)])
for h, d in rows.items():
    print(f"\n{h}\n| design | load s | put ns | writer puts/s | readers Mops/s | heap MB |\n|---|---|---|---|---|---|")
    for n, v in d.items():
        c = [st.median(x) for x in zip(*v)]
        print(f"| {n} | {c[0]:.2f} | {c[1]:.0f} | {c[2]:.0f} | {c[3]:.0f} | {c[4]:.0f} |")
