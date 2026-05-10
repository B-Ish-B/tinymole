/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Naive open-addressed hash table. Uses full 8-byte pool offsets
 * instead of tiny pointers, so each slot is 32 bytes (vs 24 for HashTable).
 * Same linear probing logic and load factor. Exists only as a benchmark
 * baseline to isolate the cache impact of the larger slot size.
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

// Open-addressed hash table using full 8-byte pool offsets.
// Slot layout: 16-byte MD5 key + 8-byte offset + 1-byte occupied + 7-byte padding = 32 bytes.
// Length is derived at lookup from the null terminator in the pool.
// Used as a baseline to isolate the contribution of pointer compression.

struct SlotNaive {
    uint8_t  key[16];
    uint64_t offset;
    bool     occupied;
    uint8_t  padding[7];
};

static_assert(sizeof(SlotNaive) == 32, "SlotNaive must be 32 bytes");

struct HashTableNaive {
    HashTableNaive() = default;

    void build(size_t num_entries);
    bool insert(const uint8_t key[16], uint64_t offset);
    std::string_view lookup(const uint8_t* query_hash, const char* pool) const;
    BuildStats load(std::istream& src, PasswordPool& pool, quill::Logger* logger = nullptr);

    size_t capacity()    const { return slots_.size(); }
    size_t entry_count() const { return count_; }

private:
    std::vector<SlotNaive> slots_;
    size_t                 table_mask_ = 0;
    size_t                 count_      = 0;

    size_t index_of(const uint8_t key[16]) const;
};
