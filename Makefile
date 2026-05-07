CXXFLAGS_RELEASE = -std=c++17 -O2 -march=native
CXXFLAGS_RELEASE += -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO

CXXFLAGS_DEBUG = -std=c++17 -O0 -g -fsanitize=address,undefined

CXXFLAGS_TSAN = -std=c++17 -O1 -g -fsanitize=thread

LDFLAGS = -lssl -lcrypto -lpthread

.PHONY: all debug tsan test bench clean

all:

debug:

tsan:

test: build/test_tiny_ptr build/test_hash_table build/test_hash_table_naive build/test_hash_table_stdmap
	./build/test_tiny_ptr
	./build/test_hash_table
	./build/test_hash_table_naive
	./build/test_hash_table_stdmap

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

clean:
	rm -rf build
