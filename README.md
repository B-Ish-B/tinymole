# Multithreaded Password Cracker with Tiny Pointer Hash Tables and Frequency-Ranked Candidate Generation

This project builds a high-performance password cracker that demonstrates three interconnected computer science concepts: statistical frequency analysis for candidate ranking, a space-efficient hash table using tiny pointers from a 2024 peer-reviewed paper, and concurrent multithreaded execution with zero lock contention. The system is benchmarked at each layer to produce measurable, graphable results for the project write-up. The project directly satisfies two required course topics: cryptography, covered through the treatment of password hashing algorithms (MD5, SHA-1, SHA-256, bcrypt) and hash-to-plaintext lookup tables; and parallel concurrency, covered through the multithreaded candidate evaluation loop, keyspace partitioning across threads, and the thread-safe shared read-only data structure design.

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
cd csc255-password-cracker
direnv allow
uv sync
make all
make test
```

After the one-time `direnv allow`, every subsequent `cd` into the project directory activates the environment automatically.

### Windows

Nix does not run natively on Windows. You need WSL2 with a Linux distribution (Ubuntu 22.04 or later is recommended).

One-time WSL2 setup (run in PowerShell as Administrator):

```powershell
wsl --install
```

Restart your machine, then open your WSL2 terminal and install Nix:

```bash
sh <(curl -L https://nixos.org/nix/install) --daemon
```

Close and reopen the terminal, then install direnv:

```bash
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
nix profile install nixpkgs#direnv
eval "$(direnv hook bash)"
```

From this point the setup is identical to Linux. Clone the repo inside your WSL2 home directory (not under `/mnt/c/`) and run the first-time setup steps above. All subsequent work should be done from within WSL2.

## Usage

```
./build/cracker --hash <hex> [options]

options:
  --hash       <hex>   hex-encoded hash to crack (required)
  --algo       <name>  hash algorithm: md5, sha1, sha256 (default: md5)
  --wordlist   <path>  wordlist file, one password per line
                       (default: data/rockyou_1m.txt)
  --candidates <path>  frequency-ranked candidate list
                       (defaults to wordlist if not provided)
  --threads    <n>     number of worker threads (default: 4)
  --log-path   <path>  log file path (default: logs/cracker.log)
```

The `--hash` value must be the correct hex length for the chosen algorithm:
32 chars for MD5, 40 for SHA-1, 64 for SHA-256. The cracker validates this
at startup and exits with an error if the lengths do not match.

Example:

```bash
# crack an MD5 hash
./build/cracker --hash 5f4dcc3b5aa765d61d8327deb882cf99 --threads 4

# crack a SHA-256 hash
./build/cracker \
  --hash 5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8 \
  --algo sha256 \
  --threads 4
```

Note: bcrypt requires a separate verification path and is not supported
via `--algo`. bcrypt hashes embed a salt and cost factor and cannot be
cracked with a simple hash comparison loop.

## Makefile Targets

| Target | Purpose | Flags |
|---|---|---|
| `make all` | Default release build, used for all benchmarks | `CXXFLAGS_RELEASE` |
| `make debug` | Development build with address + undefined sanitizers | `CXXFLAGS_DEBUG` |
| `make tsan` | Thread sanitizer build, run before any multithreaded benchmark | `CXXFLAGS_TSAN` |
| `make test` | Build and run all Google Test unit tests | `CXXFLAGS_DEBUG` |
| `make bench` | Build and run Google Benchmark microbenchmarks | `CXXFLAGS_RELEASE` |
| `make clean` | Remove all build artifacts | n/a |
