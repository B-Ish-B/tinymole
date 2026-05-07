# Multithreaded Password Cracker with Tiny Pointer Hash Tables and Frequency-Ranked Candidate Generation

This project builds a high-performance password cracker that demonstrates three interconnected computer science concepts: statistical frequency analysis for candidate ranking, a space-efficient hash table using tiny pointers from a 2024 peer-reviewed paper, and concurrent multithreaded execution with zero lock contention. The system is benchmarked at each layer to produce measurable, graphable results for the project write-up. The project directly satisfies two required course topics: cryptography, covered through the treatment of password hashing algorithms (MD5, SHA-1, SHA-256, bcrypt) and hash-to-plaintext lookup tables; and parallel concurrency, covered through the multithreaded candidate evaluation loop, keyspace partitioning across threads, and the thread-safe shared read-only data structure design.

## Setup

### One-time machine setup

```bash
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf

eval "$(direnv hook bash)"   # or zsh
```

### First-time setup after cloning

```bash
git clone <repo>
cd csc255-password-cracker
direnv allow
uv sync
make all
make test
```

After the one-time `direnv allow`, every subsequent `cd` into the project directory activates the environment automatically.

## Makefile Targets

| Target | Purpose | Flags |
|---|---|---|
| `make all` | Default release build, used for all benchmarks | `CXXFLAGS_RELEASE` |
| `make debug` | Development build with address + undefined sanitizers | `CXXFLAGS_DEBUG` |
| `make tsan` | Thread sanitizer build, run before any multithreaded benchmark | `CXXFLAGS_TSAN` |
| `make test` | Build and run all Google Test unit tests | `CXXFLAGS_DEBUG` |
| `make bench` | Build and run Google Benchmark microbenchmarks | `CXXFLAGS_RELEASE` |
| `make clean` | Remove all build artifacts | n/a |
