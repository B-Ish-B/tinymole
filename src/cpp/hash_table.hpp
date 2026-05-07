#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>
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

    // Build the table from a pool. Each call to insert() must have already
    // added the password to pool and received the tiny_ptr back.
    // Size the table at num_entries / 0.7, rounded up to next power of two.
    void build(size_t num_entries);

    // Insert a precomputed MD5 hash and its tiny_ptr. Returns false if the
    // entry is a duplicate (same key already present).
    bool insert(const uint8_t key[16], uint32_t tiny_ptr);

    // Look up a 16-byte MD5 hash. Returns the plaintext via string_view into
    // pool, or an empty string_view if not found.
    std::string_view lookup(const uint8_t* query_hash, const char* pool) const;

    size_t capacity()    const { return slots_.size(); }
    size_t entry_count() const { return count_; }

private:
    std::vector<Slot> slots_;
    size_t            table_mask_ = 0;
    size_t            count_      = 0;

    size_t index_of(const uint8_t key[16]) const;
};
