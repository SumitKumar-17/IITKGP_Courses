#!/usr/bin/env python3
"""Where the RCU readers spend their time, by leaf function of each sampled stack."""
import collections
import re
import sys

D = "partD/results/profile"
CATS = [
    ("clock reads (the generator's per-call timing)", re.compile(r"vdso|clock_gettime|steady_clock")),
    ("latency vector (push_back, page faults)", re.compile(r"emplace_back|vector|clear_page|page_fault|memcg|alloc_pages|do_anonymous|handle_mm|asm_exc")),
    ("random number generator", re.compile(r"mersenne|_M_gen_rand")),
    ("RCU get: epoch announce, trie walk, copy-out", re.compile(r"KVStore::|find|ReadGuard|my_slot|copy_out")),
]
for r in (1, 8):
    c = collections.Counter()
    for line in open(f"{D}/rcu_r{r}.folded"):
        stack, n = line.rsplit(" ", 1)
        leaf = stack.split(";")[-1]
        for name, rx in CATS:
            if rx.search(leaf):
                c[name] += int(n)
                break
        else:
            c["other"] += int(n)
    tot = sum(c.values())
    print(f"\nrcu, {r} reader(s)")
    for k, v in c.most_common():
        print(f"  {100 * v / tot:5.1f}%  {k}")
    top = collections.Counter()
    for line in open(f"{D}/rcu_r{r}.folded"):
        stack, n = line.rsplit(" ", 1)
        top[stack.split(";")[-1][:70]] += int(n)
    for f, v in top.most_common(8):
        print(f"    {100 * v / tot:5.1f}% {f}")
