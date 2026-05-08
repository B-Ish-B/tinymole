#include "src/cpp/cracker.hpp"

#include <chrono>
#include <cstring>
#include <thread>

#include <openssl/evp.h>
#include "quill/Backend.h"
#include "quill/LogMacros.h"

#define CLOG_INFO(logger, ...)  do { if (logger) { LOG_INFO(logger,  __VA_ARGS__); } } while(0)
#define CLOG_DEBUG(logger, ...) do { if (logger) { LOG_DEBUG(logger, __VA_ARGS__); } } while(0)

std::vector<std::vector<std::string>> partition_candidates(
    const std::vector<std::string>& candidates,
    int num_threads
) {
    std::vector<std::vector<std::string>> partitions(num_threads);
    for (size_t i = 0; i < candidates.size(); ++i)
        partitions[i % num_threads].push_back(candidates[i]);
    return partitions;
}

void worker(
    const std::vector<std::string>& partition,
    const HashTable&                table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    std::atomic<bool>&              found,
    std::string&                    result
) {
    for (const auto& candidate : partition) {
        if (found.load(std::memory_order_relaxed)) return;

        uint8_t hash[16];
        unsigned int hlen = 16;
        EVP_Digest(candidate.data(), candidate.size(), hash, &hlen, EVP_md5(), nullptr);

        if (std::memcmp(hash, target_hash, 16) == 0) {
            // Retrieve plaintext from the table. If the candidate is a generated
            // variant not in the table, fall back to the candidate string itself.
            auto match = table.lookup(hash, pool.base());
            result = match.empty() ? candidate : std::string(match);
            found.store(true, std::memory_order_relaxed);
            return;
        }
    }
}

std::string crack(
    const std::vector<std::string>& candidates,
    const HashTable&                table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    int                             num_threads,
    quill::Logger*                  logger
) {
    auto t_start = std::chrono::steady_clock::now();

    CLOG_INFO(logger, "starting crack: {} candidates, {} threads",
        candidates.size(), num_threads);

    auto partitions = partition_candidates(candidates, num_threads);

    std::atomic<bool> found{false};
    std::vector<std::string>  results(num_threads);
    std::vector<std::thread>  threads;
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        CLOG_DEBUG(logger, "spawning thread {} with {} candidates", i, partitions[i].size());
        threads.emplace_back(worker,
            std::cref(partitions[i]),
            std::cref(table),
            std::cref(pool),
            target_hash,
            std::ref(found),
            std::ref(results[i]));
    }

    for (auto& t : threads) t.join();

    auto t_end = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    for (int i = 0; i < num_threads; ++i) {
        if (!results[i].empty()) {
            CLOG_INFO(logger, "crack found in {:.1f} ms: {}", elapsed_ms, results[i]);
            return results[i];
        }
    }

    CLOG_INFO(logger, "crack failed after {:.1f} ms, password not in candidate list", elapsed_ms);
    return {};
}
