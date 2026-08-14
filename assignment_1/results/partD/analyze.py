#!/usr/bin/env python3
"""Part D analysis: pull GC-instruction share, sweepstep costs, and
promotion counts out of the callgrind_annotate outputs produced by
run_partD.sh. Run from results/partD/ on the VM."""
import re
import sys
import glob

GCFUNCS = set(l.strip() for l in open("gcfuncs.txt") if l.strip())


def totals(annotate_path):
    total = None
    gc_total = 0
    per_fn = {}
    for line in open(annotate_path):
        m = re.match(r"\s*([\d,]+)\s*\(\s*([\d.]+)%\)\s+PROGRAM TOTALS", line)
        if m:
            total = int(m.group(1).replace(",", ""))
            continue
        m = re.match(r"\s*([\d,]+)\s*\([\s\d.]+%\)\s+\?\?\?:([A-Za-z0-9_]+)", line)
        if m:
            ir = int(m.group(1).replace(",", ""))
            fn = m.group(2)
            per_fn[fn] = per_fn.get(fn, 0) + ir
            if fn in GCFUNCS:
                gc_total += ir
    return total, gc_total, per_fn


def main():
    for path in sorted(glob.glob("*.annotate.txt")):
        total, gc_total, per_fn = totals(path)
        if total is None:
            print(f"{path}: NO TOTAL FOUND")
            continue
        pct = 100.0 * gc_total / total
        sweepstep = per_fn.get("sweepstep", 0)
        sweeplist = per_fn.get("sweeplist", 0)
        freeobj = per_fn.get("freeobj", 0)
        print(f"{path}\ttotal_ir={total}\tgc_ir={gc_total}\tgc_pct={pct:.2f}\t"
              f"sweeplist_ir={sweeplist}\tfreeobj_ir={freeobj}")


if __name__ == "__main__":
    main()
