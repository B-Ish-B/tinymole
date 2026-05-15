# tinymole

[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![OpenSSL](https://img.shields.io/badge/crypto-OpenSSL%203.x-red?logo=openssl&logoColor=white)](https://www.openssl.org/)
[![Nix](https://img.shields.io/badge/built%20with-Nix-5277C3?logo=nixos&logoColor=white)](flake.nix)
[![Last commit](https://img.shields.io/github/last-commit/B-Ish-B/tinymole)](https://github.com/B-Ish-B/tinymole/commits/main)

**Multithreaded dictionary password cracker with four hash table implementations, benchmarked against the full 14.3 million entry RockYou wordlist.**

The core question is whether compact pointer encoding -- as formalized by [Bender et al. (ACM Transactions on Algorithms, 2024)](https://arxiv.org/abs/2111.12800) -- reduces lookup latency in a memory-bound cracking workload. Four implementations are compared: a bit-packed tiny pointer table, a naive full-offset table, a probabilistic key-dependent table derived from the paper, and `std::unordered_map` as a baseline.

See [docs/write_up.md](docs/write_up.md) for the full paper with methodology, statistical analysis, and discussion.

---

## Results

Benchmarked on a 4-core Intel CPU (4.1 GHz, 6 MB L3, performance governor) against the full RockYou dataset (14,344,391 entries). All three custom implementations use 16-byte open-addressed slots. End-to-end times use 4 threads and a mid-list target (~entry 7M), reported as the mean of 5 hyperfine runs.

| Implementation | Pointer | Miss (ns) | Hit (ns) | End-to-end (4t) | Memory |
|---|---|---|---|---|---|
| TinyPtr (bit-packed) | 27-bit offset + 5-bit length | 44.5 | **13.9** | **9.2 s** | 680 MB |
| Naive (full offset) | 32-bit raw offset | 42.7 | 27.7 | 10.3 s | 680 MB |
| Prob (key-dependent) | 6-bit DEREFERENCE | 43.8 | 33.4 | 11.5 s | 1194 MB |
| std::unordered_map | 64-bit heap pointer | 305.9 | 392.5 | 17.2 s | 1912 MB |

**Key findings:**

- All three custom tables beat `std::unordered_map` by **6.9x** on miss latency and **1.86x** end-to-end.
- TinyPtr achieves **2.0x faster hit lookups** than Naive (13.9 ns vs 27.7 ns) by encoding password length directly in the pointer, eliminating a dependent pool read on the hit path.
- Probabilistic pointers (6 bits, key-dependent) incur **2.4x more LLC cache misses** than TinyPtr due to fixed 32-byte pool slots, making them 1.25x slower end-to-end despite similar miss-path latency.
- Thread scaling is sub-linear: TinyPtr scales from 2.9 MH/s (1 thread) to 3.6 MH/s (4 threads), saturating the memory bus before exhausting CPU capacity.

![Lookup latency by workload](results/figures/fig2_workload_comparison.png)

---

## Quick Start

```bash
git clone <repo>
cd tinymole
direnv allow
uv sync
make all
make test
# crack the MD5 of "password"
./build/cracker --hash 5f4dcc3b5aa765d61d8327deb882cf99 --wordlist data/test_wordlist.txt
```

Expected output:

```
cracked: password
```

To crack against the full RockYou table, place `rockyou.txt` at `data/rockyou.txt` (see [Step 1](#step-1-obtain-rockyoutxt)), then:

```bash
make crack HASH=5f4dcc3b5aa765d61d8327deb882cf99
```

---

## Setup

### Linux and macOS

One-time machine setup:

```bash
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
eval "$(direnv hook bash)"   # or zsh
```

First-time setup after cloning:

```bash
git clone <repo>
cd tinymole
direnv allow
uv sync
make all
make test
```

After the one-time `direnv allow`, every subsequent `cd` activates the environment automatically.

### Windows (WSL2)

Nix does not run natively on Windows. Use WSL2 with Ubuntu 22.04 or later.

```powershell
# PowerShell (Administrator)
wsl --install
```

Restart, then inside the WSL2 terminal:

```bash
sh <(curl -L https://nixos.org/nix/install) --daemon
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
nix profile install nixpkgs#direnv
eval "$(direnv hook bash)"
```

Clone the repo inside the WSL2 home directory (not under `/mnt/c/`) and follow the Linux setup steps above.

---

## Pipeline

### Step 1: Obtain rockyou.txt

The wordlist is not included in the repo. Place it at `data/rockyou.txt`:

```bash
# Kali or Parrot (already on disk)
cp /usr/share/wordlists/rockyou.txt data/rockyou.txt
# or if gzipped
gunzip -c /usr/share/wordlists/rockyou.txt.gz > data/rockyou.txt
```

### Step 2: Run frequency analysis

```bash
uv run src/python/frequency_analysis.py
```

Reads `data/rockyou.txt` and produces:

| Output | Path |
|---|---|
| Frequency-ranked candidate list | `data/candidates_ranked.txt` |
| Substitution pattern frequencies | `data/substitution_rules.json` |
| All-substitutions chart | `results/substitution_analysis_all.png` |
| Leet-speak chart | `results/substitution_analysis_leet.png` |

For a faster test on a subset:

```bash
uv run src/python/frequency_analysis.py --limit 1000000 --top 10000
```

### Step 3: Crack a hash

```bash
make crack HASH=<hex>
# or with options
make crack HASH=<hex> ALGO=md5 THREADS=4 WORDLIST=data/rockyou.txt
```

The cracker loads the wordlist into the TinyPtr hash table, then iterates through `data/candidates_ranked.txt` in frequency order until a match is found.

---

## Usage

```
./build/cracker --hash <hex> [options]

  --hash       <hex>   hex-encoded target hash (required)
  --algo       <name>  hash algorithm: md5, sha1, sha256  (default: md5)
  --wordlist   <path>  wordlist to build the lookup table  (default: data/rockyou.txt)
  --candidates <path>  ranked candidate iteration order    (defaults to wordlist)
  --threads    <n>     number of worker threads            (default: 4)
  --log-path   <path>  log file path                       (default: logs/cracker.log)
```

`--hash` must be the correct hex length for the chosen algorithm: 32 chars for MD5, 40 for SHA-1, 64 for SHA-256.

```bash
# MD5
./build/cracker --hash 5f4dcc3b5aa765d61d8327deb882cf99 --threads 4

# SHA-256
./build/cracker \
  --hash 5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8 \
  --algo sha256 --threads 4
```

Logs are written to `logs/cracker.log` on every run.

---

## Terminal UI

```bash
make tui
```

Requires the cracker binary to be built first (`make all`). Displays a configuration form and a live log panel that tails `logs/cracker.log` as the cracker runs. Implemented in `src/python/tui.py` using [Textual](https://github.com/Textualize/textual).

---

## Makefile Targets

| Target | What it does |
|---|---|
| `make all` | Release build (`-O2 -march=native`) |
| `make debug` | Debug build with AddressSanitizer and UBSan |
| `make tsan` | Thread sanitizer build |
| `make test` | Build and run all unit tests (C++) |
| `make bench` | Google Benchmark throughput: miss/hit/mixed, 5 reps, all 4 implementations; writes `results/benchmark.csv` |
| `make latency` | RDTSC per-lookup latency distribution: p50/p95/p99/p99.9, 2M samples; writes `results/latency_percentiles.txt` |
| `make hyperfine` | End-to-end wall-clock timing via hyperfine: 5 runs, 2 warmups, 4 threads; writes `results/hyperfine.csv` and `results/hyperfine.md` |
| `make crack HASH=<hex>` | Run the cracker (builds table, runs frequency analysis if needed) |
| `make tui` | Launch the interactive terminal UI |
| `make lookup HASH=<hex>` | Query the weakpass API before cracking locally |
| `make clean` | Remove all build artifacts |

To run the full benchmark suite and regenerate all figures:

```bash
make bench
make latency
make hyperfine
uv run python3 results/plot_benchmarks.py
```

Output figures go to `results/figures/`. Raw data files are in `results/`.

---

## Tests

### C++ (Google Test)

```bash
make test
```

Expected output:

```
[  PASSED  ] 10 tests.   # tiny_ptr encoding and PasswordPool
[  PASSED  ] 13 tests.   # HashTable insert, lookup, collision handling
[  PASSED  ] 9 tests.    # HashTableNaive
[  PASSED  ] 8 tests.    # HashTableStdMap
[  PASSED  ] 11 tests.   # HashTableProb (including key-dependent DEREFERENCE)
[  PASSED  ] 11 tests.   # cracker integration, thread partitioning
```

### Python (pytest)

```bash
uv run pytest -v
```

No wordlist files required; all tests use in-memory fixtures.

| Test file | What it covers |
|---|---|
| `tests/test_frequency_analysis.py` | Leet normalization, substitution weight tallying, variant generation |
| `tests/test_merge_wordlists.py` | UTF-8/latin-1 loading, deduplication, append/prepend ordering |

---

## Project Structure

```
src/cpp/
  hash_table.hpp / .cpp        TinyPtr implementation (27-bit offset + 5-bit length)
  hash_table_naive.hpp / .cpp  Naive full-offset implementation
  hash_table_prob.hpp / .cpp   Probabilistic key-dependent implementation
  hash_table_stdmap.hpp / .cpp std::unordered_map wrapper
  tiny_ptr.hpp                 Pointer encoding and PasswordPool
  cracker.hpp / .cpp           Templated multithreaded worker
  main.cpp                     CLI entry point
  bench_lookup.cpp             Google Benchmark: miss/hit/mixed throughput
  bench_latency.cpp            RDTSC percentile latency tool
  cracker_bench.cpp            End-to-end throughput benchmark

src/python/
  frequency_analysis.py        Candidate ranking pipeline
  tui.py                       Textual terminal UI
  weakpass_lookup.py           Online hash lookup via weakpass API

results/
  benchmark.csv                Raw Google Benchmark output
  latency_percentiles.txt      RDTSC percentile results
  hyperfine.csv / .md          Hyperfine wall-clock results
  plot_benchmarks.py           Figure and table generation (uv run python3 ...)
  figures/                     PDF and PNG figures (fig1 through fig8)

docs/
  write_up.md                  Full paper: background, design, results, discussion
  dev-benchmarks.md            Benchmark methodology notes
```

---

## References

Bender, M. A., Farach-Colton, M., Kuszmaul, J., Kuszmaul, W., and Liu, M. (2024). Tiny Pointers. *ACM Transactions on Algorithms*, 20(3), Article 23. https://arxiv.org/abs/2111.12800
