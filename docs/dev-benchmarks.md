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

### Development results (1M subset, 2026-05-07, tiny pointer only)

Three runs each; median reported. Cracker uses EVP_MD_CTX reuse optimization.

| Threads | Run 1 | Run 2 | Run 3 | Median | Speedup vs 1T |
|---|---|---|---|---|---|
| 1 | 350.4 ms | 327.7 ms | 332.6 ms | 332.6 ms | 1.00x |
| 2 | 299.0 ms | 283.6 ms | 304.6 ms | 299.0 ms | 1.11x |
| 4 | 237.8 ms | 293.5 ms | 224.5 ms | 237.8 ms | 1.40x |

Key observations:
- 1 to 2 threads (two physical cores): 1.11x speedup. Modest gain because the
  48 MB table far exceeds L3 (6 MB), making lookup memory-bandwidth bound.
  MD5 computation dominates at single thread; at 2 threads DRAM bandwidth
  becomes the bottleneck.
- 2 to 4 threads (adding hyperthreads): further improvement to 1.40x overall.
  HT allows each physical core to issue memory requests from two logical
  threads simultaneously, partially hiding DRAM latency.
- These numbers will be more pronounced on the full 14.3M dataset.

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

Key observations:
- At 1 thread, naive and stdmap are both about 6.5% slower than tiny pointer.
  The difference is smaller than the isolated lookup benchmark (47 vs 50 vs
  282 ns/lookup) because MD5 computation dominates each iteration; the table
  lookup is one component, not the whole cost.
- At 4 threads, tiny pointer maintains its lead: 9.9% faster than naive and
  13.3% faster than stdmap. The gap widens slightly because with more
  concurrent memory traffic the smaller slot size of tiny pointer (24 vs 32
  bytes) fits more entries per cache line and reduces DRAM bandwidth pressure.
- stdmap scales least well (1.32x vs 1.41x) because its heap-allocated node
  structure produces more scattered memory accesses under concurrent load,
  increasing effective DRAM latency per lookup.
- The ordering is consistent across all thread counts: tinyptr < naive < stdmap.

---

## Known Limitations and Future Optimizations

### SIMD MD5 (AVX2 / AVX-512)

**Background.** Every candidate password must be hashed before it can be
compared against the target. The current implementation does this one password
at a time using OpenSSL's standard EVP API, which is a scalar (non-vectorized)
path. Modern CPUs expose SIMD (Single Instruction, Multiple Data) registers
that can operate on many values simultaneously. For MD5 specifically, AVX2
(256-bit registers, supported on the i3-1115G4) can hash 8 independent
passwords in the time it would otherwise take to hash one. AVX-512 (512-bit,
not available on this CPU) extends that to 16. This is the technique used by
production password crackers such as hashcat, and it is the primary reason
dedicated cracking tools are orders of magnitude faster than a naive loop.

**Why it is not implemented here.** A program is only as fast as its slowest
stage. The perf stat results for the tiny pointer run show 5.1 billion cycles
against 11.6 billion instructions. When cycles exceed instructions the CPU is
stalling, meaning it is waiting for data to arrive from memory rather than
waiting for computation to finish. The hash table for the 1M subset is 48 MB,
which is 8x larger than this CPU's 6 MB L3 cache. Every lookup that misses
the cache must go to DRAM, which takes roughly 60-80 ns compared to a 4 ns
L3 hit. Because nearly every lookup in a realistic cracking workload is a miss
(the target password is typically found near the end of the candidate list),
the worker spends most of its time waiting on DRAM, not computing MD5.

Making MD5 8x faster with AVX2 would not produce an 8x end-to-end speedup
because the lookup stall, not the hash computation, is the dominant cost. The
optimization would be largely hidden by the memory bottleneck.

**When SIMD MD5 does matter.** The compute-to-memory balance shifts in two
scenarios. First, if the table fits in L3 cache, lookups return in ~4 ns and
MD5 computation becomes the bottleneck. This requires either a much smaller
wordlist or server-class hardware with 32 MB or more of L3. Second, if the
cracker generates candidate mutations on the fly (leetspeak substitutions,
appended digits, capitalization variants) rather than reading from a static
list, the hash rate directly determines throughput because there is no table
lookup at all until a match is found.

**Implementation path.** Intel's isa-l_crypto library provides a multi-buffer
MD5 API designed for exactly this use case. The worker loop would be
restructured to accumulate a batch of N candidates, submit the batch to the
SIMD hasher, then scan all N digests for a match. The batch size matches the
SIMD width: 8 for AVX2, 16 for AVX-512.

---

## Next Steps

- Run full 14.3M entry benchmarks and save to results/perf_full.txt
- Full dataset cracker comparison across all three implementations
- Generate graphs from results/ using src/python/plot_results.py
