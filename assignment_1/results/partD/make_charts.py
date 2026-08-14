#!/usr/bin/env python3
"""Generates the Part D chart PNGs from the numbers already reported in
PART_D_REPORT.md. Categorical colors are the validated slots from the
dataviz reference palette (blue/orange/aqua), same across all charts."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e3e2dd"

plt.rcParams.update({
    "font.size": 11,
    "axes.edgecolor": GRID,
    "axes.labelcolor": INK,
    "text.color": INK,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.facecolor": "white",
    "axes.facecolor": "white",
})


def style_axes(ax):
    ax.grid(axis="y", color=GRID, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)


# ---------- D.1: GC% vs problem size, three configs ----------
sizes = [10_000, 50_000, 500_000, 1_000_000]  # m*n, log x-axis
labels = ["100x100", "500x100", "5000x100", "1000x1000\n(default)"]
full = [1.18, 1.35, 3.54, 5.90]
inc = [6.53, 7.21, 7.53, 7.63]
gen = [3.55, 4.13, 4.61, 4.75]

fig, ax = plt.subplots(figsize=(7.5, 4.6), dpi=150)
for y, c, name, marker in [(full, BLUE, "full", "o"), (inc, ORANGE, "incremental", "s"),
                           (gen, AQUA, "generational", "^")]:
    ax.plot(sizes, y, color=c, linewidth=2, marker=marker, markersize=6, label=name, zorder=3)
ax.set_xscale("log")
ax.set_xticks(sizes)
ax.set_xticklabels(labels, fontsize=9)
ax.set_ylabel("GC-attributable instructions (%)")
ax.set_xlabel("problem size (m x n table-pairs allocated)")
ax.set_title("D.1: GC instruction share vs. problem size", loc="left", fontsize=12, pad=12)
style_axes(ax)
ax.legend(frameon=False, loc="upper left")
fig.tight_layout()
fig.savefig("fig_d1_gcpercent.png")
plt.close(fig)

# ---------- D.1c: perf counters, small multiples ----------
configs = ["full", "incremental", "generational", "no-GC"]
colors = [BLUE, ORANGE, AQUA, MUTED]
metrics = {
    "Instructions (billions)": [3.815, 3.374, 3.246, 3.815],
    "Cycles (billions)": [1.047, 0.896, 0.779, 1.110],
    "IPC": [3.64, 3.77, 4.17, 3.44],
    "Branch misses (k)": [390.7, 426.2, 329.8, 78.1],
    "Cache misses (k)": [123.5, 169.1, 141.8, 5752.4],
    "Page faults": [1060, 1519, 1467, 47047],
}
fig, axes = plt.subplots(2, 3, figsize=(11, 6.4), dpi=150)
for ax, (title, vals) in zip(axes.flat, metrics.items()):
    bars = ax.bar(configs, vals, color=colors, width=0.62, zorder=3)
    ax.set_title(title, fontsize=10.5, loc="left")
    style_axes(ax)
    ax.tick_params(axis="x", labelsize=8.5, rotation=12)
    for b, v in zip(bars, vals):
        ax.annotate(f"{v:,.3g}", (b.get_x() + b.get_width() / 2, b.get_height()),
                    textcoords="offset points", xytext=(0, 3), ha="center", fontsize=8, color=MUTED)
fig.suptitle("D.1: perf counters at default parameters (pinned to one P-core)", x=0.01, ha="left", fontsize=12)
fig.tight_layout(rect=(0, 0, 1, 0.95))
fig.savefig("fig_d1_perf.png")
plt.close(fig)

# ---------- D.2: sweep-step ----------
steps = [5, 20, 50, 200]
gcpct = [8.13, 7.63, 7.53, 7.48]
calls = [602765, 151199, 60882, 15725]

fig, axes = plt.subplots(1, 2, figsize=(10, 4.2), dpi=150)
ax = axes[0]
ax.plot(steps, gcpct, color=BLUE, linewidth=2, marker="o", markersize=7, zorder=3)
for x, y in zip(steps, gcpct):
    ax.annotate(f"{y:.2f}%", (x, y), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=9)
ax.set_ylim(7.35, 8.35)
ax.set_xscale("log")
ax.set_xticks(steps)
ax.set_xticklabels([str(s) for s in steps])
ax.set_xlabel("gcsweepmax")
ax.set_ylabel("GC-attributable instructions (%)")
ax.set_title("Whole-program GC share", loc="left", fontsize=11)
style_axes(ax)

ax = axes[1]
bars = ax.bar([str(s) for s in steps], calls, color=BLUE, width=0.6, zorder=3)
ax.set_yscale("log")
ax.set_xlabel("gcsweepmax")
ax.set_ylabel("sweeplist() calls (log scale)")
ax.set_title("Sweep-step call count", loc="left", fontsize=11)
style_axes(ax)
for b, v in zip(bars, calls):
    ax.annotate(f"{v:,}", (b.get_x() + b.get_width() / 2, b.get_height()),
                textcoords="offset points", xytext=(0, 3), ha="center", fontsize=8.5, color=MUTED)
fig.suptitle("D.2: sweep-step tuning, testbench.lua under incremental GC", x=0.01, ha="left", fontsize=12)
fig.tight_layout(rect=(0, 0, 1, 0.93))
fig.savefig("fig_d2_sweepstep.png")
plt.close(fig)

# ---------- D.3: promotion benefit, three panels, independent y-scales ----------
panels = [
    ("testbench.lua\n(m=1000,n=1000)", 7.65, 7.63),
    ("promobench2.lua\n(short-lived frames)", 4.84, 3.23),
    ("promobench.lua\n(one long-lived frame)", 5.48, 93.35),
]
fig, axes = plt.subplots(1, 3, figsize=(11, 4.4), dpi=150)
for ax, (title, sv, cv) in zip(axes, panels):
    bars = ax.bar(["stock", "current"], [sv, cv], color=[MUTED, ORANGE], width=0.55, zorder=3)
    ax.set_title(title, fontsize=10.5, loc="center")
    ax.set_ylabel("GC-attributable instructions (%)")
    style_axes(ax)
    for b, v in zip(bars, [sv, cv]):
        ax.annotate(f"{v:.2f}%", (b.get_x() + b.get_width() / 2, b.get_height()),
                    textcoords="offset points", xytext=(0, 4), ha="center", fontsize=9.5)
fig.suptitle("D.3: GC share, stock vs. C.1+C.2 enabled (independent y-scales per panel)",
             x=0.01, ha="left", fontsize=11.5)
fig.tight_layout(rect=(0, 0, 1, 0.90))
fig.savefig("fig_d3_promotion.png")
plt.close(fig)

print("wrote fig_d1_gcpercent.png, fig_d1_perf.png, fig_d2_sweepstep.png, fig_d3_promotion.png")
