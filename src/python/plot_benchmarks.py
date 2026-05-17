"""
Benchmark analysis and figure generation for the tinymole password cracker.
Run from the project root: uv run python3 src/python/plot_benchmarks.py
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
    "font.family":        "sans-serif",
    "font.sans-serif":    ["Arial", "Helvetica Neue", "DejaVu Sans"],
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
    "axes.grid.axis":     "y",
    "grid.alpha":         0.35,
    "grid.linestyle":     "-",
    "grid.color":         "#e0e0e0",
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
               "Prob (key-dep.)", "std::unordered_map"]

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

# perf_event_open counters: 7 runs of 2M miss queries each. Counters are
# scoped to the lookup loop only (load and query-generation excluded) via
# PERF_EVENT_IOC_ENABLE/DISABLE around the loop.
# trimmed mean: per metric, drop highest and lowest, compute mean+CI on remaining 5
perf_raw = {
    "tinyptr": {
        "cache_misses":    [11_010_673, 10_462_127, 11_060_043, 10_715_662, 11_126_650, 10_715_225, 9_779_956],
        "cache_refs":      [12_711_781, 12_131_061, 12_693_127, 12_382_768, 12_796_517, 12_328_228, 11_148_309],
        "instructions":    [128_077_897, 128_077_902, 128_077_907, 128_077_904, 128_077_898, 128_077_903, 128_077_930],
        "cycles":          [295_651_873, 316_751_176, 301_832_223, 304_958_397, 295_231_709, 314_661_636, 382_677_963],
        "branch_misses":   [1_772_356, 1_775_519, 1_780_805, 1_780_798, 1_778_001, 1_777_234, 1_778_569],
        "dtlb_misses":     [1_981_724, 1_981_754, 1_981_496, 1_981_514, 1_981_322, 1_981_938, 1_984_113],
        "l1d_replacement": [7_457_975, 7_426_696, 7_425_925, 7_414_429, 7_469_792, 7_428_100, 7_358_659],
    },
    "naive": {
        "cache_misses":    [10_203_535, 10_862_885, 10_066_692, 10_817_463, 8_702_576, 10_624_588, 10_622_189],
        "cache_refs":      [11_774_155, 12_315_326, 11_483_150, 12_489_548, 9_887_587, 12_197_855, 12_138_985],
        "instructions":    [128_077_905, 128_077_922, 128_077_914, 128_077_901, 128_077_958, 128_077_904, 128_077_923],
        "cycles":          [322_851_924, 313_573_417, 346_970_689, 293_895_294, 397_314_799, 314_382_975, 316_763_595],
        "branch_misses":   [1_780_621, 1_777_811, 1_780_644, 1_773_571, 1_779_508, 1_775_863, 1_774_790],
        "dtlb_misses":     [1_982_220, 1_981_854, 1_982_403, 1_981_061, 1_984_628, 1_981_963, 1_981_993],
        "l1d_replacement": [7_404_595, 7_420_091, 7_365_815, 7_423_714, 7_317_337, 7_415_618, 7_405_281],
    },
    "prob": {
        "cache_misses":    [11_471_618, 11_201_326, 6_994_976, 10_534_960, 11_144_849, 8_959_659, 11_206_264],
        "cache_refs":      [12_985_175, 12_816_190, 8_035_767, 11_966_487, 12_612_350, 10_287_676, 12_691_380],
        "instructions":    [116_978_665, 116_978_640, 116_978_710, 116_978_663, 116_978_648, 116_978_681, 116_978_643],
        "cycles":          [314_659_962, 311_912_051, 467_412_074, 341_752_809, 326_586_941, 371_459_482, 323_099_115],
        "branch_misses":   [1_851_180, 1_852_369, 1_864_000, 1_851_822, 1_856_939, 1_853_761, 1_851_945],
        "dtlb_misses":     [1_984_903, 1_984_110, 1_988_655, 1_985_128, 1_985_163, 1_984_694, 1_984_796],
        "l1d_replacement": [7_727_901, 7_721_656, 7_496_937, 7_678_706, 7_714_346, 7_596_739, 7_711_699],
    },
    "stdmap": {
        "cache_misses":    [22_150_892, 21_914_117, 22_044_915, 21_699_712, 21_805_353, 21_652_876, 21_427_958],
        "cache_refs":      [24_458_845, 24_069_234, 24_357_922, 23_872_906, 24_065_187, 23_834_893, 23_660_305],
        "instructions":    [674_537_840, 674_537_845, 674_537_834, 674_537_852, 674_537_843, 674_537_853, 674_537_851],
        "cycles":          [1_807_894_325, 1_891_678_907, 1_829_126_265, 1_944_971_691, 1_879_867_588, 1_900_757_086, 1_970_466_282],
        "branch_misses":   [1_735_374, 1_731_133, 1_732_133, 1_728_025, 1_728_081, 1_727_034, 1_723_812],
        "dtlb_misses":     [4_415_815, 4_420_125, 4_417_682, 4_420_563, 4_418_018, 4_421_823, 4_409_544],
        "l1d_replacement": [14_803_583, 14_838_418, 14_796_308, 14_865_328, 14_876_987, 14_869_953, 14_881_914],
    },
}

perf = {}
for impl, d in perf_raw.items():
    n   = len(d["cache_misses"])
    avg = {k: np.mean(v) for k, v in d.items()}

    def _perf_ci(vals):
        a = np.array(sorted(vals), dtype=float)[1:-1]  # drop min and max
        nt = len(a)
        m = float(a.mean())
        s = float(a.std(ddof=1))
        t_crit = float(stats.t.ppf(0.975, df=nt - 1))
        return m, t_crit * s / np.sqrt(nt)

    ipc_per_run = [d["instructions"][i] / d["cycles"][i] for i in range(n)]
    m_miss,  ci_miss  = _perf_ci([v / N_QUERIES for v in d["cache_misses"]])
    m_l1d,   ci_l1d   = _perf_ci([v / N_QUERIES for v in d["l1d_replacement"]])
    m_dtlb,  ci_dtlb  = _perf_ci([v / N_QUERIES for v in d["dtlb_misses"]])
    m_bmiss, ci_bmiss = _perf_ci([v / N_QUERIES for v in d["branch_misses"]])
    m_ipc,   ci_ipc   = _perf_ci(ipc_per_run)

    perf[impl] = {
        "misses_per_lookup":         m_miss,  "misses_per_lookup_ci":         ci_miss,
        "l1d_per_lookup":            m_l1d,   "l1d_per_lookup_ci":            ci_l1d,
        "dtlb_per_lookup":           m_dtlb,  "dtlb_per_lookup_ci":           ci_dtlb,
        "branch_miss_per_lookup":    m_bmiss, "branch_miss_per_lookup_ci":    ci_bmiss,
        "ipc":                       m_ipc,   "ipc_ci":                       ci_ipc,
        "miss_rate_pct": 100.0 * avg["cache_misses"] / avg["cache_refs"],
    }

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

# bench_cracker: 3 runs per impl/thread count, mid-list target (entry ~7M)
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
ax_zoom.set_ylim(0, 56)
for bar, m in zip(bars2, zm):
    ax_zoom.text(bar.get_x() + bar.get_width() / 2, m + 0.4,
                 f"{m:.1f}", ha="center", va="bottom", fontsize=8.5)

# Significance brackets: only show significant pairs; omit ns (TinyPtr vs Prob)
_, p_tp_na = welch_t_test(zm[0], zs[0], 5, zm[1], zs[1], 5)
_, p_na_pr = welch_t_test(zm[1], zs[1], 5, zm[2], zs[2], 5)
add_sig_bracket(ax_zoom, 0, 1, 47.5, p_tp_na, label_offset=0.3)
add_sig_bracket(ax_zoom, 1, 2, 51.0, p_na_pr, label_offset=0.3)

fig.suptitle(
    "Miss-query lookup latency (Google Benchmark, n=5, 95% CI)",
    fontsize=9, y=1.01)
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
ax.set_title("Lookup latency by workload (Google Benchmark, n=5, 95% CI)")
ax.legend(loc="upper left", framealpha=0.9)
y_max = max(gbench["hit"]["stdmap"]["mean"], gbench["miss"]["stdmap"]["mean"])
ax.set_ylim(0, y_max * 1.3)
stdmap_label_offset = max(gbench[wk]["stdmap"]["mean"] for wk in workload_keys) * 0.012
for wi, wk in enumerate(workload_keys):
    for ii, impl in enumerate(IMPLS):
        m      = gbench[wk][impl]["mean"]
        loff   = stdmap_label_offset if impl == "stdmap" else 1.2
        lsize  = 7.0 if impl == "stdmap" else 6.5
        ax.text(ii + offsets[wi], m + loff, f"{m:.1f}",
                ha="center", va="bottom", fontsize=lsize, color="#222222")

plt.tight_layout()
save(fig, "fig2_workload_comparison")

# ---------------------------------------------------------------------------
# Figure 3: Cache hierarchy counters (3 subplots)
# ---------------------------------------------------------------------------

fig, axes = plt.subplots(1, 3, figsize=(11, 4))
metrics = [
    ("misses_per_lookup",   "LLC Cache Misses per Lookup", "misses / lookup"),
    ("l1d_per_lookup",      "L1D Replacements per Lookup", "replacements / lookup"),
    ("dtlb_per_lookup",     "dTLB Misses per Lookup",      "misses / lookup"),
]

for ax, (key, title, ylabel) in zip(axes, metrics):
    vals = [perf[i][key]           for i in IMPLS]
    errs = [perf[i][key + "_ci"]   for i in IMPLS]
    bars = ax.bar(x, vals, yerr=errs, color=colors, edgecolor="white", linewidth=0.5,
                  capsize=4, error_kw={"elinewidth": 1.2, "ecolor": "black"})
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
    "Cache hierarchy counters, lookup phase only (n=7 trimmed, 95% CI, 2M miss queries)",
    fontsize=9, y=1.01)
plt.tight_layout()
save(fig, "fig3_cache_counters")

# ---------------------------------------------------------------------------
# Figure 4: IPC and branch mispredictions
# ---------------------------------------------------------------------------

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.5, 4))

ipc_vals   = [perf[i]["ipc"]                    for i in IMPLS]
bmiss_vals = [perf[i]["branch_miss_per_lookup"]  for i in IMPLS]

ipc_ci    = [perf[i]["ipc_ci"]                   for i in IMPLS]
bmiss_ci  = [perf[i]["branch_miss_per_lookup_ci"] for i in IMPLS]

b1 = ax1.bar(x, ipc_vals, yerr=ipc_ci, color=colors, edgecolor="white", linewidth=0.5,
             capsize=4, error_kw={"elinewidth": 1.2, "ecolor": "black"})
ax1.set_ylabel("Instructions per Cycle (IPC)")
ax1.set_xticks(x)
ax1.set_xticklabels(AXIS_LABELS, fontsize=9)
ax1.set_title("IPC (higher is better)")
ax1.set_ylim(0, max(ipc_vals) * 1.2)
for bar, v in zip(b1, ipc_vals):
    ax1.text(bar.get_x() + bar.get_width() / 2, v + max(ipc_vals) * 0.02,
             f"{v:.3f}", ha="center", va="bottom", fontsize=8.5)

b2 = ax2.bar(x, bmiss_vals, yerr=bmiss_ci, color=colors, edgecolor="white", linewidth=0.5,
             capsize=4, error_kw={"elinewidth": 1.2, "ecolor": "black"})
ax2.set_ylabel("Branch Misses per Lookup")
ax2.set_xticks(x)
ax2.set_xticklabels(AXIS_LABELS, fontsize=9)
ax2.set_title("Branch Mispredictions per Lookup")
ax2.set_ylim(0, max(bmiss_vals) * 1.25)
for bar, v in zip(b2, bmiss_vals):
    ax2.text(bar.get_x() + bar.get_width() / 2, v + max(bmiss_vals) * 0.025,
             f"{v:.3f}", ha="center", va="bottom", fontsize=8.5)

fig.suptitle("CPU execution efficiency, lookup phase only (n=7 trimmed, 95% CI, 2M lookups)",
             fontsize=9, y=1.01)
plt.tight_layout()
save(fig, "fig4_ipc_branch")

# ---------------------------------------------------------------------------
# Figure 5: Percentile latency -- full range + tail log scale
# ---------------------------------------------------------------------------

fig, (ax_full, ax_tail) = plt.subplots(1, 2, figsize=(11, 4))

pct_x    = [0, 1, 2, 3]
pct_label = ["p50", "p95", "p99", "p99.9"]

for impl, label in zip(IMPLS, LEG_LABELS):
    d  = latency[impl]
    ys = [d["p50"], d["p95"], d["p99"], d["p999"]]
    ax_full.plot(pct_x, ys, marker="o", label=label,
                 color=COLORS[impl], linewidth=1.8, markersize=5)

ax_full.set_xlabel("Percentile")
ax_full.set_ylabel("Latency (ns)")
ax_full.set_title("Full Percentile Range")
ax_full.legend(fontsize=8, loc="upper left")
ax_full.set_xticks(pct_x)
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
ax_tail.set_xticklabels(["p95", "p99", "p99.9"], fontweight="normal")

fig.suptitle(
    "Lookup latency percentiles, miss workload (RDTSC, 2M samples, 500K warmup)",
    fontsize=9, y=1.01)
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
ax.set_title("Cracker throughput scaling (3 runs per cell, error bars: stddev)")
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
base_time = hf_means[0]
for i, (bar, m, s) in enumerate(zip(bars, hf_means, hf_stds)):
    ratio = m / base_time
    r_str = "baseline" if i == 0 else f"{ratio:.2f}x"
    ax.text(bar.get_x() + bar.get_width() / 2, hf_maxs[i] + 0.25,
            f"{m:.2f}s\n({r_str})", ha="center", va="bottom", fontsize=8)

ax.set_ylabel("Wall-clock time (s)")
ax.set_xticks(x)
ax.set_xticklabels(AXIS_LABELS, fontsize=9)
ax.set_title("End-to-end crack time (hyperfine, 5 runs, 2 warmups, 4 threads)")
ax.set_ylim(0, max(hf_maxs) * 1.35)
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
ax.set_title("Peak memory usage (/usr/bin/time -v)")
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
print("TABLE 4: Hardware Counters (perf stat, n=7 trimmed, 2M miss queries)")
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
