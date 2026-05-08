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
    const EVP_MD*                   md,
    std::atomic<bool>&              found,
    std::string&                    result
) {
    const int hash_len = EVP_MD_get_size(md);
    std::vector<uint8_t> hash(hash_len);
    unsigned int hlen = static_cast<unsigned int>(hash_len);

    // Allocate once; reuse across every candidate to avoid per-iteration
    // heap traffic from EVP_Digest's internal alloc/free cycle.
    auto ctx_del = [](EVP_MD_CTX* c) { EVP_MD_CTX_free(c); };
    std::unique_ptr<EVP_MD_CTX, decltype(ctx_del)> ctx(EVP_MD_CTX_new(), ctx_del);

    for (const auto& candidate : partition) {
        if (found.load(std::memory_order_relaxed)) return;

        EVP_DigestInit_ex(ctx.get(), md, nullptr);
        EVP_DigestUpdate(ctx.get(), candidate.data(), candidate.size());
        EVP_DigestFinal_ex(ctx.get(), hash.data(), &hlen);

        if (std::memcmp(hash.data(), target_hash, hash_len) == 0) {
            auto match = table.lookup(hash.data(), pool.base());
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
    const EVP_MD*                   md,
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
            md,
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
