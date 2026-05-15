# tinymole

[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![OpenSSL](https://img.shields.io/badge/crypto-OpenSSL%203.x-red?logo=openssl&logoColor=white)](https://www.openssl.org/)
[![Nix](https://img.shields.io/badge/built%20with-Nix-5277C3?logo=nixos&logoColor=white)](flake.nix)
[![Last commit](https://img.shields.io/github/last-commit/B-Ish-B/tinymole)](https://github.com/B-Ish-B/tinymole/commits/main)

Multithreaded dictionary password cracker with four hash table implementations, benchmarked against the full 14.3 million entry RockYou wordlist. Built around a bit-packed pointer design derived from [Bender et al. (ACM ToA, 2024)](https://arxiv.org/abs/2111.12800).

![TUI splash screen](docs/tui.png)

```bash
make tui
```

---

## Quick Start

```bash
git clone <repo> && cd tinymole
direnv allow && uv sync
make all && make test
./build/cracker --hash 5f4dcc3b5aa765d61d8327deb882cf99 --wordlist data/test_wordlist.txt
# cracked: password
```

To crack against the full RockYou table, place `rockyou.txt` at `data/rockyou.txt`, then:

```bash
make crack HASH=<hex>
```

---

## Results

Benchmarked on a 4-core Intel CPU (4.1 GHz, 6 MB L3) against all 14,344,391 RockYou entries. End-to-end times use 4 threads targeting a mid-list entry (~7M in), reported as the mean of 5 hyperfine runs.

| Implementation | Pointer | Miss (ns) | Hit (ns) | End-to-end (4t) | Memory |
|---|---|---|---|---|---|
| TinyPtr (bit-packed) | 27-bit offset + 5-bit length | 44.5 | **13.9** | **9.2 s** | 680 MB |
| Naive (full offset) | 32-bit raw offset | 42.7 | 27.7 | 10.3 s | 680 MB |
| Prob (key-dependent) | 6-bit DEREFERENCE | 43.8 | 33.4 | 11.5 s | 1194 MB |
| std::unordered_map | 64-bit heap pointer | 305.9 | 392.5 | 17.2 s | 1912 MB |

- All three custom tables beat `std::unordered_map` by **6.9x** on miss latency and **1.86x** end-to-end.
- TinyPtr achieves **2.0x faster hit lookups** than Naive by encoding password length directly in the pointer, eliminating one pool read on the hit path.
- Probabilistic 6-bit pointers incur **2.4x more LLC cache misses** due to fixed 32-byte pool slots, making Prob 1.25x slower end-to-end despite similar miss-path latency.
- Thread scaling saturates around 3.6-4.1 MH/s at 4 threads on this machine (memory-bandwidth bound).

![Lookup latency by workload](results/figures/fig2_workload_comparison.png)

See [docs/write_up.md](docs/write_up.md) for the full paper with methodology, statistical analysis, and discussion.

---

## Setup

### Linux and macOS

```bash
# one-time machine setup
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
eval "$(direnv hook bash)"   # or zsh

# after cloning
direnv allow
uv sync
make all && make test
```

### Windows (WSL2)

```powershell
# PowerShell (Administrator)
wsl --install
```

Then inside the WSL2 terminal:

```bash
sh <(curl -L https://nixos.org/nix/install) --daemon
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
nix profile install nixpkgs#direnv
eval "$(direnv hook bash)"
```

Clone inside the WSL2 home directory (not `/mnt/c/`) and follow the Linux steps above.

---

## Usage

### CLI

```
./build/cracker --hash <hex> [options]

  --hash       <hex>   target hash (required)
  --algo       <name>  md5 | sha1 | sha256  (default: md5)
  --wordlist   <path>  wordlist for the lookup table  (default: data/rockyou.txt)
  --candidates <path>  ranked candidate iteration order  (defaults to wordlist)
  --threads    <n>     worker threads  (default: 4)
  --log-path   <path>  log file  (default: logs/cracker.log)
```

```bash
# MD5
./build/cracker --hash 5f4dcc3b5aa765d61d8327deb882cf99 --threads 4

# SHA-256
./build/cracker --hash 5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8 \
  --algo sha256 --threads 4
```

### Frequency Analysis

Run once to generate a ranked candidate list before cracking:

```bash
uv run src/python/frequency_analysis.py
# produces data/candidates_ranked.txt and data/substitution_rules.json
```

Then pass both to the cracker so it iterates in probability order:

```bash
./build/cracker --hash <hex> --wordlist data/rockyou.txt --candidates data/candidates_ranked.txt
```

---

## Makefile Targets

| Target | What it does |
|---|---|
| `make all` | Release build (`-O2 -march=native`) |
| `make debug` | Debug build with AddressSanitizer and UBSan |
| `make tsan` | Thread sanitizer build |
| `make test` | Build and run all unit tests |
| `make bench` | Google Benchmark throughput: miss/hit/mixed, 5 reps; writes `results/benchmark.csv` |
| `make latency` | RDTSC latency percentiles: p50/p95/p99/p99.9, 2M samples; writes `results/latency_percentiles.txt` |
| `make hyperfine` | End-to-end wall-clock via hyperfine: 5 runs, 2 warmups; writes `results/hyperfine.csv` |
| `make crack HASH=<hex>` | Build and run the cracker |
| `make tui` | Launch the terminal UI |
| `make lookup HASH=<hex>` | Query the weakpass API before cracking locally |
| `make clean` | Remove build artifacts |

To regenerate all figures after running benchmarks:

```bash
make bench && make latency && make hyperfine
uv run python3 results/plot_benchmarks.py
# figures written to results/figures/
```

---

## Tests

### C++

```bash
make test
```

```
[  PASSED  ] 10 tests.   # tiny_ptr encoding and PasswordPool
[  PASSED  ] 13 tests.   # HashTable insert, lookup, collision handling
[  PASSED  ] 9 tests.    # HashTableNaive
[  PASSED  ] 8 tests.    # HashTableStdMap
[  PASSED  ] 11 tests.   # HashTableProb (key-dependent DEREFERENCE)
[  PASSED  ] 11 tests.   # cracker integration, thread partitioning
```

### Python

```bash
uv run pytest -v
```

| File | Covers |
|---|---|
| `tests/test_frequency_analysis.py` | Leet normalization, substitution tallying, variant generation |
| `tests/test_merge_wordlists.py` | UTF-8/latin-1 loading, deduplication, ordering |

---

## Project Structure

```
src/cpp/
  hash_table.hpp / .cpp          TinyPtr (27-bit offset + 5-bit length)
  hash_table_naive.hpp / .cpp    Naive full-offset table
  hash_table_prob.hpp / .cpp     Probabilistic key-dependent table
  hash_table_stdmap.hpp / .cpp   std::unordered_map wrapper
  tiny_ptr.hpp                   Pointer encoding and PasswordPool
  cracker.hpp / .cpp             Templated multithreaded worker
  bench_lookup.cpp               Google Benchmark: miss/hit/mixed throughput
  bench_latency.cpp              RDTSC latency distribution tool
  cracker_bench.cpp              End-to-end throughput benchmark

src/python/
  frequency_analysis.py          Candidate ranking pipeline
  tui.py                         Textual terminal UI
  weakpass_lookup.py             Online hash lookup

results/
  benchmark.csv                  Google Benchmark raw output
  latency_percentiles.txt        RDTSC percentile results
  hyperfine.csv / .md            Hyperfine wall-clock results
  plot_benchmarks.py             Figure and table generation
  figures/                       PDF and PNG output (fig1 through fig8)

docs/
  write_up.md                    Full paper: design, results, discussion
  dev-benchmarks.md              Benchmark methodology notes
```

---

## References

Bender, M. A., Farach-Colton, M., Kuszmaul, J., Kuszmaul, W., and Liu, M. (2024). Tiny Pointers. *ACM Transactions on Algorithms*, 20(3), Article 23. https://arxiv.org/abs/2111.12800
