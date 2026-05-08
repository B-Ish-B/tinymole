#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "quill/Logger.h"
#include "src/cpp/hash_table.hpp"

// Split candidates round-robin across num_threads partitions so each
// thread gets an equivalent statistical distribution of likely passwords.
std::vector<std::vector<std::string>> partition_candidates(
    const std::vector<std::string>& candidates,
    int num_threads
);

// Thread worker. Iterates its partition, computes MD5 per candidate,
// and checks against target_hash. Writes plaintext to result and sets
// found on match. Exits early if found is already set by another thread.
void worker(
    const std::vector<std::string>& partition,
    const HashTable&                table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    std::atomic<bool>&              found,
    std::string&                    result
);

// Launch num_threads workers against candidates. Joins all threads and
// returns the cracked plaintext, or an empty string if not found.
std::string crack(
    const std::vector<std::string>& candidates,
    const HashTable&                table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    int                             num_threads,
    quill::Logger*                  logger = nullptr
);
