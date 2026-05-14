# Development Benchmarks

All numbers in this document are from the 1M-entry subset of RockYou, run on the development machine before the full-dataset run. They are preliminary and exist to validate methodology and confirm directional results. Full-dataset numbers will replace the tables in the README once the final run is complete.

This document records how each benchmark was run, what was measured, and why. The final polished results and interpretation go in write_up.md. Raw data files live in results/.

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

Tiny pointer has 14% fewer cache misses than naive (smaller slots fit more per cache line).
Stdmap is 2.5x worse than tiny pointer due to scattered heap allocation.
Instruction counts between tiny pointer and naive are nearly identical -- the difference
is memory cost, not compute. Gaps will be larger on the full 14.3M dataset.

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

### Development results (1M subset, 2026-05-07, tiny pointer only)

Three runs each; median reported. Cracker uses EVP_MD_CTX reuse optimization.

| Threads | Run 1 | Run 2 | Run 3 | Median | Speedup vs 1T |
|---|---|---|---|---|---|
| 1 | 350.4 ms | 327.7 ms | 332.6 ms | 332.6 ms | 1.00x |
| 2 | 299.0 ms | 283.6 ms | 304.6 ms | 299.0 ms | 1.11x |
| 4 | 237.8 ms | 293.5 ms | 224.5 ms | 237.8 ms | 1.40x |

1 to 2 threads gives only 1.11x because the 48 MB table far exceeds L3 (6 MB) and
the workload is memory-bandwidth bound. 2 to 4 threads (adding hyperthreads) gets
to 1.40x overall -- HT lets each physical core overlap memory requests from two
logical threads, hiding some DRAM latency. Expect larger gains on the full dataset.

Raw output saved to results/thread_scaling_1m.txt.

---

## Benchmark 4: 3-Way Cracker Comparison

### What it measures

End-to-end crack time across all three hash table implementations at 1, 2,
and 4 threads. This shows how much the hash table choice affects total crack
time, not just isolated lookup throughput.

### How to run

```bash
HASH=86d23dd6cc9e3c345c0baea4b0feb0d7
WL=data/rockyou_1m.txt

for impl in tinyptr naive stdmap; do
  taskset -c 0       ./build/cracker_bench --impl $impl --hash $HASH --wordlist $WL --threads 1
  taskset -c 0,2     ./build/cracker_bench --impl $impl --hash $HASH --wordlist $WL --threads 2
  taskset -c 0,1,2,3 ./build/cracker_bench --impl $impl --hash $HASH --wordlist $WL --threads 4
done
```

Raw output saved to results/cracker_comparison_1m.txt.

### Development results (1M subset, 2026-05-07)

Three runs each; median reported.

| Implementation | 1 thread | 2 threads | 4 threads | 1T->4T speedup |
|---|---|---|---|---|
| tiny pointer | 333.4 ms | 317.6 ms | 236.4 ms | 1.41x |
| naive | 355.2 ms | 332.5 ms | 259.7 ms | 1.37x |
| stdmap | 354.9 ms | 338.4 ms | 267.9 ms | 1.32x |

At 1 thread, naive and stdmap are ~6.5% slower than tiny pointer. The gap is smaller
than the isolated lookup benchmark because MD5 computation is part of the cost, not
just the table lookup. At 4 threads the gap widens slightly (9.9% and 13.3%) as more
concurrent memory traffic makes the smaller slot size more valuable. stdmap scales
worst (1.32x) due to scattered heap accesses under concurrent load. Ordering is
consistent at every thread count: tinyptr < naive < stdmap.

---

## Known Limitations and Future Optimizations

### SIMD MD5 (AVX2 / AVX-512)

The current implementation hashes one password at a time via OpenSSL's EVP
API. AVX2 can hash 8 passwords in parallel in the same time, which is how
tools like hashcat get their speed.

Not implemented here because the workload is memory-bound, not compute-bound.
The perf stat results show 5.1B cycles against 11.6B instructions -- when
cycles exceed instructions the CPU is stalling on memory, not doing work. The
48 MB hash table is 8x larger than the 6 MB L3, so almost every lookup goes
to DRAM (~60-80 ns vs ~4 ns for L3). Making MD5 8x faster would not move the
end-to-end time much because the lookup stall dominates.

SIMD hashing would matter if the table fit in cache (smaller wordlist or
bigger L3) or if the cracker generated mutations on the fly instead of
reading from a static list.

Implementation path: Intel's isa-l_crypto library has a multi-buffer MD5 API.
The worker would accumulate a batch of N candidates, hash them all at once,
then scan the N digests for a match. Batch size = 8 for AVX2, 16 for AVX-512.

---

## Next Steps

- Run full 14.3M entry benchmarks and save to results/perf_full.txt
- Full dataset cracker comparison across all three implementations
- Generate graphs from results/ using src/python/plot_results.py
