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

## Benchmark 3: Thread Scaling (cracker)

### What it measures

End-to-end crack time for a target near the end of the candidate list
(position 999,000 of 1,000,000). This exercises the full search path:
MD5 per candidate, hash table lookup on hit, atomic early-exit on match.
Table build time and candidate load time are excluded; only the crack()
wall time is recorded.

### Target

Password: bunnyxx (line 999,000 of rockyou_1m.txt)
MD5: 86d23dd6cc9e3c345c0baea4b0feb0d7

### Conditions

- CPU governor set to performance (cpupower frequency-set --governor performance)
- taskset per run: 1 thread = -c 0, 2 threads = -c 0,2, 4 threads = -c 0,1,2,3
- Pinning for 2 threads uses one logical thread per physical core to isolate
  the real core-to-core speedup from hyperthreading

### How to run

```bash
HASH=86d23dd6cc9e3c345c0baea4b0feb0d7
WL=data/rockyou_1m.txt

taskset -c 0       ./build/cracker --hash $HASH --wordlist $WL --threads 1 --log-path /dev/null
taskset -c 0,2     ./build/cracker --hash $HASH --wordlist $WL --threads 2 --log-path /dev/null
taskset -c 0,1,2,3 ./build/cracker --hash $HASH --wordlist $WL --threads 4 --log-path /dev/null
```

Raw output saved to results/thread_scaling_1m.txt.

### Development results (1M subset, 2026-05-07)

Three runs each; median reported.

| Threads | Run 1 | Run 2 | Run 3 | Median | Speedup vs 1T |
|---|---|---|---|---|---|
| 1 | 434.0 ms | 363.0 ms | 388.8 ms | 388.8 ms | 1.00x |
| 2 | 323.8 ms | 317.5 ms | 323.7 ms | 323.7 ms | 1.20x |
| 4 | 221.6 ms | 228.7 ms | 248.4 ms | 228.7 ms | 1.70x |

Key observations:
- 1 to 2 threads (two physical cores): 1.20x speedup. Lower than the theoretical
  2x because the 48 MB table far exceeds L3 (6 MB), making lookup
  memory-bandwidth bound. Both cores compete for DRAM bandwidth rather than
  running independently.
- 2 to 4 threads (adding hyperthreads): 1.42x additional gain. HT allows each
  physical core to issue memory requests from two logical threads simultaneously,
  partially hiding DRAM latency. The gain is larger than typical HT benefit
  precisely because the workload is latency-bound on memory accesses.
- Combined 1 to 4 threads: 1.70x. This is the ceiling on this hardware for a
  memory-bandwidth-bound workload. A compute-bound workload would scale closer
  to 4x.
- These numbers will improve on the full 14.3M dataset run because the table will
  be even larger, making the latency-hiding benefit of additional threads more
  pronounced.

---

## Next Steps

- Run full 14.3M entry benchmarks and save to results/perf_full.txt
- Thread scaling benchmarks on full rockyou.txt
- Generate graphs from results/ using src/python/plot_results.py
