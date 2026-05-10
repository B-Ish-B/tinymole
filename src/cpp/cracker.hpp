/*
 * @author Ismail Alwahsh
 * @since May 10, 2026
 * @description: Multithreaded cracker. Splits the candidate list round-robin
 * across threads so each thread gets an equal share of high-probability
 * candidates. Each worker hashes its partition with OpenSSL EVP and checks
 * results against the hash table. A shared atomic<bool> stops all threads as
 * soon as one finds a match. Templated on table type so the same worker runs
 * against all three hash table implementations.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <openssl/evp.h>
#include "quill/Backend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "src/cpp/tiny_ptr.hpp"

#define CLOG_INFO(logger, ...)  do { if (logger) { LOG_INFO(logger,  __VA_ARGS__); } } while(0)
#define CLOG_DEBUG(logger, ...) do { if (logger) { LOG_DEBUG(logger, __VA_ARGS__); } } while(0)

// Split candidates round-robin across num_threads partitions so each
// thread gets an equivalent statistical distribution of likely passwords.
std::vector<std::vector<std::string>> partition_candidates(
    const std::vector<std::string>& candidates,
    int num_threads
);

// Thread worker templated on hash table type. All three table types expose
// the same lookup(hash, pool_base) signature so the same worker body runs
// against HashTable, HashTableNaive, and HashTableStdMap.
// The EVP_MD_CTX is allocated once per call and reused across all candidates
// to avoid per-iteration heap traffic.
// Note: bcrypt requires a separate verification path and cannot use this
// EVP_MD interface.
template<typename Table>
void worker(
    const std::vector<std::string>& partition,
    const Table&                    table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    const EVP_MD*                   md,
    std::atomic<bool>&              found,
    std::string&                    result
) {
    const int hash_len = EVP_MD_get_size(md);
    std::vector<uint8_t> hash(hash_len);
    unsigned int hlen = static_cast<unsigned int>(hash_len);

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

// Launch num_threads workers against candidates. Joins all threads and
// returns the cracked plaintext, or an empty string if not found.
// md selects the hash algorithm; defaults to MD5. target_hash must be
// exactly EVP_MD_get_size(md) bytes.
template<typename Table>
std::string crack(
    const std::vector<std::string>& candidates,
    const Table&                    table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    int                             num_threads,
    const EVP_MD*                   md     = EVP_md5(),
    quill::Logger*                  logger = nullptr
) {
    auto t_start = std::chrono::steady_clock::now();

    CLOG_INFO(logger, "starting crack: {} candidates, {} threads",
        candidates.size(), num_threads);

    auto partitions = partition_candidates(candidates, num_threads);

    std::atomic<bool> found{false};
    std::vector<std::string> results(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        CLOG_DEBUG(logger, "spawning thread {} with {} candidates", i, partitions[i].size());
        threads.emplace_back(worker<Table>,
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
