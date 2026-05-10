/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Google Benchmark microbenchmarks comparing lookup throughput
 * across all three hash table implementations (tiny pointer, naive, stdmap).
 * Runs against a real wordlist with miss-query workloads to reflect typical
 * cracker behavior. Output written to results/benchmark.csv via make bench.
 */

#include <benchmark/benchmark.h>
#include <openssl/evp.h>

#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table.hpp"
#include "src/cpp/hash_table_naive.hpp"
#include "src/cpp/hash_table_stdmap.hpp"

// ---------------------------------------------------------------------------
// Shared setup: load the wordlist once, build miss queries.
//
// Miss queries are MD5 hashes of strings that are guaranteed not to be in
// the wordlist. We generate them by hashing "MISS_QUERY_<i>" for i in
// [0, N_QUERIES). This reflects real cracker workload where the vast
// majority of candidate hashes do not match any entry in the table.
// ---------------------------------------------------------------------------

static constexpr size_t N_QUERIES = 1000000;
static const char* WORDLIST_PATH  = "data/rockyou_1m.txt";

struct BenchState {
    PasswordPool   pool;
    HashTable      tinyptr;
    HashTableNaive naive;
    HashTableStdMap stdmap;
    std::vector<std::array<uint8_t, 16>> miss_queries;

    BenchState() {
        // Load tinyptr and naive from the same pool so memory layout is identical
        {
            std::ifstream f(WORDLIST_PATH);
            tinyptr.load(f, pool);
        }
        {
            PasswordPool pool2;
            std::ifstream f(WORDLIST_PATH);
            naive.load(f, pool2);
            // pool2 is discarded after this scope; naive keeps its own offsets
            // We keep a separate pool for naive below
        }
        {
            std::ifstream f(WORDLIST_PATH);
            stdmap.load(f);
        }

        // Generate miss queries
        miss_queries.reserve(N_QUERIES);
        for (size_t i = 0; i < N_QUERIES; ++i) {
            std::string s = "MISS_QUERY_" + std::to_string(i);
            std::array<uint8_t, 16> h;
            unsigned int hlen = 16;
            EVP_Digest(s.data(), s.size(), h.data(), &hlen, EVP_md5(), nullptr);
            miss_queries.push_back(h);
        }
    }
};

// Naive needs its own pool since it stores raw offsets into whatever pool
// was used during load(). Rebuild it here with a persistent pool.
struct NaiveState {
    PasswordPool   pool;
    HashTableNaive table;

    NaiveState() {
        std::ifstream f(WORDLIST_PATH);
        table.load(f, pool);
    }
};

static BenchState*  g_bench  = nullptr;
static NaiveState*  g_naive  = nullptr;

// Called once before all benchmarks run
static void global_setup() {
    if (!g_bench) g_bench = new BenchState();
    if (!g_naive) g_naive = new NaiveState();
}

// ---------------------------------------------------------------------------
// Benchmark: tiny pointer table miss lookups
// ---------------------------------------------------------------------------
static void BM_TinyPtr_Miss(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->miss_queries[idx % N_QUERIES];
        auto result = g_bench->tinyptr.lookup(q.data(), g_bench->pool.base());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ---------------------------------------------------------------------------
// Benchmark: naive full-offset table miss lookups
// ---------------------------------------------------------------------------
static void BM_Naive_Miss(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->miss_queries[idx % N_QUERIES];
        auto result = g_naive->table.lookup(q.data(), g_naive->pool.base());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ---------------------------------------------------------------------------
// Benchmark: std::unordered_map miss lookups
// ---------------------------------------------------------------------------
static void BM_StdMap_Miss(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->miss_queries[idx % N_QUERIES];
        auto result = g_bench->stdmap.lookup(q.data());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(BM_TinyPtr_Miss)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Naive_Miss)  ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_StdMap_Miss) ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
