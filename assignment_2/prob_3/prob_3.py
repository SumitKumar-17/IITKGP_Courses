"""
prob_3.py  --  CACTI sweep: Access Time vs Cache Size (similar to Fig 2.8)

5 cache sizes x 4 associativities = 20 configurations
Run as:  python3 prob_3.py > prob_3.log
"""

import subprocess
import os
import re
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

# ── paths ────────────────────────────────────────────────────────────────────
CACTI      = '/home/rtx3060/cacti/cacti'
CACTI_DIR  = '/home/rtx3060/cacti'           # CACTI always writes cache.cfg.out here
CACTI_CFG  = os.path.join(CACTI_DIR, 'cache.cfg')
CACTI_OUT  = os.path.join(CACTI_DIR, 'cache.cfg.out')
BASE_CFG   = '/home/rtx3060/assignment_2/cache.cfg'
WORK_DIR   = os.path.dirname(os.path.abspath(__file__))

# ── sweep parameters (5 x 4 = 20 configurations) ─────────────────────────────
CACHE_SIZES  = [16384, 32768, 65536, 131072, 262144]   # 16KB 32KB 64KB 128KB 256KB
ASSOC_VALUES = [1, 2, 4, 8]
BLOCK_SIZE   = 64                                       # bytes (same as base cfg)

# ── read base config ──────────────────────────────────────────────────────────
with open(BASE_CFG) as f:
    BASE = f.read()


def make_cfg(size_bytes, assoc):
    """Return a CACTI config string with the given size and associativity."""
    cfg = re.sub(r'^-size \(bytes\) \d+',
                 f'-size (bytes) {size_bytes}',
                 BASE, flags=re.MULTILINE)
    cfg = re.sub(r'^-associativity \d+',
                 f'-associativity {assoc}',
                 cfg, flags=re.MULTILINE)
    return cfg


def run_cacti(size_bytes, assoc):
    """
    Save cfg to WORK_DIR (named cfg file) AND copy to CACTI_CFG, then run CACTI.
    CACTI APPENDS one result line to cache.cfg.out per run.
    Returns path to .out file; caller should read the last line.
    """
    size_kb = size_bytes // 1024
    cfg_content = make_cfg(size_bytes, assoc)

    # Save named cfg file in working directory (required by assignment)
    named_cfg = os.path.join(WORK_DIR, f'prob3_{size_kb}KB_{assoc}way.cfg')
    with open(named_cfg, 'w') as f:
        f.write(cfg_content)

    # Copy to CACTI dir as cache.cfg so CACTI writes to cache.cfg.out
    with open(CACTI_CFG, 'w') as f:
        f.write(cfg_content)

    subprocess.run(
        [CACTI, '-infile', CACTI_CFG],
        cwd=CACTI_DIR,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    return CACTI_OUT


def parse_last_result(out_path):
    """
    CACTI appends one CSV data line per run to cache.cfg.out.
    Read the LAST non-empty line and extract access time (column index 5).
    CSV format: Tech node, Capacity, Banks, Assoc, Output width,
                Access time (ns), Random cycle time (ns), ...
    """
    try:
        with open(out_path) as f:
            lines = [l.strip() for l in f if l.strip()]
        # last line is the most recent run's result
        data_line = lines[-1]
        fields = [x.strip() for x in data_line.split(',')]
        access_time_ns = float(fields[5])
        return access_time_ns
    except Exception as e:
        print(f'  [parse error] {out_path}: {e}', file=sys.stderr)
        return None


# ── main sweep ────────────────────────────────────────────────────────────────
print('=' * 70)
print('prob_3.py  --  CACTI sweep: Access Time (ns) vs Cache Size')
print(f'Configurations: {len(CACHE_SIZES)} sizes x {len(ASSOC_VALUES)} assoc = '
      f'{len(CACHE_SIZES)*len(ASSOC_VALUES)} total')
print('=' * 70)
print()

# results[assoc][size_kb] = access_time_ns
results = {a: {} for a in ASSOC_VALUES}

# Delete stale .out file so we start fresh (CACTI appends per run)
if os.path.exists(CACTI_OUT):
    os.remove(CACTI_OUT)

cfg_idx = 0
for size in CACHE_SIZES:
    size_kb = size // 1024
    for assoc in ASSOC_VALUES:
        cfg_idx += 1

        # run CACTI (writes cfg to CACTI dir, reads output from there)
        out_path = run_cacti(size, assoc)

        # parse result — read last appended line
        access_time = parse_last_result(out_path)

        label = f'{size_kb}KB, {assoc}-way'
        if access_time is not None:
            results[assoc][size_kb] = access_time
            print(f'[{cfg_idx:02d}/20]  {label:<20s}  Access time = {access_time:.4f} ns')
        else:
            results[assoc][size_kb] = float('nan')
            print(f'[{cfg_idx:02d}/20]  {label:<20s}  Access time = ERROR')

print()
print('=' * 70)
print('Summary Table')
print('=' * 70)
sizes_kb = [s // 1024 for s in CACHE_SIZES]
header = f"{'Cache Size':<12}" + ''.join(f"{f'{a}-way':>12}" for a in ASSOC_VALUES)
print(header)
print('-' * (12 + 12 * len(ASSOC_VALUES)))
for skb in sizes_kb:
    row = f"{f'{skb} KB':<12}"
    for a in ASSOC_VALUES:
        val = results[a].get(skb, float('nan'))
        row += f'{val:>11.4f} '
    print(row)
print()

# ── plot (grouped bar chart, similar to Fig 2.8) ─────────────────────────────
fig, ax = plt.subplots(figsize=(9, 5))

x      = np.arange(len(sizes_kb))
n_bars = len(ASSOC_VALUES)
width  = 0.18
colors = ['#111111', '#888888', '#aaaaaa', '#cccccc']
labels = ['1-way', '2-way', '4-way', '8-way']

for i, (assoc, color, label) in enumerate(zip(ASSOC_VALUES, colors, labels)):
    vals = [results[assoc].get(skb, 0) for skb in sizes_kb]
    offset = (i - (n_bars - 1) / 2) * width
    bars = ax.bar(x + offset, vals, width, label=label, color=color,
                  edgecolor='white', linewidth=0.5)

ax.set_xlabel('Cache size', fontsize=12)
ax.set_ylabel('Access time (ns)', fontsize=12)
ax.set_title('Access Time vs Cache Size for Different Associativities\n'
             '(CACTI 7.0, 90 nm, 64-byte blocks)', fontsize=11)
ax.set_xticks(x)
ax.set_xticklabels([f'{s} KB' for s in sizes_kb])
ax.legend(title='Associativity', fontsize=10)
ax.yaxis.grid(True, linestyle='--', alpha=0.5)
ax.set_axisbelow(True)

plt.tight_layout()
plot_path = os.path.join(WORK_DIR, 'prob_3_plot.png')
plt.savefig(plot_path, dpi=150)
print(f'Plot saved to {plot_path}')
