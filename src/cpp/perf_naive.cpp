/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Standalone perf runner for the naive open-addressed hash table.
 * Same workload as perf_tinyptr for direct comparison. Use with perf stat to
 * collect cache miss counts and compare against the tiny pointer variant.
 */

#include <openssl/evp.h>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <cstdio>

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table_naive.hpp"

static constexpr size_t N_QUERIES  = 2000000;
static const char*      WORDLIST   = "data/rockyou_1m.txt";

int main() {
    PasswordPool   pool;
    HashTableNaive table;

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

    volatile size_t sink = 0;
    for (size_t i = 0; i < N_QUERIES; ++i) {
        auto r = table.lookup(queries[i].data(), pool.base());
        sink += r.size();
    }

    std::printf("naive lookups done (sink=%zu)\n", (size_t)sink);
    return 0;
}
