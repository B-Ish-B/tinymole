# Benchmark Methodology

This document records how benchmarks were run, what was measured, and why.
The final polished results and interpretation go in write_up.md.
The raw data files live in results/.

---

## Hardware

All benchmarks run on the same machine to keep numbers comparable.

- CPU: Intel Core i3-1115G4 (11th Gen), 3.00 GHz base, 4.10 GHz max
- Cores: 2 physical cores, 4 logical threads (hyperthreading)
- L1d: 48 KiB per core (96 KiB total)
- L2: 1.25 MiB per core (2.5 MiB total)
- L3: 6 MiB shared
- OS: Linux (Fedora), kernel 6.19

---

## Implementations Under Test

Three hash table implementations, all loading the same wordlist and
answering the same queries. The only differences are internal storage layout.

| Name | File | Slot size | Storage |
|---|---|---|---|
| tiny pointer | hash_table.hpp/cpp | 24 bytes | contiguous pool, 27-bit offset + 5-bit length |
| naive | hash_table_naive.hpp/cpp | 32 bytes | contiguous pool, 64-bit offset |
| stdmap | hash_table_stdmap.hpp/cpp | ~80-120 bytes | std::unordered_map, heap per entry |

---

## Wordlists

- Development subset: data/rockyou_1m.txt (first 1M lines, 8.2 MB)
- Final run: data/rockyou.txt (14.3M lines, 134 MB)

---

## Benchmark 1: Lookup Throughput (Google Benchmark)

### What it measures

Nanoseconds per lookup and lookups per second for each implementation
under a miss-heavy query workload.

### Why miss-heavy

A password cracker spends the vast majority of its time on candidates
that do not match. A hit-heavy benchmark would not reflect the real workload
and would also be dominated by the cost of returning the result string rather
than the cost of the lookup itself.

### Query generation

Miss queries are MD5 hashes of the strings "MISS_QUERY_0", "MISS_QUERY_1", ...
These are guaranteed not to appear in the RockYou wordlist.

### How to run

```bash
make bench
```

Output written to results/benchmark.csv.

### Development results (1M subset, 2026-05-07)

| Implementation | ns per lookup | lookups/sec |
|---|---|---|
| tiny pointer | 47 ns | 21.4 M/s |
| naive | 50 ns | 20.1 M/s |
| stdmap | 282 ns | 3.6 M/s |

CPU scaling was enabled during this run. Disable before final run:
```bash
sudo cpupower frequency-set --governor performance
```

---

## Benchmark 2: Cache Miss Comparison (perf stat)

### What it measures

L1 and LLC (last-level cache) miss counts for 2M miss-heavy lookups
against each implementation. This directly quantifies the cache benefit
of smaller slot sizes.

### Why separate binaries

perf stat measures the entire process. Running all three in one binary
would mix their working sets. Separate binaries give clean per-implementation
cache profiles.

### How to run

```bash
make bench  # builds perf_tinyptr, perf_naive, perf_stdmap

perf stat -e cache-misses,cache-references,L1-dcache-misses,instructions,cycles \
  ./build/perf_tinyptr

perf stat -e cache-misses,cache-references,L1-dcache-misses,instructions,cycles \
  ./build/perf_naive

perf stat -e cache-misses,cache-references,L1-dcache-misses,instructions,cycles \
  ./build/perf_stdmap
```

Raw output saved to results/perf_1m.txt (subset) and results/perf_full.txt (final).

### Development results (1M subset, 2026-05-07)

| Implementation | cache-misses | L1-dcache-misses | cycles | wall time |
|---|---|---|---|---|
| tiny pointer | 15,959,836 | 12,786,094 | 5,113,272,802 | 1.297 s |
| naive | 18,475,385 | 14,574,396 | 5,245,410,118 | 1.334 s |
| stdmap | 39,366,312 | 31,125,968 | 7,528,961,785 | 2.128 s |

Key observations:
- Tiny pointer has 14% fewer cache misses than naive (smaller slots, more fit in cache per probe)
- Stdmap has 2.5x more cache misses than tiny pointer (heap allocation, scattered memory)
- Instruction counts are nearly identical between tiny pointer and naive, confirming
  the difference is memory access cost, not compute cost
- These differences will be more pronounced on the full 14.3M dataset where the
  table size exceeds L3 cache for all implementations

---

## Next Steps

- Run full 14.3M entry benchmarks and save to results/perf_full.txt
- Thread scaling benchmarks (1, 2, 4 threads) once cracker is implemented
- Generate graphs from results/ using src/python/plot_results.py
