/*
 * @author Ismail Alwahsh
 * @since May 15, 2026
 * Implementation of the probabilistic tiny pointer hash table.
 * See hash_table_prob.hpp for the design overview.
 */

#include "src/cpp/hash_table_prob.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>
#include "quill/Backend.h"
#include "quill/LogMacros.h"

// Null-safe log wrappers (same pattern as hash_table.cpp).
#define HLOG_INFO(logger, ...)  do { if (logger) { LOG_INFO(logger,  __VA_ARGS__); } } while(0)
#define HLOG_WARN(logger, ...)  do { if (logger) { LOG_WARNING(logger, __VA_ARGS__); } } while(0)
#define HLOG_DEBUG(logger, ...) do { if (logger) { LOG_DEBUG(logger, __VA_ARGS__); } } while(0)

// ---------------------------------------------------------------------------
// Hash functions
// ---------------------------------------------------------------------------

// ht_index: open-addressing probe start for the hash table itself.
// Uses the raw low bits of the first 8 bytes of the MD5 key.
size_t HashTableProb::ht_index(const uint8_t* key) const {
    uint64_t v;
    std::memcpy(&v, key, 8);
    return v & table_mask_;
}

// h1: maps the MD5 key to a primary pool bucket.
// Uses bytes 0-7 with a MurmurHash3-style mix to separate from ht_index.
size_t HashTableProb::h1(const uint8_t* key) const {
    uint64_t v;
    std::memcpy(&v, key, 8);
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return v % num_primary_buckets_;
}

// h2: maps the MD5 key to a secondary pool bucket (first two-choice candidate).
// Uses bytes 0-7 with a different mixing constant than h1.
size_t HashTableProb::h2(const uint8_t* key) const {
    uint64_t v;
    std::memcpy(&v, key, 8);
    v ^= v >> 31;
    v *= 0x9e3779b97f4a7c15ULL;  // Fibonacci hashing constant
    v ^= v >> 31;
    return v % num_secondary_buckets_;
}

// h3: maps the MD5 key to a secondary pool bucket (second two-choice candidate).
// Uses bytes 8-15 so it is independent of h2.
size_t HashTableProb::h3(const uint8_t* key) const {
    uint64_t v;
    std::memcpy(&v, key + 8, 8);
    v ^= v >> 33;
    v *= 0x6c62272e07bb0142ULL;
    v ^= v >> 33;
    return v % num_secondary_buckets_;
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

void HashTableProb::build_ht(size_t num_entries) {
    if (num_entries == 0)
        throw std::invalid_argument("num_entries must be > 0");

    // Same 70% load factor and power-of-two sizing as hash_table.cpp.
    size_t capacity = static_cast<size_t>(num_entries / 0.7) + 1;
    size_t pow2 = 1;
    while (pow2 < capacity)
        pow2 <<= 1;

    slots_.assign(pow2, ProbSlot{});
    table_mask_ = pow2 - 1;
    count_      = 0;
}

void HashTableProb::build_pool(size_t num_entries) {
    // Total pool slots at 70% load factor, rounded up to a bucket boundary.
    size_t total = static_cast<size_t>(num_entries / 0.7) + 1;
    total = ((total + PROB_BUCKET_SIZE - 1) / PROB_BUCKET_SIZE) * PROB_BUCKET_SIZE;

    // 85% primary, 15% secondary (matches Theorem 1 split in the paper).
    size_t primary_slots   = ((total * 85 / 100 + PROB_BUCKET_SIZE - 1)
                               / PROB_BUCKET_SIZE) * PROB_BUCKET_SIZE;
    size_t secondary_slots = total - primary_slots;

    // Ensure at least one bucket in each section.
    if (primary_slots   < PROB_BUCKET_SIZE) primary_slots   = PROB_BUCKET_SIZE;
    if (secondary_slots < PROB_BUCKET_SIZE) secondary_slots = PROB_BUCKET_SIZE;

    num_primary_buckets_   = primary_slots   / PROB_BUCKET_SIZE;
    num_secondary_buckets_ = secondary_slots / PROB_BUCKET_SIZE;

    size_t total_slots = primary_slots + secondary_slots;
    pool_.assign(static_cast<size_t>(total_slots) * PROB_POOL_SLOT_SIZE, '\0');
    pool_occupied_.assign(total_slots, 0);
}

// ---------------------------------------------------------------------------
// Pool operations
// ---------------------------------------------------------------------------

void HashTableProb::pool_write(size_t slot_idx, std::string_view pw) {
    char* s   = pool_.data() + slot_idx * PROB_POOL_SLOT_SIZE;
    s[0]      = static_cast<char>(static_cast<uint8_t>(pw.size()));
    std::memcpy(s + 1, pw.data(), pw.size());
}

int HashTableProb::free_in_primary_bucket(size_t bucket) const {
    size_t base = bucket * PROB_BUCKET_SIZE;
    int free = 0;
    for (uint8_t j = 0; j < PROB_BUCKET_SIZE; ++j)
        free += (pool_occupied_[base + j] == 0) ? 1 : 0;
    return free;
}

int HashTableProb::free_in_secondary_bucket(size_t bucket) const {
    size_t base = num_primary_buckets_ * PROB_BUCKET_SIZE + bucket * PROB_BUCKET_SIZE;
    int free = 0;
    for (uint8_t j = 0; j < PROB_BUCKET_SIZE; ++j)
        free += (pool_occupied_[base + j] == 0) ? 1 : 0;
    return free;
}

// ALLOCATE(key, pw): find the right bucket and store pw there.
// Returns the encoded tiny pointer, or PROB_TP_EMPTY if both sections are full.
uint8_t HashTableProb::pool_allocate(const uint8_t* key, std::string_view pw) {
    // Try primary bucket first.
    size_t pb   = h1(key);
    size_t base = pb * PROB_BUCKET_SIZE;
    for (uint8_t j = 0; j < PROB_BUCKET_SIZE; ++j) {
        if (!pool_occupied_[base + j]) {
            pool_write(base + j, pw);
            pool_occupied_[base + j] = 1;
            // Primary: bits 3-0 = slot index, bits 5-4 = 0.
            return static_cast<uint8_t>(j);
        }
    }

    // Primary bucket full: two-choice selection in secondary section.
    size_t sb2   = h2(key);
    size_t sb3   = h3(key);
    int    free2 = free_in_secondary_bucket(sb2);
    int    free3 = free_in_secondary_bucket(sb3);

    // Pick the secondary bucket with more free slots (power-of-two-choices).
    bool   use_h3 = (free3 > free2);
    size_t sb     = use_h3 ? sb3 : sb2;

    size_t sec_base = num_primary_buckets_ * PROB_BUCKET_SIZE + sb * PROB_BUCKET_SIZE;
    for (uint8_t j = 0; j < PROB_BUCKET_SIZE; ++j) {
        if (!pool_occupied_[sec_base + j]) {
            pool_write(sec_base + j, pw);
            pool_occupied_[sec_base + j] = 1;
            uint8_t tp = PROB_TP_SECONDARY
                       | (use_h3 ? PROB_TP_H3 : 0u)
                       | static_cast<uint8_t>(j);
            return tp;
        }
    }

    // Both primary and both secondary buckets are full. With correct sizing
    // this should not happen (failure probability is exponentially small per
    // the paper's Lemma 2 for two-choice hashing).
    return PROB_TP_EMPTY;
}

// DEREFERENCE(key, tiny_ptr): recover the password using both key and pointer.
std::string_view HashTableProb::pool_get(const uint8_t* key, uint8_t tp) const {
    size_t slot_idx;
    if (!(tp & PROB_TP_SECONDARY)) {
        // Primary: bucket is fully determined by h1(key).
        size_t bucket = h1(key);
        uint8_t j     = tp & 0x0Fu;
        slot_idx      = primary_abs(bucket, j);
    } else {
        // Secondary: bit 5 says which hash function was used at allocation time.
        size_t bucket = (tp & PROB_TP_H3) ? h3(key) : h2(key);
        uint8_t j     = tp & 0x0Fu;
        slot_idx      = secondary_abs(bucket, j);
    }

    const char* s = pool_.data() + slot_idx * PROB_POOL_SLOT_SIZE;
    uint8_t len   = static_cast<uint8_t>(s[0]);
    return std::string_view(s + 1, len);
}

// ---------------------------------------------------------------------------
// Hash table insert and lookup
// ---------------------------------------------------------------------------

bool HashTableProb::insert(const uint8_t* key16, std::string_view pw) {
    uint8_t tp = pool_allocate(key16, pw);
    if (tp == PROB_TP_EMPTY)
        return false;  // pool exhausted (should not happen with correct sizing)

    size_t idx = ht_index(key16);
    while (slots_[idx].occupied) {
        if (std::memcmp(slots_[idx].key, key16, 16) == 0) {
            // Duplicate: free the pool slot we just wrote.
            // Recompute the slot to undo the allocation.
            if (!(tp & PROB_TP_SECONDARY)) {
                size_t bucket = h1(key16);
                uint8_t j     = tp & 0x0Fu;
                pool_occupied_[primary_abs(bucket, j)] = 0;
            } else {
                size_t bucket = (tp & PROB_TP_H3) ? h3(key16) : h2(key16);
                uint8_t j     = tp & 0x0Fu;
                pool_occupied_[secondary_abs(bucket, j)] = 0;
            }
            return false;
        }
        idx = (idx + 1) & table_mask_;
    }

    std::memcpy(slots_[idx].key, key16, 16);
    slots_[idx].tiny_ptr = tp;
    slots_[idx].occupied = 1;
    ++count_;
    return true;
}

std::string_view HashTableProb::lookup(const uint8_t* query_hash) const {
    size_t idx = ht_index(query_hash);

    while (slots_[idx].occupied) {
        if (std::memcmp(slots_[idx].key, query_hash, 16) == 0) {
            // Found: dereference using both the key and the tiny pointer.
            return pool_get(slots_[idx].key, slots_[idx].tiny_ptr);
        }
        idx = (idx + 1) & table_mask_;
    }

    return {};
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

BuildStats HashTableProb::load(std::istream& src, quill::Logger* logger) {
    BuildStats stats;
    auto t_start = std::chrono::steady_clock::now();

    // Pass 1: validate lines, count insertable entries.
    size_t valid_count = 0;
    std::string line;

    while (std::getline(src, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) {
            stats.skip_empty++;
            continue;
        }

        if (line.find('\0') != std::string::npos) {
            stats.skip_null_byte++;
            if (stats.sample_null_byte.size() < 5) {
                std::string display;
                display.reserve(line.size() + 16);
                for (unsigned char c : line)
                    display += (c == '\0') ? std::string("[NUL]") : std::string(1, c);
                stats.sample_null_byte.push_back(display);
            }
            continue;
        }

        if (line.size() > PROB_MAX_LEN) {
            stats.skip_too_long++;
            if (line.size() > stats.max_rejected_len)
                stats.max_rejected_len = line.size();
            HLOG_DEBUG(logger, "skipping password over max length ({} chars)", line.size());
            continue;
        }

        valid_count++;
    }

    if (valid_count == 0) {
        HLOG_WARN(logger, "no valid passwords found in wordlist");
        return stats;
    }

    build_ht(valid_count);
    build_pool(valid_count);

    HLOG_INFO(logger, "prob pool: {} primary buckets, {} secondary buckets ({} total slots)",
        num_primary_buckets_, num_secondary_buckets_,
        (num_primary_buckets_ + num_secondary_buckets_) * PROB_BUCKET_SIZE);

    // Pass 2: insert all valid entries.
    src.clear();
    src.seekg(0);

    size_t pool_full_count = 0;
    size_t lines_processed = 0;

    while (std::getline(src, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty() ||
            line.find('\0') != std::string::npos ||
            line.size() > PROB_MAX_LEN)
            continue;

        uint8_t hash[16];
        unsigned int hlen = 16;
        EVP_Digest(line.data(), line.size(), hash, &hlen, EVP_md5(), nullptr);

        if (!insert(hash, line)) {
            // insert returns false for duplicates and for pool-full failures.
            // Distinguish by checking if the key is already in the HT.
            size_t idx = ht_index(hash);
            bool is_dup = false;
            while (slots_[idx].occupied) {
                if (std::memcmp(slots_[idx].key, hash, 16) == 0) {
                    is_dup = true;
                    break;
                }
                idx = (idx + 1) & table_mask_;
            }
            if (is_dup)
                stats.skip_duplicate++;
            else
                pool_full_count++;
        } else {
            stats.loaded++;
        }

        lines_processed++;
        if (lines_processed % 1000000 == 0)
            HLOG_DEBUG(logger, "load progress: {} entries processed", lines_processed);
    }

    auto t_end = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    size_t total_skipped = stats.total_skipped();
    double skip_pct = (stats.loaded + total_skipped) > 0
        ? 100.0 * total_skipped / (stats.loaded + total_skipped)
        : 0.0;

    HLOG_INFO(logger, "prob hash table build complete in {:.1f} ms", elapsed_ms);
    HLOG_INFO(logger, "  entries loaded:          {}", stats.loaded);
    HLOG_INFO(logger, "  total skipped:           {} ({:.1f}%)", total_skipped, skip_pct);
    HLOG_INFO(logger, "    duplicates:            {}", stats.skip_duplicate);
    HLOG_INFO(logger, "    too long (>{} chars):  {}", PROB_MAX_LEN, stats.skip_too_long);
    HLOG_INFO(logger, "    empty lines:           {}", stats.skip_empty);
    HLOG_INFO(logger, "    embedded null bytes:   {}", stats.skip_null_byte);

    if (pool_full_count > 0)
        HLOG_WARN(logger, "  pool allocation failures (both buckets full): {}", pool_full_count);

    HLOG_INFO(logger, "  HT slot size:            {} bytes", sizeof(ProbSlot));
    HLOG_INFO(logger, "  pool slot size:          {} bytes (fixed)", PROB_POOL_SLOT_SIZE);
    HLOG_INFO(logger, "  HT capacity:             {} slots ({} MB)",
        slots_.size(),
        (slots_.size() * sizeof(ProbSlot)) / (1024 * 1024));
    HLOG_INFO(logger, "  pool capacity:           {} slots ({} MB)",
        pool_occupied_.size(),
        (pool_occupied_.size() * PROB_POOL_SLOT_SIZE) / (1024 * 1024));

    return stats;
}
