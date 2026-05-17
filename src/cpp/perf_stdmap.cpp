/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Standalone perf runner for the std::unordered_map hash table.
 * Same workload as perf_tinyptr and perf_naive. Provides the third data point
 * for the cache miss comparison across all three implementations.
 */

#include <openssl/evp.h>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <cstdio>

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table_stdmap.hpp"
#include "src/cpp/perf_counters.hpp"

static constexpr size_t N_QUERIES  = 2000000;
static const char*      WORDLIST   = "data/rockyou.txt";

int main() {
    HashTableStdMap table;

    {
        std::ifstream f(WORDLIST);
        table.load(f);
    }

    std::vector<std::array<uint8_t, 16>> queries;
    queries.reserve(N_QUERIES);
    for (size_t i = 0; i < N_QUERIES; ++i) {
        std::string s = "MISS_QUERY_" + std::to_string(i);
        std::array<uint8_t, 16> h;
        unsigned int hlen = 16;
        EVP_Digest(s.data(), s.size(), h.data(), &hlen, EVP_md5(), nullptr);
        queries.push_back(h);
    }

    PerfCounters perf(standard_events());

    volatile size_t sink = 0;
    perf.reset_and_start();
    for (size_t i = 0; i < N_QUERIES; ++i) {
        auto r = table.lookup(queries[i].data());
        sink += r.size();
    }
    perf.stop();

    perf.print();
    std::printf("stdmap lookups done (sink=%zu)\n", (size_t)sink);
    return 0;
}
