#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <openssl/evp.h>
#include "quill/Logger.h"
#include "src/cpp/hash_table.hpp"

// Split candidates round-robin across num_threads partitions so each
// thread gets an equivalent statistical distribution of likely passwords.
std::vector<std::vector<std::string>> partition_candidates(
    const std::vector<std::string>& candidates,
    int num_threads
);

// Thread worker. Iterates its partition, hashes each candidate with md,
// and checks against target_hash. Writes plaintext to result and sets
// found on match. Exits early if found is already set by another thread.
// The EVP_MD_CTX is allocated once per worker call and reused across all
// candidates to avoid per-iteration heap traffic.
// Note: bcrypt requires a separate verification path (bcrypt_checkpw or
// similar) and does not use this EVP_MD interface.
void worker(
    const std::vector<std::string>& partition,
    const HashTable&                table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    const EVP_MD*                   md,
    std::atomic<bool>&              found,
    std::string&                    result
);

// Launch num_threads workers against candidates. Joins all threads and
// returns the cracked plaintext, or an empty string if not found.
// md selects the hash algorithm; defaults to MD5. target_hash must be
// exactly EVP_MD_get_size(md) bytes.
std::string crack(
    const std::vector<std::string>& candidates,
    const HashTable&                table,
    const PasswordPool&             pool,
    const uint8_t*                  target_hash,
    int                             num_threads,
    const EVP_MD*                   md     = EVP_md5(),
    quill::Logger*                  logger = nullptr
);
