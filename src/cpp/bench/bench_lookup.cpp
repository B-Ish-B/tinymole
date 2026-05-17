/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * Google Benchmark microbenchmarks comparing lookup throughput across all
 * four hash table implementations. Three workloads per implementation:
 *   Miss:  query hash guaranteed absent from the table (dominant real-world case).
 *   Hit:   query hash guaranteed present (tests the successful lookup path).
 *   Mixed: 95% miss + 5% hit interleaved, reflecting realistic cracker behavior.
 * Output written to results/benchmark.csv via make bench.
 */

#include <benchmark/benchmark.h>
#include <openssl/evp.h>

#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "src/cpp/hashtable/tiny_ptr.hpp"
#include "src/cpp/hashtable/hash_table.hpp"
#include "src/cpp/hashtable/hash_table_naive.hpp"
#include "src/cpp/hashtable/hash_table_stdmap.hpp"
#include "src/cpp/hashtable/hash_table_prob.hpp"

// ---------------------------------------------------------------------------
// Shared setup: load the wordlist once, build miss queries.
//
// Miss queries are MD5 hashes of strings that are guaranteed not to be in
// the wordlist. We generate them by hashing "MISS_QUERY_<i>" for i in
// [0, N_QUERIES). This reflects real cracker workload where the vast
// majority of candidate hashes do not match any entry in the table.
// ---------------------------------------------------------------------------

static constexpr size_t N_QUERIES   = 1000000;
static constexpr size_t N_HIT_WORDS = 10000;   // wordlist sample for hit queries
static const char* WORDLIST_PATH    = "data/rockyou.txt";

struct BenchState {
    PasswordPool    pool;
    HashTable       tinyptr;
    HashTableNaive  naive;
    HashTableStdMap stdmap;
    HashTableProb   prob;

    std::vector<std::array<uint8_t, 16>> miss_queries;
    std::vector<std::array<uint8_t, 16>> hit_queries;
    std::vector<std::array<uint8_t, 16>> mixed_queries; // 95% miss, 5% hit

    BenchState() {
        {
            std::ifstream f(WORDLIST_PATH);
            tinyptr.load(f, pool);
        }
        {
            PasswordPool pool2;
            std::ifstream f(WORDLIST_PATH);
            naive.load(f, pool2);
        }
        {
            std::ifstream f(WORDLIST_PATH);
            stdmap.load(f);
        }
        {
            std::ifstream f(WORDLIST_PATH);
            prob.load(f);
        }

        // Miss queries: MD5 of "MISS_QUERY_<i>", guaranteed absent.
        miss_queries.reserve(N_QUERIES);
        for (size_t i = 0; i < N_QUERIES; ++i) {
            std::string s = "MISS_QUERY_" + std::to_string(i);
            std::array<uint8_t, 16> h;
            unsigned int hlen = 16;
            EVP_Digest(s.data(), s.size(), h.data(), &hlen, EVP_md5(), nullptr);
            miss_queries.push_back(h);
        }

        // Hit queries: sample N_HIT_WORDS entries evenly spaced across the
        // wordlist, hash them. These are guaranteed present in the table.
        {
            std::vector<std::string> words;
            words.reserve(N_HIT_WORDS);
            std::ifstream f(WORDLIST_PATH);
            std::string line;
            size_t line_num = 0;
            // Stride chosen so we sample evenly across the full 14M-entry file.
            const size_t stride = 1434;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line.size() > TINY_PTR_MAX_LEN) continue;
                if (line_num % stride == 0) words.push_back(line);
                if (words.size() >= N_HIT_WORDS) break;
                ++line_num;
            }
            hit_queries.reserve(N_QUERIES);
            for (size_t i = 0; i < N_QUERIES; ++i) {
                const auto& w = words[i % words.size()];
                std::array<uint8_t, 16> h;
                unsigned int hlen = 16;
                EVP_Digest(w.data(), w.size(), h.data(), &hlen, EVP_md5(), nullptr);
                hit_queries.push_back(h);
            }
        }

        // Mixed queries: 95 miss followed by 5 hit, repeated.
        mixed_queries.reserve(N_QUERIES);
        size_t mi = 0, hi = 0;
        for (size_t i = 0; i < N_QUERIES; ++i) {
            if (i % 20 < 19)
                mixed_queries.push_back(miss_queries[mi++ % N_QUERIES]);
            else
                mixed_queries.push_back(hit_queries[hi++ % N_QUERIES]);
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

// ---------------------------------------------------------------------------
// Benchmark: probabilistic tiny pointer table miss lookups
// ---------------------------------------------------------------------------
static void BM_Prob_Miss(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->miss_queries[idx % N_QUERIES];
        auto result = g_bench->prob.lookup(q.data());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ---------------------------------------------------------------------------
// Hit benchmarks: query hash is guaranteed present in the table
// ---------------------------------------------------------------------------
static void BM_TinyPtr_Hit(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->hit_queries[idx % N_QUERIES];
        auto result = g_bench->tinyptr.lookup(q.data(), g_bench->pool.base());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

static void BM_Naive_Hit(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->hit_queries[idx % N_QUERIES];
        auto result = g_naive->table.lookup(q.data(), g_naive->pool.base());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

static void BM_StdMap_Hit(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->hit_queries[idx % N_QUERIES];
        auto result = g_bench->stdmap.lookup(q.data());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

static void BM_Prob_Hit(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->hit_queries[idx % N_QUERIES];
        auto result = g_bench->prob.lookup(q.data());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ---------------------------------------------------------------------------
// Mixed benchmarks: 95% miss + 5% hit (realistic cracker workload)
// ---------------------------------------------------------------------------
static void BM_TinyPtr_Mixed(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->mixed_queries[idx % N_QUERIES];
        auto result = g_bench->tinyptr.lookup(q.data(), g_bench->pool.base());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

static void BM_Naive_Mixed(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->mixed_queries[idx % N_QUERIES];
        auto result = g_naive->table.lookup(q.data(), g_naive->pool.base());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

static void BM_StdMap_Mixed(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->mixed_queries[idx % N_QUERIES];
        auto result = g_bench->stdmap.lookup(q.data());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

static void BM_Prob_Mixed(benchmark::State& state) {
    global_setup();
    size_t idx = 0;
    for (auto _ : state) {
        const auto& q = g_bench->mixed_queries[idx % N_QUERIES];
        auto result = g_bench->prob.lookup(q.data());
        benchmark::DoNotOptimize(result);
        ++idx;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(BM_TinyPtr_Miss)  ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Naive_Miss)    ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_StdMap_Miss)   ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Prob_Miss)     ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_TinyPtr_Hit)   ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Naive_Hit)     ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_StdMap_Hit)    ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Prob_Hit)      ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_TinyPtr_Mixed) ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Naive_Mixed)   ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_StdMap_Mixed)  ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Prob_Mixed)    ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
