#!/usr/bin/env python3

'''
@author Patrick Rooney
@since May 10, 2026
@description: Generates benchmark graphs from the compiled password cracker's
result files in results/. Produces four PNGs covering lookup throughput, cache
miss counts, thread scaling, and the 3-way cracker comparison.
'''

import csv
import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt

RESULTS_DIR = Path("results")

IMPL_LABELS = {
    "tinyptr": "tiny pointer",
    "naive":   "naive",
    "stdmap":  "std::unordered_map",
}

IMPL_COLORS = {
    "tiny pointer":        "#2196F3",
    "naive":               "#FF9800",
    "std::unordered_map":  "#F44336",
}


# reads benchmark.csv and returns a list of {name, real_time_ns, items_per_second} dicts
def parse_benchmark_csv(path: Path) -> list[dict]:
    rows = []
    with open(path, newline='') as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            rows.append({
                "name": row["name"].strip('"'),  # Google Benchmark wraps names in quotes
                "real_time_ns": float(row["real_time"]),
                "items_per_second": float(row["items_per_second"]),
            })
    return rows


# reads perf output and returns a list of {impl, cache_misses, l1_misses, cycles, wall_time_s} dicts
def parse_perf_txt(path: Path) -> list[dict]:
    section_re = re.compile(r"^--- (.+?) ---")  # matches "--- tinyptr ---" section headers
    metric_re  = re.compile(r"^([\w-]+):\s+([\d,]+)")
    wall_re    = re.compile(r"^wall time:\s+([\d.]+) s")

    results = []
    current: dict | None = None
    for line in path.read_text().splitlines():
        m = section_re.match(line)
        if m:
            if current is not None:
                results.append(current)  # flush completed section before starting the next
            current = {"impl": m.group(1)}
            continue
        m = metric_re.match(line)
        if m and current is not None:
            key = m.group(1)
            val = int(m.group(2).replace(",", ""))  # strip thousands separators from perf counts
            current[key] = val
            continue
        m = wall_re.match(line)
        if m and current is not None:
            current["wall_time_s"] = float(m.group(1))

    if current is not None:
        results.append(current)
    return results


# reads thread scaling output and returns a list of {threads, runs, median_ms} dicts, one per thread count
def parse_thread_scaling_txt(path: Path) -> list[dict]:
    section_re = re.compile(r"^--- (\d+) thread")
    run_re     = re.compile(r"^run \d+:\s+([\d.]+) ms")
    median_re  = re.compile(r"^median:\s+([\d.]+) ms")

    results = []
    current: dict = {}
    for line in path.read_text().splitlines():
        m = section_re.match(line)
        if m:
            if current:
                results.append(current)
            current = {"threads": int(m.group(1)), "runs": []}
            continue
        m = run_re.match(line)
        if m and current:
            current["runs"].append(float(m.group(1)))
            continue
        m = median_re.match(line)
        if m and current:
            current["median_ms"] = float(m.group(1))

    if current and "median_ms" in current:
        results.append(current)
    return results


# reads the 3-way cracker comparison file and returns {impl_key: [{threads, runs, median_ms}]} per implementation
def parse_cracker_comparison_txt(path: Path) -> dict[str, list[dict]]:
    section_re = re.compile(r"^--- (\w+) ")
    row_re     = re.compile(r"^(\d+)T:\s+([\d.]+(?:\s+[\d.]+)*)\s+median=([\d.]+) ms")

    results: dict[str, list[dict]] = {}
    current_impl: str | None = None
    for line in path.read_text().splitlines():
        m = section_re.match(line)
        if m:
            current_impl = m.group(1).lower()
            results[current_impl] = []
            continue
        m = row_re.match(line)
        if m and current_impl is not None:
            threads = int(m.group(1))
            runs = [float(x) for x in m.group(2).split()]
            median = float(m.group(3))
            results[current_impl].append({"threads": threads, "runs": runs, "median_ms": median})

    return results


# bar chart: ns per lookup for each implementation, from Google Benchmark miss-heavy workload
def plot_lookup_throughput(rows: list[dict], out_path: Path) -> None:
    name_map = {
        "BM_TinyPtr_Miss": "tiny pointer",
        "BM_Naive_Miss":   "naive",
        "BM_StdMap_Miss":  "std::unordered_map",
    }
    labels = [name_map.get(r["name"], r["name"]) for r in rows]
    times  = [r["real_time_ns"] for r in rows]
    colors = [IMPL_COLORS[l] for l in labels]

    fig, ax = plt.subplots(figsize=(8, 5))
    bars = ax.bar(labels, times, color=colors, width=0.5)
    ax.bar_label(bars, fmt="%.1f ns", padding=4, fontsize=10)  # annotate each bar with its exact value
    ax.set_ylabel("ns per lookup")
    ax.set_title("Hash Table Lookup Throughput — Miss-Heavy Workload")
    ax.set_ylim(0, max(times) * 1.2)  # 20% headroom above the tallest bar for labels
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)
    print(f"Chart saved to {out_path}")


# grouped bar chart: LLC and L1-dcache miss counts per implementation
def plot_cache_misses(perf_rows: list[dict], out_path: Path) -> None:
    impls = [r["impl"] for r in perf_rows]
    llc   = [r.get("cache-misses", 0) / 1e6 for r in perf_rows]  # convert raw counts to millions
    l1    = [r.get("L1-dcache-misses", 0) / 1e6 for r in perf_rows]

    x = range(len(impls))
    width = 0.35  # each bar takes half the gap so the pair is centered under its label
    fig, ax = plt.subplots(figsize=(9, 5))
    b1 = ax.bar([i - width / 2 for i in x], llc, width, label="LLC misses",       color="#2196F3")
    b2 = ax.bar([i + width / 2 for i in x], l1,  width, label="L1-dcache misses", color="#FF9800")
    ax.bar_label(b1, fmt="%.1fM", padding=3, fontsize=9)
    ax.bar_label(b2, fmt="%.1fM", padding=3, fontsize=9)
    ax.set_xticks(list(x))
    ax.set_xticklabels(impls)
    ax.set_ylabel("Cache misses (millions)")
    ax.set_title("Cache Miss Comparison — 2M Miss-Heavy Lookups (1M Wordlist)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)
    print(f"Chart saved to {out_path}")


# line chart: tinyptr crack time vs thread count with individual run scatter points to show variance
def plot_thread_scaling(scaling: list[dict], out_path: Path) -> None:
    threads = [r["threads"] for r in scaling]
    medians = [r["median_ms"] for r in scaling]

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.plot(threads, medians, marker='o', linewidth=2,
            color=IMPL_COLORS["tiny pointer"], label="median")

    for row in scaling:
        # overlay individual run times as faint dots behind the median line
        ax.scatter([row["threads"]] * len(row["runs"]), row["runs"],
                   color=IMPL_COLORS["tiny pointer"], alpha=0.35, s=30, zorder=3)

    ax.set_xticks(threads)
    ax.set_xlabel("Threads")
    ax.set_ylabel("Crack time (ms)")
    ax.set_title("Thread Scaling — Tiny Pointer Implementation (1M Wordlist)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)
    print(f"Chart saved to {out_path}")


# line chart: crack time vs thread count for all three implementations on one axis
def plot_cracker_comparison(comparison: dict[str, list[dict]], out_path: Path) -> None:
    impl_order = ["tinyptr", "naive", "stdmap"]
    display    = ["tiny pointer", "naive", "std::unordered_map"]

    fig, ax = plt.subplots(figsize=(8, 5))
    for key, label in zip(impl_order, display):
        rows = comparison.get(key, [])
        if not rows:
            continue
        threads = [r["threads"] for r in rows]
        medians = [r["median_ms"] for r in rows]
        ax.plot(threads, medians, marker='o', linewidth=2,
                color=IMPL_COLORS[label], label=label)

        for row in rows:
            # faint scatter of individual runs behind each median line to show spread
            ax.scatter([row["threads"]] * len(row["runs"]), row["runs"],
                       color=IMPL_COLORS[label], alpha=0.3, s=25, zorder=3)

    all_threads = sorted({r["threads"] for rows in comparison.values() for r in rows})
    ax.set_xticks(all_threads)
    ax.set_xlabel("Threads")
    ax.set_ylabel("Crack time (ms)")
    ax.set_title("3-Way Cracker Comparison — Crack Time vs Threads (1M Wordlist)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)
    print(f"Chart saved to {out_path}")


# entry point: reads each results file and saves all four benchmark charts
def main() -> None:
    RESULTS_DIR.mkdir(exist_ok=True)
    missing = []

    benchmark_csv   = RESULTS_DIR / "benchmark.csv"
    perf_txt        = RESULTS_DIR / "perf_1m.txt"
    scaling_txt     = RESULTS_DIR / "thread_scaling_1m.txt"
    comparison_txt  = RESULTS_DIR / "cracker_comparison_1m.txt"

    for p in (benchmark_csv, perf_txt, scaling_txt, comparison_txt):
        if not p.exists():
            missing.append(str(p))
    if missing:
        print("error: missing result files:", file=sys.stderr)
        for m in missing:
            print(f"  {m}", file=sys.stderr)
        print("Run 'make bench' to generate them.", file=sys.stderr)
        sys.exit(1)

    print("Parsing results...")
    bench_rows  = parse_benchmark_csv(benchmark_csv)
    perf_rows   = parse_perf_txt(perf_txt)
    scaling     = parse_thread_scaling_txt(scaling_txt)
    comparison  = parse_cracker_comparison_txt(comparison_txt)

    print("Generating charts...")
    plot_lookup_throughput(bench_rows, RESULTS_DIR / "lookup_throughput.png")
    plot_cache_misses(perf_rows,       RESULTS_DIR / "cache_misses.png")
    plot_thread_scaling(scaling,       RESULTS_DIR / "thread_scaling.png")
    plot_cracker_comparison(comparison, RESULTS_DIR / "cracker_comparison.png")

    print("Done.")


if __name__ == '__main__':
    main()
