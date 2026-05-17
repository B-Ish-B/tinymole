CXXFLAGS_RELEASE = -std=c++17 -O2
CXXFLAGS_RELEASE += -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO

CXXFLAGS_DEBUG = -std=c++17 -O0 -g -fsanitize=address,undefined

CXXFLAGS_TSAN = -std=c++17 -O1 -g -fsanitize=thread

LDFLAGS = -lssl -lcrypto -lpthread

HASH     ?=
ALGO     ?= md5
THREADS  ?= 4
WORDLIST ?= data/rockyou.txt

.PHONY: all debug tsan test bench hyperfine latency crack lookup tui clean help

help:  ## Show this help message and the list of targets
	@echo "Usage: make <target> [HASH=<hex>] [ALGO=md5|sha1|sha256] [THREADS=<n>] [WORDLIST=<path>]"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "} {printf "  %-20s %s\n", $$1, $$2}'

all: build/cracker  ## Release build of the cracker (-O2)

debug: build/cracker_debug  ## Debug build with AddressSanitizer + UBSan
tsan: build/cracker_tsan  ## Build with ThreadSanitizer

test: build/test_tiny_ptr build/test_hash_table build/test_hash_table_naive build/test_hash_table_stdmap build/test_hash_table_prob build/test_cracker  ## Build and run all C++ unit tests
	./build/test_tiny_ptr
	./build/test_hash_table
	./build/test_hash_table_naive
	./build/test_hash_table_stdmap
	./build/test_hash_table_prob
	./build/test_cracker

build/test_tiny_ptr: tests/test_tiny_ptr.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $< -o $@ -lgtest -lgtest_main -lpthread

build/test_hash_table: tests/test_hash_table.cpp src/cpp/hashtable/hash_table.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

build/test_hash_table_naive: tests/test_hash_table_naive.cpp src/cpp/hashtable/hash_table_naive.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

build/test_hash_table_stdmap: tests/test_hash_table_stdmap.cpp src/cpp/hashtable/hash_table_stdmap.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

build/test_hash_table_prob: tests/test_hash_table_prob.cpp src/cpp/hashtable/hash_table_prob.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

build/test_cracker: tests/test_cracker.cpp src/cpp/cracker/cracker.cpp src/cpp/hashtable/hash_table.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

CRACKER_SRCS      = src/cpp/cracker/main.cpp src/cpp/cracker/cracker.cpp src/cpp/hashtable/hash_table.cpp
BENCH_CRACKER_SRCS = src/cpp/bench/bench_cracker.cpp src/cpp/cracker/cracker.cpp \
                     src/cpp/hashtable/hash_table.cpp src/cpp/hashtable/hash_table_naive.cpp \
                     src/cpp/hashtable/hash_table_stdmap.cpp src/cpp/hashtable/hash_table_prob.cpp

build/cracker: $(CRACKER_SRCS)
	mkdir -p build logs
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/cracker_debug: $(CRACKER_SRCS)
	mkdir -p build logs
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ $(LDFLAGS)

build/cracker_tsan: $(CRACKER_SRCS)
	mkdir -p build logs
	$(CXX) $(CXXFLAGS_TSAN) -I. $^ -o $@ $(LDFLAGS)

build/bench_cracker: $(BENCH_CRACKER_SRCS)
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

# MD5 of "jimmyisno1" -- entry ~7M in rockyou, forces real crack work across all threads
BENCH_HASH    ?= 1637ff9c1826eb09071d234ea6b5563b
BENCH_THREADS ?= 4
HYPERFINE     := $(shell which hyperfine 2>/dev/null || echo /nix/store/fqz9ffja5czl5zrd2bg88ik8axr2pi7i-hyperfine-1.20.0/bin/hyperfine)

bench: build/bench_lookup build/perf_tinyptr build/perf_naive build/perf_stdmap build/perf_prob  ## Google Benchmark throughput (miss/hit/mixed, 5 reps)
	mkdir -p results
	./build/bench_lookup --benchmark_repetitions=5 --benchmark_format=csv > results/benchmark.csv
	@echo "Results written to results/benchmark.csv"

hyperfine: build/bench_cracker  ## End-to-end wall-clock timing with hyperfine (5 runs, 2 warmups)
	mkdir -p results
	$(HYPERFINE) \
	  --warmup 2 \
	  --runs 5 \
	  --export-csv results/hyperfine.csv \
	  --export-markdown results/hyperfine.md \
	  "build/bench_cracker --impl tinyptr --hash $(BENCH_HASH) --threads $(BENCH_THREADS)" \
	  "build/bench_cracker --impl naive   --hash $(BENCH_HASH) --threads $(BENCH_THREADS)" \
	  "build/bench_cracker --impl stdmap  --hash $(BENCH_HASH) --threads $(BENCH_THREADS)" \
	  "build/bench_cracker --impl prob    --hash $(BENCH_HASH) --threads $(BENCH_THREADS)"
	@echo "Results written to results/hyperfine.csv and results/hyperfine.md"

build/bench_lookup: src/cpp/bench/bench_lookup.cpp src/cpp/hashtable/hash_table.cpp src/cpp/hashtable/hash_table_naive.cpp src/cpp/hashtable/hash_table_stdmap.cpp src/cpp/hashtable/hash_table_prob.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS) -lbenchmark -lbenchmark_main

build/perf_tinyptr: src/cpp/bench/perf_tinyptr.cpp src/cpp/hashtable/hash_table.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/perf_naive: src/cpp/bench/perf_naive.cpp src/cpp/hashtable/hash_table_naive.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/perf_stdmap: src/cpp/bench/perf_stdmap.cpp src/cpp/hashtable/hash_table_stdmap.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/perf_prob: src/cpp/bench/perf_prob.cpp src/cpp/hashtable/hash_table_prob.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/bench_latency: src/cpp/bench/bench_latency.cpp src/cpp/hashtable/hash_table.cpp \
                     src/cpp/hashtable/hash_table_naive.cpp src/cpp/hashtable/hash_table_stdmap.cpp \
                     src/cpp/hashtable/hash_table_prob.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

latency: build/bench_latency  ## RDTSC latency percentiles (2M samples after 500K warmup)
	mkdir -p results
	./build/bench_latency | tee results/latency_percentiles.txt
	@echo "Results written to results/latency_percentiles.txt"

crack: build/cracker data/candidates_ranked.txt  ## Run the cracker against HASH (requires HASH=<hex>)
	@if [ -z "$(HASH)" ]; then echo "error: HASH is required. Usage: make crack HASH=<hex>"; exit 1; fi
	./build/cracker --hash $(HASH) --algo $(ALGO) --wordlist $(WORDLIST) --candidates data/candidates_ranked.txt --threads $(THREADS)

tui: build/cracker  ## Launch the interactive terminal UI
	uv run src/python/tui.py

lookup:  ## Online weakpass.com lookup for HASH (requires HASH=<hex>)
	@if [ -z "$(HASH)" ]; then echo "error: HASH is required. Usage: make lookup HASH=<hex>"; exit 1; fi
	uv run src/python/weakpass_lookup.py --hash $(HASH) --algo $(ALGO)

data/candidates_ranked.txt: data/rockyou.txt
	uv run src/python/frequency_analysis.py

clean:  ## Remove build artifacts
	rm -rf build
