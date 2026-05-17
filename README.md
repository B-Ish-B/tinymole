# tinymole

[![CI](https://github.com/B-Ish-B/tinymole/actions/workflows/ci.yml/badge.svg)](https://github.com/B-Ish-B/tinymole/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![OpenSSL](https://img.shields.io/badge/crypto-OpenSSL%203.x-red?logo=openssl&logoColor=white)](https://www.openssl.org/)
[![Nix](https://img.shields.io/badge/built%20with-Nix-5277C3?logo=nixos&logoColor=white)](flake.nix)
[![Last commit](https://img.shields.io/github/last-commit/B-Ish-B/tinymole)](https://github.com/B-Ish-B/tinymole/commits/main)

Multithreaded dictionary password cracker comparing four hash table implementations under a realistic 14.3-million-entry RockYou workload. Built around a bit-packed pointer design derived from [Bender et al. (ACM ToA, 2024)](https://doi.org/10.1145/3700594).

- Four hash table designs: bit-packed 32-bit pointer, naive 32-bit offset, 6-bit key-dependent (DEREFERENCE), and `std::unordered_map` baseline
- TinyPtr and Naive use flat linear probing; Prob uses linear probing within fixed-size buckets plus two-choice fallback to a secondary region
- 96-bit truncated MD5 keys, multi-threaded cracker with frequency-ranked candidate ordering
- Benchmarks: Google Benchmark, RDTSC percentiles, `perf_event_open` counters, `hyperfine` wall time

![TUI splash screen](docs/tui.png)

---

## Results

Benchmarked on an Intel i3-1115G4 (Tiger Lake, 2 cores / 4 threads, 4.1 GHz boost, 6 MiB L3) against all 14,344,391 RockYou entries. End-to-end times use 4 threads targeting a mid-list entry, mean of 5 hyperfine runs.

| Implementation | Pointer | Miss (ns) | Hit (ns) | End-to-end (4t) | Memory (MiB) |
|---|---|---|---|---|---|
| TinyPtr (bit-packed) | 27-bit offset + 5-bit length | 44.5 | **13.9** | **9.2 s** | 680 |
| Naive (full offset) | 32-bit raw offset | **42.7** | 27.7 | 10.3 s | 680 |
| Prob (key-dependent) | 6-bit DEREFERENCE | 43.8 | 33.4 | 11.5 s | 1194 |
| std::unordered_map | 64-bit heap pointer | 305.9 | 392.5 | 17.2 s | 1912 |

- All three custom tables beat `std::unordered_map` by **6.9x** on miss latency and **1.86x** end-to-end.
- TinyPtr achieves **2.0x faster hit lookups** than Naive by encoding password length in the pointer, eliminating one dependent pool read. (Microbenchmark times `string_view` construction; a client that reads the password contents still pays one pool cache-line miss.)
- With perf counters scoped to the lookup loop alone, all three custom designs hit the same **~5.3 LLC misses per lookup**; StdMap doubles that at 10.9 due to pointer chasing.
- Probabilistic 6-bit pointers are 1.25x slower end-to-end primarily because building the two-level table takes ~2.4 s longer; the lookup phase itself finishes within 0.15 s of TinyPtr at 4 threads.

![End-to-end crack time](results/figures/fig7_walltime.png)

![Lookup latency by workload](results/figures/fig2_workload_comparison.png)

Throughput saturates around 3.4-3.7 MH/s at 4 threads, which fully populates the 2 cores × 2 hyperthreads of the test machine (memory-subsystem bound, not CPU bound). Full analysis in [docs/write_up.md](docs/write_up.md).

---

## Setup

### Linux and macOS

Install Nix (if not already installed) and enable flakes + direnv:

```bash
sh <(curl -L https://nixos.org/nix/install) --daemon
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
nix profile install nixpkgs#direnv
eval "$(direnv hook bash)"   # or zsh
```

### Windows (WSL2)

```powershell
wsl --install   # PowerShell, Administrator
```

Then inside WSL2, follow the Linux/macOS steps above. Clone inside the WSL2 home directory, not `/mnt/c/`.

---

## Quick Start

After Setup is done:

```bash
git clone https://github.com/B-Ish-B/tinymole.git && cd tinymole
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

## Usage

```
./build/cracker --hash <hex> [options]

  --hash       <hex>   target hash (required)
  --algo       <name>  md5 | sha1 | sha256  (default: md5)
  --wordlist   <path>  wordlist for the lookup table  (default: data/rockyou_1m.txt)
  --candidates <path>  ranked candidate order  (defaults to wordlist)
  --threads    <n>     worker threads  (default: 4)
  --log-path   <path>  log file  (default: logs/cracker.log)
```

Run frequency analysis once to build a ranked candidate list:

```bash
uv run src/python/frequency_analysis.py
make crack HASH=<hex>   # uses candidates_ranked.txt automatically
```

---

## Makefile Targets

| Target | What it does |
|---|---|
| `make help` | List every target with a one-line description |
| `make all` | Release build (`-O2`) |
| `make test` | Build and run all unit tests |
| `make bench` | Google Benchmark throughput (miss/hit/mixed, 5 reps) |
| `make latency` | RDTSC latency percentiles, 2M samples |
| `make hyperfine` | End-to-end wall-clock timing, 5 runs |
| `make crack HASH=<hex>` | Build and run the cracker |
| `make tui` | Launch the terminal UI |
| `make debug` / `make tsan` | Sanitizer builds |
| `make clean` | Remove build artifacts |

Regenerate all figures after running benchmarks:

```bash
make bench && make latency && make hyperfine
uv run python3 src/python/plot_benchmarks.py
```

---

## References

Bender, M. A., Conway, A., Farach-Colton, M., Kuszmaul, W., and Tagliavini, G. (2024). Tiny Pointers. *ACM Transactions on Algorithms*, 21(4), Article 38, 1-43. https://doi.org/10.1145/3700594
