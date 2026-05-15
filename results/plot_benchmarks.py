"""
Benchmark analysis and figure generation for the tinymole password cracker.
Run from the project root: uv run python3 results/plot_benchmarks.py
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.patches as mpatches
import numpy as np
import os
import math
from scipy import stats

OUT_DIR = "results/figures"
os.makedirs(OUT_DIR, exist_ok=True)

# Remove stale figures from previous numbering scheme
for stale in ["fig2_cache_behavior", "fig3_thread_scaling", "fig4_walltime", "fig5_memory"]:
    for ext in [".pdf", ".png"]:
        p = os.path.join(OUT_DIR, stale + ext)
        if os.path.exists(p):
            os.remove(p)

# ---------------------------------------------------------------------------
# Global style
# ---------------------------------------------------------------------------

plt.rcParams.update({
    "font.family":        "serif",
    "font.size":          10,
    "axes.labelsize":     11,
    "axes.titlesize":     11,
    "axes.spines.top":    False,
    "axes.spines.right":  False,
    "xtick.labelsize":    9,
    "ytick.labelsize":    9,
    "legend.fontsize":    9,
    "legend.framealpha":  0.9,
    "figure.dpi":         150,
    "savefig.dpi":        300,
    "savefig.bbox":       "tight",
    "axes.grid":          True,
    "grid.alpha":         0.25,
    "grid.linestyle":     "--",
})

# ColorBrewer Dark2 -- perceptually distinct, colorblind-safe
COLORS = {
    "tinyptr": "#1b9e77",
    "naive":   "#d95f02",
    "prob":    "#7570b3",
    "stdmap":  "#e7298a",
}
IMPLS       = ["tinyptr", "naive", "prob", "stdmap"]
AXIS_LABELS = ["TinyPtr", "Naive", "Prob", "StdMap"]
LEG_LABELS  = ["TinyPtr (bit-packed)", "Naive (full offset)",
               "Prob. (key-dep.)", "std::unordered_map"]

N_QUERIES = 2_000_000
x         = np.arange(len(IMPLS))
colors    = [COLORS[i] for i in IMPLS]

# ---------------------------------------------------------------------------
# Raw data
# ---------------------------------------------------------------------------

# Google Benchmark: 5 repetitions, full RockYou 14,344,391 entries
gbench = {
    "miss": {
        "tinyptr": {"mean": 44.515, "median": 44.389, "stddev": 0.750, "cv": 1.685},
        "naive":   {"mean": 42.740, "median": 43.035, "stddev": 0.734, "cv": 1.718},
        "prob":    {"mean": 43.847, "median": 43.766, "stddev": 0.488, "cv": 1.112},
        "stdmap":  {"mean": 305.859, "median": 306.209, "stddev": 1.639, "cv": 0.536},
    },
    "hit": {
        "tinyptr": {"mean": 13.939, "median": 13.905, "stddev": 0.173, "cv": 1.239},
        "naive":   {"mean": 27.738, "median": 27.752, "stddev": 0.255, "cv": 0.920},
        "prob":    {"mean": 33.366, "median": 33.538, "stddev": 0.534, "cv": 1.600},
        "stdmap":  {"mean": 392.488, "median": 390.634, "stddev": 5.181, "cv": 1.320},
    },
    "mixed": {
        "tinyptr": {"mean": 46.245, "median": 46.751, "stddev": 0.985, "cv": 2.130},
        "naive":   {"mean": 47.001, "median": 46.653, "stddev": 1.229, "cv": 2.616},
        "prob":    {"mean": 51.592, "median": 51.160, "stddev": 1.509, "cv": 2.926},
        "stdmap":  {"mean": 318.830, "median": 318.870, "stddev": 1.804, "cv": 0.566},
    },
}

# perf stat: 3 runs, 2M miss queries, full RockYou
perf_raw = {
    "tinyptr": {
        "cache_misses":    [85_487_179,  85_544_103,  86_580_262],
        "cache_refs":      [100_265_297, 99_962_956,  100_296_298],
        "instructions":    [56_046_774_245, 56_046_774_640, 56_046_773_272],
        "cycles":          [27_193_067_643, 26_939_566_361, 27_368_062_057],
        "branch_misses":   [23_922_679,  23_954_949,  23_937_905],
        "dtlb_misses":     [6_190_271,   6_197_264,   6_191_665],
        "l1d_replacement": [59_362_512,  58_887_598,  58_854_800],
    },
    "naive": {
        "cache_misses":    [88_101_829,  87_544_835,  86_019_067],
        "cache_refs":      [101_205_700, 101_271_120, 99_602_804],
        "instructions":    [56_173_186_762, 56_173_186_091, 56_173_185_270],
        "cycles":          [27_650_904_427, 27_940_002_146, 28_324_836_546],
        "branch_misses":   [23_605_900,  23_609_046,  23_630_635],
        "dtlb_misses":     [5_235_785,   5_232_137,   5_763_798],
        "l1d_replacement": [59_805_760,  59_730_885,  59_562_123],
    },
    "prob": {
        "cache_misses":    [209_531_836, 206_759_604, 208_562_361],
        "cache_refs":      [234_721_718, 231_813_384, 233_550_481],
        "instructions":    [57_971_328_247, 57_971_330_589, 57_971_328_499],
        "cycles":          [32_684_592_585, 31_336_683_282, 32_102_622_687],
        "branch_misses":   [38_909_128,  38_983_202,  38_980_374],
        "dtlb_misses":     [22_814_144,  22_890_154,  22_817_242],
        "l1d_replacement": [144_995_432, 144_473_497, 145_103_905],
    },
    "stdmap": {
        "cache_misses":    [236_790_447, 233_868_864, 235_881_877],
        "cache_refs":      [282_457_164, 277_017_168, 279_373_459],
        "instructions":    [68_928_251_992, 68_928_255_403, 68_928_254_641],
        "cycles":          [45_283_525_181, 43_766_835_380, 43_592_400_163],
        "branch_misses":   [26_386_922,  26_428_749,  26_449_160],
        "dtlb_misses":     [32_504_794,  32_465_111,  32_481_946],
        "l1d_replacement": [167_371_128, 163_974_121, 162_304_198],
    },
}

perf = {}
for impl, d in perf_raw.items():
    avg = {k: sum(v) / len(v) for k, v in d.items()}
    avg["misses_per_lookup"]      = avg["cache_misses"]    / N_QUERIES
    avg["ipc"]                    = avg["instructions"]    / avg["cycles"]
    avg["miss_rate_pct"]          = 100.0 * avg["cache_misses"] / avg["cache_refs"]
    avg["l1d_per_lookup"]         = avg["l1d_replacement"] / N_QUERIES
    avg["dtlb_per_lookup"]        = avg["dtlb_misses"]     / N_QUERIES
    avg["branch_miss_per_lookup"] = avg["branch_misses"]   / N_QUERIES
    perf[impl] = avg

# RDTSC percentile latency: 2M samples, 500K warmup, miss workload
latency = {
    "tinyptr": {"mean": 116.321, "stddev": 131.757, "p50":  91.513,
                "p95": 186.334, "p99": 424.488, "p999": 1873.259, "max": 15793.154},
    "naive":   {"mean": 114.667, "stddev": 122.385, "p50":  93.718,
                "p95": 191.846, "p99": 417.321, "p999":  767.937, "max": 40734.849},
    "prob":    {"mean": 111.635, "stddev":  82.141, "p50":  92.064,
                "p95": 183.577, "p99": 405.744, "p999":  571.680, "max": 18117.914},
    "stdmap":  {"mean": 425.866, "stddev": 735.963, "p50": 335.731,
                "p95": 868.271, "p99": 5079.521, "p999": 8490.309, "max": 45883.281},
}

# cracker_bench: 3 runs per impl/thread count, mid-list target (entry ~7M)
cracker = {
    "tinyptr": {1: [3.06, 3.04, 2.53], 2: [3.21, 3.33, 2.94],
                4: [3.61, 3.78, 3.44], 8: [3.67, 3.67, 3.78]},
    "naive":   {1: [2.62, 2.63, 2.52], 2: [3.17, 3.39, 3.32],
                4: [3.52, 3.77, 3.73], 8: [3.69, 3.76, 3.33]},
    "prob":    {1: [3.04, 2.63, 3.17], 2: [3.34, 3.25, 3.43],
                4: [3.75, 3.70, 3.64], 8: [4.13, 4.02, 4.01]},
    "stdmap":  {1: [2.52, 3.07, 3.03], 2: [3.15, 3.14, 3.06],
                4: [3.39, 3.36, 3.34], 8: [3.46, 3.53, 3.45]},
}

# Hyperfine: 5 runs, 2 warmups, 4 threads, mid-list target
hyperfine = {
    "tinyptr": {"mean":  9.226, "stddev": 0.518, "min":  8.771, "max": 10.021},
    "naive":   {"mean": 10.317, "stddev": 0.266, "min":  9.983, "max": 10.596},
    "prob":    {"mean": 11.501, "stddev": 0.521, "min": 11.038, "max": 12.355},
    "stdmap":  {"mean": 17.177, "stddev": 0.272, "min": 16.715, "max": 17.375},
}

# Peak RSS from /usr/bin/time -v, 14,344,391 entries loaded
rss_kb = {
    "tinyptr": 695_868,
    "naive":   695_948,
    "prob":    1_222_456,
    "stdmap":  1_958_288,
}

# ---------------------------------------------------------------------------
# Statistical helpers
# ---------------------------------------------------------------------------

def welch_t_test(a_mean, a_std, a_n, b_mean, b_std, b_n):
    se = math.sqrt(a_std**2 / a_n + b_std**2 / b_n)
    if se == 0:
        return 0.0, 1.0
    t   = (a_mean - b_mean) / se
    num = (a_std**2 / a_n + b_std**2 / b_n) ** 2
    den = (a_std**2 / a_n)**2 / (a_n - 1) + (b_std**2 / b_n)**2 / (b_n - 1)
    df  = num / den if den > 0 else 1
    p   = 2 * stats.t.sf(abs(t), df)
    return t, p

def cohens_d(a_mean, a_std, b_mean, b_std):
    pooled = math.sqrt((a_std**2 + b_std**2) / 2)
    return abs(a_mean - b_mean) / pooled if pooled > 0 else 0.0

def ci_95(mean, std, n):
    t_crit = stats.t.ppf(0.975, df=n - 1)
    margin = t_crit * std / math.sqrt(n)
    return mean - margin, mean + margin

def sig_label(p):
    if p < 0.001: return "***"
    if p < 0.01:  return "**"
    if p < 0.05:  return "*"
    return "ns"

def add_sig_bracket(ax, x1, x2, y_top, p, label_offset=0.5):
    label = sig_label(p)
    h = y_top * 0.018
    ax.plot([x1, x1, x2, x2], [y_top, y_top + h, y_top + h, y_top],
            lw=1.0, color="#444444", clip_on=False)
    ax.text((x1 + x2) / 2, y_top + h + label_offset,
            label, ha="center", va="bottom", fontsize=7.5, color="#444444")

def save(fig, name):
    fig.savefig(f"{OUT_DIR}/{name}.pdf")
    fig.savefig(f"{OUT_DIR}/{name}.png")
    plt.close(fig)

# ---------------------------------------------------------------------------
# Figure 1: Miss lookup latency -- log panel + linear zoom
# ---------------------------------------------------------------------------

fig, (ax_log, ax_zoom) = plt.subplots(1, 2, figsize=(9.5, 4.0),
                                       gridspec_kw={"width_ratios": [1, 3]})

means_miss = [gbench["miss"][i]["mean"]   for i in IMPLS]
stds_miss  = [gbench["miss"][i]["stddev"] for i in IMPLS]
ci_lo = [m - ci_95(m, s, 5)[0] for m, s in zip(means_miss, stds_miss)]
ci_hi = [ci_95(m, s, 5)[1] - m for m, s in zip(means_miss, stds_miss)]

# Log-scale panel: all 4 implementations
bars = ax_log.bar(x, means_miss, color=colors, edgecolor="white", linewidth=0.5)
ax_log.errorbar(x, means_miss, yerr=[ci_lo, ci_hi], fmt="none",
                capsize=3, ecolor="#333333", elinewidth=1.0, capthick=1.0)
ax_log.set_yscale("log")
ax_log.set_ylim(5, 2000)
ax_log.set_ylabel("Latency (ns/op, log scale)")
ax_log.set_xticks(x)
ax_log.set_xticklabels(AXIS_LABELS, fontsize=9)
ax_log.set_title("All Implementations")
for bar, m in zip(bars, means_miss):
    y_label = m * 2.2 if m < 100 else m * 1.4
    ax_log.text(bar.get_x() + bar.get_width() / 2, y_label,
                f"{m:.1f}", ha="center", va="bottom", fontsize=8)

# Linear zoom panel: custom implementations only (exclude stdmap)
zoom_impls = ["tinyptr", "naive", "prob"]
zoom_x     = np.arange(len(zoom_impls))
zm  = [gbench["miss"][i]["mean"]   for i in zoom_impls]
zs  = [gbench["miss"][i]["stddev"] for i in zoom_impls]
zci_lo = [m - ci_95(m, s, 5)[0] for m, s in zip(zm, zs)]
zci_hi = [ci_95(m, s, 5)[1] - m for m, s in zip(zm, zs)]
zc  = [COLORS[i] for i in zoom_impls]
z_labels = [AXIS_LABELS[IMPLS.index(i)] for i in zoom_impls]

bars2 = ax_zoom.bar(zoom_x, zm, color=zc, edgecolor="white", linewidth=0.5)
ax_zoom.errorbar(zoom_x, zm, yerr=[zci_lo, zci_hi], fmt="none",
                 capsize=4, ecolor="#333333", elinewidth=1.2, capthick=1.2)
ax_zoom.set_ylabel("Latency (ns/op)")
ax_zoom.set_xticks(zoom_x)
ax_zoom.set_xticklabels(z_labels, fontsize=9)
ax_zoom.set_title("Custom Implementations (zoomed, 95% CI)")
ax_zoom.set_ylim(0, max(zm) * 1.85)
for bar, m in zip(bars2, zm):
    ax_zoom.text(bar.get_x() + bar.get_width() / 2, m + 0.5,
                 f"{m:.3f}", ha="center", va="bottom", fontsize=8.5)

# Significance brackets
_, p_tp_na = welch_t_test(zm[0], zs[0], 5, zm[1], zs[1], 5)
_, p_tp_pr = welch_t_test(zm[0], zs[0], 5, zm[2], zs[2], 5)
_, p_na_pr = welch_t_test(zm[1], zs[1], 5, zm[2], zs[2], 5)
y0 = max(zm) * 1.28
add_sig_bracket(ax_zoom, 0, 1, y0,        p_tp_na, label_offset=0.3)
add_sig_bracket(ax_zoom, 0, 2, y0 + 6.0,  p_tp_pr, label_offset=0.3)
add_sig_bracket(ax_zoom, 1, 2, y0 + 12.0, p_na_pr, label_offset=0.3)

fig.suptitle(
    "Figure 1 -- Miss-Query Lookup Latency (Google Benchmark, n=5 reps, "
    "14,344,391 entries, error bars = 95% CI)",
    fontsize=9, y=1.02)
plt.tight_layout()
save(fig, "fig1_lookup_latency")

# ---------------------------------------------------------------------------
# Figure 2: Workload comparison (miss / hit / mixed)
# ---------------------------------------------------------------------------

WORKLOAD_COLORS  = {"miss": "#4575b4", "hit": "#d73027", "mixed": "#fdae61"}
workload_keys    = ["miss", "hit", "mixed"]
workload_display = ["Miss (key absent)", "Hit (key present)", "Mixed (95% miss + 5% hit)"]

fig, ax = plt.subplots(figsize=(10, 4.2))
bar_w   = 0.22
offsets = np.array([-1, 0, 1]) * bar_w

for wi, (wk, wl) in enumerate(zip(workload_keys, workload_display)):
    means = [gbench[wk][i]["mean"]   for i in IMPLS]
    stds  = [gbench[wk][i]["stddev"] for i in IMPLS]
    ci_lo = [m - ci_95(m, s, 5)[0] for m, s in zip(means, stds)]
    ci_hi = [ci_95(m, s, 5)[1] - m for m, s in zip(means, stds)]
    xs    = x + offsets[wi]
    ax.bar(xs, means, bar_w, color=WORKLOAD_COLORS[wk],
           edgecolor="white", linewidth=0.5, label=wl, alpha=0.88)
    ax.errorbar(xs, means, yerr=[ci_lo, ci_hi], fmt="none",
                capsize=3, ecolor="#333333", elinewidth=0.9, capthick=0.9)

ax.set_ylabel("Latency (ns/op)")
ax.set_xticks(x)
ax.set_xticklabels(AXIS_LABELS, fontsize=9)
ax.set_title("Figure 2 -- Lookup Latency by Workload (Google Benchmark, n=5, error bars = 95% CI)")
ax.legend(loc="upper left", framealpha=0.9)
y_max = max(gbench["hit"]["stdmap"]["mean"], gbench["miss"]["stdmap"]["mean"])
ax.set_ylim(0, y_max * 1.3)
# Value labels on the non-stdmap bars for readability
for wi, wk in enumerate(workload_keys):
    for ii, impl in enumerate(IMPLS):
        if impl == "stdmap":
            continue
        m = gbench[wk][impl]["mean"]
        ax.text(ii + offsets[wi], m + 1.2, f"{m:.1f}",
                ha="center", va="bottom", fontsize=6.5, color="#222222")

plt.tight_layout()
save(fig, "fig2_workload_comparison")

# ---------------------------------------------------------------------------
# Figure 3: Cache hierarchy counters (3 subplots)
# ---------------------------------------------------------------------------

fig, axes = plt.subplots(1, 3, figsize=(13, 4))
metrics = [
    ("misses_per_lookup",   "LLC Cache Misses per Lookup", "misses / lookup"),
    ("l1d_per_lookup",      "L1D Replacements per Lookup", "replacements / lookup"),
    ("dtlb_per_lookup",     "dTLB Misses per Lookup",      "misses / lookup"),
]

for ax, (key, title, ylabel) in zip(axes, metrics):
    vals = [perf[i][key] for i in IMPLS]
    bars = ax.bar(x, vals, color=colors, edgecolor="white", linewidth=0.5)
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    ax.set_xticks(x)
    ax.set_xticklabels(AXIS_LABELS, fontsize=9)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2,
                bar.get_height() + max(vals) * 0.022,
                f"{v:.2f}", ha="center", va="bottom", fontsize=8)
    ax.set_ylim(0, max(vals) * 1.2)

fig.suptitle(
    "Figure 3 -- Cache Hierarchy Counters (perf stat, avg of 3 runs, "
    "2,000,000 miss queries, full RockYou)",
    fontsize=9, y=1.02)
plt.tight_layout()
save(fig, "fig3_cache_counters")

# ---------------------------------------------------------------------------
# Figure 4: IPC and branch mispredictions
# ---------------------------------------------------------------------------

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.5, 4))

ipc_vals   = [perf[i]["ipc"]                    for i in IMPLS]
bmiss_vals = [perf[i]["branch_miss_per_lookup"]  for i in IMPLS]

b1 = ax1.bar(x, ipc_vals, color=colors, edgecolor="white", linewidth=0.5)
ax1.set_ylabel("Instructions per Cycle (IPC)")
ax1.set_xticks(x)
ax1.set_xticklabels(AXIS_LABELS, fontsize=9)
ax1.set_title("IPC (higher is better)")
ax1.set_ylim(0, max(ipc_vals) * 1.2)
for bar, v in zip(b1, ipc_vals):
    ax1.text(bar.get_x() + bar.get_width() / 2, v + max(ipc_vals) * 0.02,
             f"{v:.4f}", ha="center", va="bottom", fontsize=8.5)

b2 = ax2.bar(x, bmiss_vals, color=colors, edgecolor="white", linewidth=0.5)
ax2.set_ylabel("Branch Misses per Lookup")
ax2.set_xticks(x)
ax2.set_xticklabels(AXIS_LABELS, fontsize=9)
ax2.set_title("Branch Mispredictions per Lookup")
ax2.set_ylim(0, max(bmiss_vals) * 1.2)
for bar, v in zip(b2, bmiss_vals):
    ax2.text(bar.get_x() + bar.get_width() / 2, v + max(bmiss_vals) * 0.025,
             f"{v:.4f}", ha="center", va="bottom", fontsize=8.5)

fig.suptitle("Figure 4 -- CPU Execution Efficiency (perf stat, avg of 3 runs, 2M lookups)",
             fontsize=9, y=1.02)
plt.tight_layout()
save(fig, "fig4_ipc_branch")

# ---------------------------------------------------------------------------
# Figure 5: Percentile latency -- full range + tail log scale
# ---------------------------------------------------------------------------

fig, (ax_full, ax_tail) = plt.subplots(1, 2, figsize=(11, 4))

pct_vals  = [50, 95, 99, 99.9]
pct_label = ["p50", "p95", "p99", "p99.9"]

for impl, label in zip(IMPLS, LEG_LABELS):
    d  = latency[impl]
    ys = [d["p50"], d["p95"], d["p99"], d["p999"]]
    ax_full.plot(pct_vals, ys, marker="o", label=label,
                 color=COLORS[impl], linewidth=1.8, markersize=5)

ax_full.set_xlabel("Percentile")
ax_full.set_ylabel("Latency (ns)")
ax_full.set_title("Full Percentile Range")
ax_full.legend(fontsize=8, loc="upper left")
ax_full.set_xticks(pct_vals)
ax_full.set_xticklabels(pct_label)

for impl, label in zip(IMPLS, LEG_LABELS):
    d  = latency[impl]
    ys = [d["p95"], d["p99"], d["p999"]]
    ax_tail.plot([95, 99, 99.9], ys, marker="o", label=label,
                 color=COLORS[impl], linewidth=1.8, markersize=5)

ax_tail.set_xlabel("Percentile")
ax_tail.set_ylabel("Latency (ns, log scale)")
ax_tail.set_yscale("log")
ax_tail.set_title("Tail Latency (p95 to p99.9, log scale)")
ax_tail.legend(fontsize=8, loc="upper left")
ax_tail.set_xticks([95, 99, 99.9])
ax_tail.set_xticklabels(["p95", "p99", "p99.9"])

fig.suptitle(
    "Figure 5 -- Latency Percentiles (RDTSC, 2M samples, 500K warmup, miss workload, "
    "overhead-subtracted)",
    fontsize=9, y=1.02)
plt.tight_layout()
save(fig, "fig5_latency_percentiles")

# ---------------------------------------------------------------------------
# Figure 6: Thread scaling MH/s with error bars
# ---------------------------------------------------------------------------

fig, ax = plt.subplots(figsize=(6.5, 4.2))
threads = [1, 2, 4, 8]

for impl, label in zip(IMPLS, LEG_LABELS):
    mhs_mean = [np.mean(cracker[impl][t]) for t in threads]
    mhs_std  = [np.std(cracker[impl][t])  for t in threads]
    ax.errorbar(threads, mhs_mean, yerr=mhs_std, marker="o", label=label,
                color=COLORS[impl], linewidth=1.8, markersize=6,
                capsize=4, capthick=1.2)

ax.set_xlabel("Thread count")
ax.set_ylabel("Throughput (MH/s)")
ax.set_title("Figure 6 -- Cracker Throughput Scaling\n(3 runs per cell, error bars = stddev)")
ax.set_xticks(threads)
ax.legend(loc="upper left", ncol=1, fontsize=8)
ax.yaxis.set_major_formatter(ticker.FormatStrFormatter("%.2f"))

plt.tight_layout()
save(fig, "fig6_thread_scaling")

# ---------------------------------------------------------------------------
# Figure 7: End-to-end wall time (hyperfine), min/mean/max
# ---------------------------------------------------------------------------

fig, ax = plt.subplots(figsize=(7, 4))
hf_means = [hyperfine[i]["mean"]   for i in IMPLS]
hf_stds  = [hyperfine[i]["stddev"] for i in IMPLS]
hf_mins  = [hyperfine[i]["min"]    for i in IMPLS]
hf_maxs  = [hyperfine[i]["max"]    for i in IMPLS]
err_lo   = [m - mn for m, mn in zip(hf_means, hf_mins)]
err_hi   = [mx - m for m, mx in zip(hf_means, hf_maxs)]

bars = ax.bar(x, hf_means, color=colors, edgecolor="white", linewidth=0.5)
ax.errorbar(x, hf_means, yerr=[err_lo, err_hi], fmt="none",
            capsize=5, ecolor="#333333", elinewidth=1.5, capthick=1.5,
            label="min/max range")
for i, (bar, m, s) in enumerate(zip(bars, hf_means, hf_stds)):
    ax.text(bar.get_x() + bar.get_width() / 2, hf_maxs[i] + 0.2,
            f"{m:.2f}s\n±{s:.2f}s", ha="center", va="bottom", fontsize=8)

ax.set_ylabel("Wall-clock time (s)")
ax.set_xticks(x)
ax.set_xticklabels(AXIS_LABELS, fontsize=9)
ax.set_title("Figure 7 -- End-to-End Crack Time\n(hyperfine, 5 runs, 2 warmups, 4 threads, entry ~7M)")
ax.set_ylim(0, max(hf_maxs) * 1.3)
ax.legend(fontsize=8)
plt.tight_layout()
save(fig, "fig7_walltime")

# ---------------------------------------------------------------------------
# Figure 8: Peak memory footprint
# ---------------------------------------------------------------------------

fig, ax = plt.subplots(figsize=(6.5, 4))
mem_mb  = [rss_kb[i] / 1024 for i in IMPLS]
bars    = ax.bar(x, mem_mb, color=colors, edgecolor="white", linewidth=0.5)
for bar, v in zip(bars, mem_mb):
    ax.text(bar.get_x() + bar.get_width() / 2, v + 15,
            f"{v:.0f} MB", ha="center", va="bottom", fontsize=9)
ax.set_ylabel("Peak RSS (MiB)")
ax.set_xticks(x)
ax.set_xticklabels(AXIS_LABELS, fontsize=9)
ax.set_title("Figure 8 -- Peak Memory Usage\n(/usr/bin/time -v, 14,344,391 entries loaded)")
ax.set_ylim(0, max(mem_mb) * 1.2)
plt.tight_layout()
save(fig, "fig8_memory")

# ---------------------------------------------------------------------------
# Structured results tables
# ---------------------------------------------------------------------------

print("=" * 80)
print("SYSTEM CONFIGURATION")
print("=" * 80)
print("  CPU:       Intel (4 cores, 4 threads), 4.1 GHz max (performance governor)")
print("  L1d:       48 KiB x2    L2: 1280 KiB x2    L3: 6144 KiB (6 MB)")
print("  OS:        Linux 6.19.12 (Fedora 42)")
print("  Compiler:  GCC 15.2.1, -O2 -march=native")
print("  OpenSSL:   3.6.1")
print("  Dataset:   RockYou, 14,344,391 entries, 134 MB")
print()

print("=" * 80)
print("TABLE 1: Lookup Latency (Google Benchmark, n=5, ns/op)")
print("=" * 80)
for wk, wl in [("miss","Miss"), ("hit","Hit"), ("mixed","Mixed 95:5")]:
    print(f"\n  {wl} workload")
    print(f"  {'Impl':<10} {'Mean':>9} {'Stddev':>9} {'Median':>9} {'CV%':>7}  {'95% CI'}")
    print(f"  {'-'*72}")
    for impl in IMPLS:
        d = gbench[wk][impl]
        lo, hi = ci_95(d["mean"], d["stddev"], 5)
        print(f"  {impl:<10} {d['mean']:>9.3f} {d['stddev']:>9.3f} {d['median']:>9.3f} "
              f"{d['cv']:>7.2f}  [{lo:.3f}, {hi:.3f}]")

print()
print("=" * 80)
print("TABLE 2: Welch t-test Pairwise Significance -- Miss Latency (n=5)")
print("=" * 80)
print(f"{'Pair':<22} {'t-stat':>8} {'p-value':>10} {'sig':>5} {'Cohen d':>9}")
print("-" * 60)
pairs = [("tinyptr","naive"), ("tinyptr","prob"), ("tinyptr","stdmap"),
         ("naive","prob"),   ("naive","stdmap"),  ("prob","stdmap")]
for a, b in pairs:
    da, db = gbench["miss"][a], gbench["miss"][b]
    t, p   = welch_t_test(da["mean"], da["stddev"], 5, db["mean"], db["stddev"], 5)
    d_eff  = cohens_d(da["mean"], da["stddev"], db["mean"], db["stddev"])
    print(f"{a+' vs '+b:<22} {t:>8.3f} {p:>10.4f} {sig_label(p):>5} {d_eff:>9.3f}")

print()
print("=" * 80)
print("TABLE 3: RDTSC Percentile Latency (2M samples, overhead-subtracted, ns)")
print("=" * 80)
print(f"{'Impl':<10} {'mean':>9} {'stddev':>9} {'p50':>9} {'p95':>9} {'p99':>9} "
      f"{'p99.9':>9} {'max':>12}")
print("-" * 80)
for impl in IMPLS:
    d = latency[impl]
    print(f"{impl:<10} {d['mean']:>9.3f} {d['stddev']:>9.3f} {d['p50']:>9.3f} "
          f"{d['p95']:>9.3f} {d['p99']:>9.3f} {d['p999']:>9.3f} {d['max']:>12.3f}")

print()
print("=" * 80)
print("TABLE 4: Hardware Counters (perf stat, avg of 3 runs, 2M miss queries)")
print("=" * 80)
print(f"{'Impl':<10} {'Miss/Lkup':>10} {'L1D/Lkup':>10} {'dTLB/Lkup':>11} "
      f"{'IPC':>7} {'MissRate':>10}")
print("-" * 60)
for impl in IMPLS:
    d = perf[impl]
    print(f"{impl:<10} {d['misses_per_lookup']:>10.4f} {d['l1d_per_lookup']:>10.4f} "
          f"{d['dtlb_per_lookup']:>11.4f} {d['ipc']:>7.4f} {d['miss_rate_pct']:>9.2f}%")

print()
print("=" * 80)
print("TABLE 5: Cracker Throughput -- mean MH/s (stddev), 3 runs")
print("=" * 80)
print(f"{'Impl':<10}", end="")
for t in [1, 2, 4, 8]:
    print(f"  {'%d thread' % t:>14}", end="")
print()
print("-" * 74)
for impl in IMPLS:
    print(f"{impl:<10}", end="")
    for t in [1, 2, 4, 8]:
        runs = cracker[impl][t]
        m, s = np.mean(runs), np.std(runs)
        print(f"  {m:.2f}+/-{s:.2f} MH/s", end="")
    print()

print()
print("=" * 80)
print("TABLE 6: End-to-End Wall Time (hyperfine, 5 runs, 2 warmups, 4 threads)")
print("=" * 80)
print(f"{'Impl':<10} {'Mean (s)':>10} {'Stddev':>9} {'Min':>8} {'Max':>8} "
      f"{'vs tinyptr':>12}")
print("-" * 65)
base = hyperfine["tinyptr"]["mean"]
for impl in IMPLS:
    d = hyperfine[impl]
    print(f"{impl:<10} {d['mean']:>10.3f} {d['stddev']:>9.3f} "
          f"{d['min']:>8.3f} {d['max']:>8.3f} {d['mean']/base:>11.2f}x")

print()
print("=" * 80)
print("TABLE 7: Peak Memory Usage (/usr/bin/time -v)")
print("=" * 80)
print(f"{'Impl':<10} {'RSS (kB)':>12} {'RSS (MB)':>10} {'vs tinyptr':>12}")
print("-" * 50)
base_kb = rss_kb["tinyptr"]
for impl in IMPLS:
    kb = rss_kb[impl]
    print(f"{impl:<10} {kb:>12,} {kb/1024:>10.1f} {kb/base_kb:>11.2f}x")

print()
print(f"Figures saved to {OUT_DIR}/")
