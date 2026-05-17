/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Naive open-addressed hash table. Uses a 4-byte pool offset
 * instead of a tiny pointer, so each slot is 16 bytes: 12-byte truncated key
 * + 4-byte offset. Same linear probing, load factor, SIMD key compare, and
 * prefetch as HashTable. Baseline to isolate the pointer-encoding difference.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <istream>
#include <string>
#include <string_view>
#include <vector>
#include "quill/Logger.h"
#include "src/cpp/hashtable/build_stats.hpp"
#include "src/cpp/hashtable/tiny_ptr.hpp"

// Open-addressed hash table using full pool offsets.
// Slot layout: 12-byte truncated key + 4-byte offset = 16 bytes.
// uint32_t offset is sufficient since pool is bounded at 128 MB.
// 0xFFFFFFFF is the empty sentinel (offset can never be 4 GB).
// Length is derived at lookup from the null terminator in the pool.

static constexpr uint32_t NAIVE_SLOT_EMPTY = 0xFFFFFFFFu;

struct SlotNaive {
    uint8_t  key[12];   // 96-bit truncated key; same negligible collision rate as HashTable
    uint32_t offset;    // byte offset into pool; NAIVE_SLOT_EMPTY when unoccupied
};

static_assert(sizeof(SlotNaive) == 16, "SlotNaive must be 16 bytes");

struct HashTableNaive {
    HashTableNaive() = default;

    void build(size_t num_entries);
    bool insert(const uint8_t key[16], uint32_t offset);
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
