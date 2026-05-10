/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Tiny pointer hash table. Each slot is 24 bytes: 16-byte MD5
 * key, 4-byte tiny_ptr (27-bit pool offset + 5-bit length), 1-byte occupied
 * flag, 3-byte padding. Open addressing with linear probing. Table capacity is
 * sized to 70% load factor. This is the primary implementation benchmarked
 * against the naive and stdmap baselines.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <istream>
#include <string>
#include <string_view>
#include <vector>
#include "quill/Logger.h"
#include "src/cpp/build_stats.hpp"
#include "src/cpp/tiny_ptr.hpp"

struct Slot {
    uint8_t  key[16];
    uint32_t tiny_ptr;
    bool     occupied;
    uint8_t  padding[3];
};

static_assert(sizeof(Slot) == 24, "Slot must be 24 bytes");

struct HashTable {
    HashTable() = default;

    // Size the table at num_entries / 0.7, rounded up to next power of two.
    void build(size_t num_entries);

    // Insert a precomputed MD5 hash and its tiny_ptr. Returns false on duplicate.
    bool insert(const uint8_t key[16], uint32_t tiny_ptr);

    // Look up a 16-byte MD5 hash. Returns string_view into pool, or empty if not found.
    std::string_view lookup(const uint8_t* query_hash, const char* pool) const;

    // Read a wordlist from src (one password per line), populate pool, build and
    // insert all entries. Returns rejection and load stats. logger may be nullptr.
    BuildStats load(std::istream& src, PasswordPool& pool, quill::Logger* logger = nullptr);

    size_t capacity()    const { return slots_.size(); }
    size_t entry_count() const { return count_; }

private:
    std::vector<Slot> slots_;
    size_t            table_mask_ = 0;
    size_t            count_      = 0;

    size_t index_of(const uint8_t key[16]) const;
};
