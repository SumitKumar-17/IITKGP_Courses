#!/usr/bin/env python3
"""Turns partA/results/profile/* into the tables used in the Part A report."""
import collections
import os
import re
import sys

D = sys.argv[1] if len(sys.argv) > 1 else "partA/results/profile"

KERNEL_WAIT = re.compile(r"native_queued_spin_lock_slowpath|futex|lll_lock_wait|lll_lock_wake|schedule|wake_up|queued_spin")
LOCK_USER = re.compile(r"pthread_mutex_(lock|unlock)|lll_mutex|pthread_rwlock|__lll_unlock")
TIMING = re.compile(r"vdso|clock_gettime|steady_clock")
STORE = re.compile(r"KVStore::")


def num(s):
    return int(s.replace(",", ""))


def parse_stat(path):
    m = {}
    for line in open(path):
        line = line.strip()
        r = re.match(r"([\d,\.]+)\s+(msec task-clock|cpu_core/(\S+?)/|context-switches)", line)
        if not r:
            continue
        name = r.group(3) or ("task-clock" if "task-clock" in r.group(2) else "context-switches")
        m[name] = float(r.group(1).replace(",", "")) if "." in r.group(1) else num(r.group(1))
    return m


def categorise(path):
    c = collections.Counter()
    for line in open(path):
        stack, n = line.rsplit(" ", 1)
        leaf = stack.split(";")[-1]
        n = int(n)
        if KERNEL_WAIT.search(leaf):
            c["kernel lock wait"] += n
        elif LOCK_USER.search(leaf):
            c["lock code (user)"] += n
        elif TIMING.search(leaf):
            c["clock reads"] += n
        elif STORE.search(leaf):
            c["table lookup"] += n
        else:
            c["other"] += n
    tot = sum(c.values())
    return {k: 100.0 * v / tot for k, v in c.items()}


print("| config | CPUs busy | IPC | cache-miss % | L1 miss / 1k instr | LLC miss / 1k instr | ctx switches/s |")
print("|---|---|---|---|---|---|---|")
for prog in ("global_lock", "rwlock"):
    for r in (1, 2, 4, 8):
        m = parse_stat(f"{D}/{prog}_r{r}.stat.txt")
        win = 3.0
        ins = m["instructions"]
        print(f"| {prog} r{r} | {m['task-clock'] / 1000 / win:.2f} | {ins / m['cycles']:.2f} | "
              f"{100 * m['cache-misses'] / m['cache-references']:.1f} | "
              f"{1000 * m['L1-dcache-load-misses'] / ins:.1f} | {1000 * m['LLC-load-misses'] / ins:.2f} | "
              f"{m['context-switches'] / win:.0f} |")

print()
print("| config | kernel lock wait | lock code (user) | clock reads | table lookup | other |")
print("|---|---|---|---|---|---|")
for prog in ("global_lock", "rwlock"):
    for r in (1, 2, 4, 8):
        p = categorise(f"{D}/{prog}_r{r}.folded")
        print(f"| {prog} r{r} | " + " | ".join(f"{p.get(k, 0):.1f}%" for k in
              ("kernel lock wait", "lock code (user)", "clock reads", "table lookup", "other")) + " |")
