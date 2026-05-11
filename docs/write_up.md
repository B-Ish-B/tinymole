# CSC 255 Project Write-Up

## Introduction

tinymole is a multithreaded password cracker built around a space-efficient open-addressed hash table using tiny pointers. The goal is to demonstrate that reducing per-slot memory usage improves cache utilization and lowers average crack time, especially under multi-core parallelism. The project also includes a frequency analysis pipeline that ranks candidates by statistical likelihood, allowing the cracker to try the most probable passwords first.

## Background

Dictionary-based password cracking loads a wordlist into a lookup structure and iterates through candidates, hashing each one and checking for a match. The bottleneck at scale is memory bandwidth: a large hash table that does not fit in L3 cache causes frequent cache misses on every lookup. Tiny pointers, introduced by Bender et al. (ACM Transactions on Algorithms, 2024), reduce the per-entry storage cost by encoding pointers relative to a shared base address, shrinking slot size from 32 bytes to 24 bytes and allowing more entries to fit per cache line.

## Frequency Analysis

The frequency analysis pipeline processes the RockYou wordlist and computes a ranked candidate list ordered by statistical likelihood. It extracts character substitution patterns (e.g., `a -> @`, `e -> 3`) and scores each password by its base word frequency and the frequency of any substitutions applied. The output is `data/candidates_ranked.txt`, which the cracker uses as its iteration order.

## Tiny Pointers

The hash table uses 24-byte open-addressed slots. Each slot stores a 16-byte hash digest and a 8-byte tiny pointer encoding the offset of the plaintext password within a contiguous memory pool. This avoids per-entry heap allocation and keeps the working set compact. Two baseline implementations are provided for comparison: a naive 32-byte slot table and a `std::unordered_map` wrapper.

## Multithreading

Candidates are partitioned round-robin across worker threads so each thread receives an equivalent statistical distribution. A shared `std::atomic<bool>` signals all threads to stop as soon as one finds a match. The cracker is templated on table type so the same worker runs against all three implementations.

## Logging

Structured logging uses the Quill async logging library. Each run writes timestamped records to `logs/cracker.log` in the format used by the Textual TUI log panel. The TUI can also save the full session log to a timestamped file via the Save Log button.

## Benchmarks

Results pending full RockYou dataset runs. See `results/` for raw data and `docs/dev-benchmarks.md` for methodology.

## Conclusion

To be completed after benchmarks.

## References

Bender, M. A., Farach-Colton, M., Kuszmaul, J., Kuszmaul, W., and Liu, M. (2024). Tiny Pointers. ACM Transactions on Algorithms.
