/*
 * @author Ismail Alwahsh
 * @since May 15, 2026
 * Per-lookup latency distribution benchmark using RDTSC cycle counting.
 * Reports p50, p95, p99, p99.9, mean, stddev, and max for all four
 * implementations. RDTSC overhead is measured at startup and subtracted
 * from each sample so reported values reflect actual lookup cost.
 *
 * Only valid on x86/x86-64. Uses lfence+rdtsc/rdtscp+lfence serialization
 * as recommended in Intel's white paper on RDTSC-based measurement.
 */

#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "src/cpp/hashtable/tiny_ptr.hpp"
#include "src/cpp/hashtable/hash_table.hpp"
#include "src/cpp/hashtable/hash_table_naive.hpp"
#include "src/cpp/hashtable/hash_table_stdmap.hpp"
#include "src/cpp/hashtable/hash_table_prob.hpp"

static constexpr size_t N_WARMUP  =  500'000;
static constexpr size_t N_SAMPLES = 2'000'000;
static const char*      WORDLIST  = "data/rockyou.txt";

// ---------------------------------------------------------------------------
// RDTSC serialized measurement
// lfence before rdtsc ensures all prior loads complete before the counter read.
// rdtscp+lfence at the end serializes the counter read before subsequent loads.
// ---------------------------------------------------------------------------

static inline uint64_t rdtsc_start() {
    uint32_t lo, hi;
    __asm__ volatile("lfence\nrdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static inline uint64_t rdtsc_end() {
    uint32_t lo, hi, aux;
    __asm__ volatile("rdtscp\nlfence" : "=a"(lo), "=d"(hi), "=c"(aux) :: "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// Measure RDTSC call overhead by timing an empty measurement window 1000 times
// and taking the minimum (closest to true overhead with no intervening work).
static uint64_t measure_rdtsc_overhead() {
    uint64_t min_cycles = UINT64_MAX;
    for (int i = 0; i < 1000; ++i) {
        uint64_t t0 = rdtsc_start();
        uint64_t t1 = rdtsc_end();
        uint64_t d  = t1 - t0;
        if (d < min_cycles) min_cycles = d;
    }
    return min_cycles;
}

// Read CPU frequency in GHz from sysfs (set by performance governor).
static double cpu_ghz() {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    uint64_t khz = 0;
    if (f >> khz && khz > 0)
        return static_cast<double>(khz) / 1e6;
    return 4.1;
}

// ---------------------------------------------------------------------------
// Query generation: MD5 hashes of "MISS_QUERY_<i>" strings, guaranteed absent
// ---------------------------------------------------------------------------

static std::vector<std::array<uint8_t, 16>> make_queries(size_t n) {
    std::vector<std::array<uint8_t, 16>> qs;
    qs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        std::string s = "MISS_QUERY_" + std::to_string(i);
        std::array<uint8_t, 16> h;
        unsigned int hlen = 16;
        EVP_Digest(s.data(), s.size(), h.data(), &hlen, EVP_md5(), nullptr);
        qs.push_back(h);
    }
    return qs;
}

// ---------------------------------------------------------------------------
// Percentile and stats helpers
// ---------------------------------------------------------------------------

static double cycles_to_ns(uint64_t cycles, double ghz) {
    return static_cast<double>(cycles) / ghz;
}

struct Stats {
    double mean, stddev, p50, p95, p99, p999, max;
};

// samples must be sorted ascending before calling.
static Stats compute_stats(std::vector<uint32_t>& s, double ghz) {
    size_t n = s.size();
    double sum = 0.0;
    for (uint32_t v : s) sum += v;
    double mean_cyc = sum / n;

    double var = 0.0;
    for (uint32_t v : s) {
        double d = v - mean_cyc;
        var += d * d;
    }
    double stddev_cyc = std::sqrt(var / n);

    auto pct = [&](double p) -> double {
        size_t idx = static_cast<size_t>(p / 100.0 * (n - 1));
        return cycles_to_ns(s[idx], ghz);
    };

    return {
        cycles_to_ns(static_cast<uint64_t>(mean_cyc), ghz),
        cycles_to_ns(static_cast<uint64_t>(stddev_cyc), ghz),
        pct(50.0),
        pct(95.0),
        pct(99.0),
        pct(99.9),
        cycles_to_ns(s.back(), ghz),
    };
}

// ---------------------------------------------------------------------------
// Generic measurement loop templated on lookup call
// ---------------------------------------------------------------------------

template<typename LookupFn>
Stats measure(LookupFn lookup_fn,
              const std::vector<std::array<uint8_t, 16>>& queries,
              uint64_t rdtsc_overhead,
              double ghz) {
    const size_t total = N_WARMUP + N_SAMPLES;
    volatile size_t sink = 0;

    // Warmup: runs are not timed; purpose is to bring table into a realistic
    // cache state (neither fully cold nor unrealistically hot).
    for (size_t i = 0; i < N_WARMUP; ++i) {
        auto r = lookup_fn(queries[i % queries.size()].data());
        sink += r.size();
    }

    std::vector<uint32_t> samples;
    samples.reserve(N_SAMPLES);

    for (size_t i = 0; i < N_SAMPLES; ++i) {
        const uint8_t* q = queries[i % queries.size()].data();

        uint64_t t0 = rdtsc_start();
        auto r = lookup_fn(q);
        uint64_t t1 = rdtsc_end();

        sink += r.size();

        uint64_t raw = t1 - t0;
        // Clamp to zero if measurement noise makes raw < overhead.
        uint32_t net = static_cast<uint32_t>(raw > rdtsc_overhead ? raw - rdtsc_overhead : 0);
        samples.push_back(net);
    }

    (void)sink;
    std::sort(samples.begin(), samples.end());
    return compute_stats(samples, ghz);
}

// ---------------------------------------------------------------------------
// Print helpers
// ---------------------------------------------------------------------------

static void print_header() {
    std::printf("\n%-10s %9s %9s %9s %9s %9s %9s %9s\n",
        "impl", "mean", "stddev", "p50", "p95", "p99", "p99.9", "max");
    std::printf("%-10s %9s %9s %9s %9s %9s %9s %9s\n",
        "", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)");
    std::printf("%s\n", std::string(80, '-').c_str());
}

static void print_row(const char* name, const Stats& s) {
    std::printf("%-10s %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f\n",
        name, s.mean, s.stddev, s.p50, s.p95, s.p99, s.p999, s.max);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    double ghz             = cpu_ghz();
    uint64_t rdtsc_ovhd    = measure_rdtsc_overhead();
    double   ovhd_ns       = cycles_to_ns(rdtsc_ovhd, ghz);

    std::printf("CPU: %.3f GHz\n", ghz);
    std::printf("RDTSC overhead: %lu cycles (%.1f ns) -- subtracted from each sample\n",
                (unsigned long)rdtsc_ovhd, ovhd_ns);
    std::printf("Samples: %zu warmup + %zu measured\n", N_WARMUP, N_SAMPLES);

    // Generate queries once and share across all implementations.
    std::printf("Generating %zu miss queries...\n", N_WARMUP + N_SAMPLES);
    auto queries = make_queries(N_WARMUP + N_SAMPLES);

    // Build tables.
    std::printf("Loading wordlist: %s\n", WORDLIST);

    PasswordPool pool;
    HashTable    tinyptr;
    {
        std::ifstream f(WORDLIST);
        tinyptr.load(f, pool);
    }

    PasswordPool   pool_naive;
    HashTableNaive naive;
    {
        std::ifstream f(WORDLIST);
        naive.load(f, pool_naive);
    }

    HashTableStdMap stdmap;
    {
        std::ifstream f(WORDLIST);
        stdmap.load(f);
    }

    HashTableProb prob;
    {
        std::ifstream f(WORDLIST);
        prob.load(f);
    }

    std::printf("All tables loaded. Running latency measurements...\n\n");

    print_header();

    auto s_tp = measure(
        [&](const uint8_t* q) { return tinyptr.lookup(q, pool.base()); },
        queries, rdtsc_ovhd, ghz);
    print_row("tinyptr", s_tp);

    auto s_na = measure(
        [&](const uint8_t* q) { return naive.lookup(q, pool_naive.base()); },
        queries, rdtsc_ovhd, ghz);
    print_row("naive", s_na);

    auto s_pr = measure(
        [&](const uint8_t* q) { return prob.lookup(q); },
        queries, rdtsc_ovhd, ghz);
    print_row("prob", s_pr);

    auto s_sm = measure(
        [&](const uint8_t* q) { return stdmap.lookup(q); },
        queries, rdtsc_ovhd, ghz);
    print_row("stdmap", s_sm);

    std::printf("\nNote: all values are miss-query latency (key not in table).\n");
    std::printf("RDTSC overhead subtracted per sample. Warmup: %zu lookups.\n", N_WARMUP);

    return 0;
}
