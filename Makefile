CXXFLAGS_RELEASE = -std=c++17 -O2 -march=native
CXXFLAGS_RELEASE += -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO

CXXFLAGS_DEBUG = -std=c++17 -O0 -g -fsanitize=address,undefined

CXXFLAGS_TSAN = -std=c++17 -O1 -g -fsanitize=thread

LDFLAGS = -lssl -lcrypto -lpthread

HASH     ?=
ALGO     ?= md5
THREADS  ?= 4
WORDLIST ?= data/rockyou.txt

.PHONY: all debug tsan test bench crack lookup clean

all: build/cracker

debug: build/cracker_debug

tsan: build/cracker_tsan

test: build/test_tiny_ptr build/test_hash_table build/test_hash_table_naive build/test_hash_table_stdmap build/test_cracker
	./build/test_tiny_ptr
	./build/test_hash_table
	./build/test_hash_table_naive
	./build/test_hash_table_stdmap
	./build/test_cracker

build/test_tiny_ptr: tests/test_tiny_ptr.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $< -o $@ -lgtest -lgtest_main -lpthread

build/test_hash_table: tests/test_hash_table.cpp src/cpp/hash_table.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

build/test_hash_table_naive: tests/test_hash_table_naive.cpp src/cpp/hash_table_naive.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

build/test_hash_table_stdmap: tests/test_hash_table_stdmap.cpp src/cpp/hash_table_stdmap.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

build/test_cracker: tests/test_cracker.cpp src/cpp/cracker.cpp src/cpp/hash_table.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

CRACKER_SRCS      = src/cpp/main.cpp src/cpp/cracker.cpp src/cpp/hash_table.cpp
CRACKER_BENCH_SRCS = src/cpp/cracker_bench.cpp src/cpp/cracker.cpp \
                     src/cpp/hash_table.cpp src/cpp/hash_table_naive.cpp \
                     src/cpp/hash_table_stdmap.cpp

build/cracker: $(CRACKER_SRCS)
	mkdir -p build logs
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/cracker_debug: $(CRACKER_SRCS)
	mkdir -p build logs
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ $(LDFLAGS)

build/cracker_tsan: $(CRACKER_SRCS)
	mkdir -p build logs
	$(CXX) $(CXXFLAGS_TSAN) -I. $^ -o $@ $(LDFLAGS)

build/cracker_bench: $(CRACKER_BENCH_SRCS)
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

bench: build/bench_lookup build/perf_tinyptr build/perf_naive build/perf_stdmap
	./build/bench_lookup --benchmark_format=csv > results/benchmark.csv
	@echo "Results written to results/benchmark.csv"

build/bench_lookup: src/cpp/bench_lookup.cpp src/cpp/hash_table.cpp src/cpp/hash_table_naive.cpp src/cpp/hash_table_stdmap.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS) -lbenchmark -lbenchmark_main

build/perf_tinyptr: src/cpp/perf_tinyptr.cpp src/cpp/hash_table.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/perf_naive: src/cpp/perf_naive.cpp src/cpp/hash_table_naive.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

build/perf_stdmap: src/cpp/perf_stdmap.cpp src/cpp/hash_table_stdmap.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_RELEASE) -I. $^ -o $@ $(LDFLAGS)

crack: build/cracker data/candidates_ranked.txt
	@if [ -z "$(HASH)" ]; then echo "error: HASH is required. Usage: make crack HASH=<hex>"; exit 1; fi
	./build/cracker --hash $(HASH) --algo $(ALGO) --wordlist $(WORDLIST) --candidates data/candidates_ranked.txt --threads $(THREADS)

lookup:
	@if [ -z "$(HASH)" ]; then echo "error: HASH is required. Usage: make lookup HASH=<hex>"; exit 1; fi
	uv run src/python/weakpass_lookup.py --hash $(HASH) --algo $(ALGO)

data/candidates_ranked.txt: data/rockyou.txt
	uv run src/python/frequency_analysis.py

clean:
	rm -rf build
