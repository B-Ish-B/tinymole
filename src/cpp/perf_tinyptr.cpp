/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Standalone perf runner for the tiny pointer hash table. Loads
 * a wordlist, runs 2M miss queries, and prints hardware counter values scoped
 * to the lookup loop only (load and query-generation phases are excluded).
 * Counters are collected via perf_event_open(2) and emitted as parseable
 * `PERF: <name> = <value>` lines on stdout.
 */

#include <openssl/evp.h>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <cstdio>

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table.hpp"
#include "src/cpp/perf_counters.hpp"

static constexpr size_t N_QUERIES  = 2000000;
static const char*      WORDLIST   = "data/rockyou.txt";

int main() {
    PasswordPool pool;
    HashTable    table;

    {
        std::ifstream f(WORDLIST);
        table.load(f, pool);
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
        auto r = table.lookup(queries[i].data(), pool.base());
        sink += r.size();
    }
    perf.stop();

    perf.print();
    std::printf("tiny_ptr lookups done (sink=%zu)\n", (size_t)sink);
    return 0;
}
