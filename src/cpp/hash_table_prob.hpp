/*
 * @author Ismail Alwahsh
 * @since May 15, 2026
 * Probabilistic tiny pointer hash table following the construction in
 * Bender et al. (ACM Transactions on Algorithms, 2024).
 *
 * Core idea from the paper: DEREFERENCE(key, tiny_ptr) takes both the key
 * and the pointer. The key determines which bucket in the pool to look in
 * via h(key). The tiny pointer only encodes the slot index within that bucket
 * (4 bits for a bucket size of 16). This shrinks the stored pointer from the
 * 4-byte packed offset used in hash_table.hpp down to 1 byte, reducing each
 * hash table slot from 24 bytes to 20 bytes.
 *
 * Pool layout (fixed-size slots):
 *   [primary section:   num_primary_buckets   * BUCKET_SIZE slots]
 *   [secondary section: num_secondary_buckets * BUCKET_SIZE slots]
 *   Each slot is POOL_SLOT_SIZE bytes: 1 length byte + 31 data bytes.
 *
 * ALLOCATE(key, value):
 *   1. Compute primary bucket B = h1(key). If B has a free slot j, store
 *      value there and return tiny_ptr = j (bits 3-0 only, bit 4 = 0).
 *   2. If B is full (primary overflow), apply two-choice hashing: compute
 *      h2(key) and h3(key) into the secondary section, pick whichever
 *      secondary bucket has more free slots, store there.
 *   3. Tiny pointer for secondary: bit 4 = 1 (secondary flag), bit 5 = which
 *      hash function was used (0 = h2, 1 = h3), bits 3-0 = slot in bucket.
 *
 * DEREFERENCE(key, tiny_ptr):
 *   bit 4 = 0: bucket = h1(key) in primary section.
 *   bit 4 = 1: bucket = h2(key) or h3(key) (bit 5) in secondary section.
 *   Password = pool[ slot_absolute_index * POOL_SLOT_SIZE ].
 *
 * Hash table slot: 16 bytes (same as bit-packing after key truncation and sentinel removal).
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <istream>
#include <string_view>
#include <vector>
#include "quill/Logger.h"
#include "src/cpp/build_stats.hpp"

static constexpr uint8_t  PROB_BUCKET_SIZE    = 16;  // slots per bucket (b from paper)
static constexpr uint32_t PROB_POOL_SLOT_SIZE = 32;  // bytes per pool slot: 1 len + 31 data
static constexpr uint8_t  PROB_MAX_LEN        = 31;  // max password length (fits in 5 bits)

// Tiny pointer bit layout (stored in one uint8_t):
//   bits 3-0: slot index within bucket (0..15)
//   bit    4: 0 = primary section, 1 = secondary section
//   bit    5: which secondary hash (0 = h2, 1 = h3); only used when bit 4 = 1
static constexpr uint8_t PROB_TP_SECONDARY = (1u << 4);
static constexpr uint8_t PROB_TP_H3        = (1u << 5);
static constexpr uint8_t PROB_TP_EMPTY     = 0xFF;  // sentinel for unoccupied HT slot

struct ProbSlot {
    uint8_t key[12];    // 96-bit truncated key; same negligible collision rate as HashTable
    uint8_t tiny_ptr;   // 6-bit probabilistic pointer; PROB_TP_EMPTY (0xFF) when unoccupied
    uint8_t padding[3]; // pad to 16 bytes for 4 slots per 64B cache line
};

static_assert(sizeof(ProbSlot) == 16, "ProbSlot must be 16 bytes");

struct HashTableProb {
    HashTableProb() = default;

    // Load a wordlist from src, build pool and hash table. logger may be nullptr.
    BuildStats load(std::istream& src, quill::Logger* logger = nullptr);

    // Look up a 16-byte MD5 digest. Returns the plaintext, or empty on miss.
    // Key-dependent dereference: the query hash is used to locate the pool bucket.
    std::string_view lookup(const uint8_t* query_hash) const;

    size_t capacity()    const { return slots_.size(); }
    size_t entry_count() const { return count_; }

private:
    // Open-addressed hash table (linear probing, 70% load factor).
    std::vector<ProbSlot> slots_;
    size_t table_mask_ = 0;
    size_t count_      = 0;

    // Password pool: flat fixed-size slot storage.
    // Organized as primary_buckets followed by secondary_buckets.
    std::vector<char>    pool_;
    std::vector<uint8_t> pool_occupied_;   // one byte per pool slot (faster than bit vector)
    size_t num_primary_buckets_   = 0;
    size_t num_secondary_buckets_ = 0;

    // Resize and zero-initialize the hash table.
    void build_ht(size_t num_entries);

    // Allocate pool storage for up to num_entries passwords.
    void build_pool(size_t num_entries);

    // Hash password, allocate pool slot, insert into HT. Returns false on duplicate.
    bool insert(const uint8_t* key16, std::string_view pw);

    // ALLOCATE: find a free pool slot for this key. Returns PROB_TP_EMPTY on failure.
    uint8_t pool_allocate(const uint8_t* key16, std::string_view pw);

    // Write password data into an absolute pool slot index.
    void pool_write(size_t slot_idx, std::string_view pw);

    // DEREFERENCE: recover the password from the pool using key and tiny pointer.
    std::string_view pool_get(const uint8_t* key16, uint8_t tp) const;

    // Hash table probe index (open addressing).
    size_t ht_index(const uint8_t* key16) const;

    // Three independent hash functions mapping the MD5 key to bucket indices.
    // h1 targets the primary section; h2 and h3 target the secondary section.
    size_t h1(const uint8_t* key16) const;
    size_t h2(const uint8_t* key16) const;
    size_t h3(const uint8_t* key16) const;

    // Count free slots in a bucket. Used by two-choice secondary allocation.
    int free_in_primary_bucket(size_t bucket) const;
    int free_in_secondary_bucket(size_t bucket) const;

    // Convert (section, bucket, slot_in_bucket) to an absolute pool slot index.
    size_t primary_abs(size_t bucket, uint8_t j) const {
        return bucket * PROB_BUCKET_SIZE + j;
    }
    size_t secondary_abs(size_t bucket, uint8_t j) const {
        return num_primary_buckets_ * PROB_BUCKET_SIZE + bucket * PROB_BUCKET_SIZE + j;
    }
};
