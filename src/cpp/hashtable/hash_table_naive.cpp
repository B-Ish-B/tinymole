/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Naive open-addressed hash table implementation. Same linear
 * probe structure as HashTable but stores full 8-byte offsets instead of
 * tiny pointers, making each slot 32 bytes. Used as the baseline to measure
 * how much the pointer compression saves in cache pressure.
 */

#include "src/cpp/hashtable/hash_table_naive.hpp"
#include "src/cpp/hashtable/ht_ops.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>
#include "quill/Backend.h"
#include "quill/LogMacros.h"

#define HLOG_INFO(logger, ...)  do { if (logger) { LOG_INFO(logger,  __VA_ARGS__); } } while(0)
#define HLOG_WARN(logger, ...)  do { if (logger) { LOG_WARNING(logger, __VA_ARGS__); } } while(0)
#define HLOG_DEBUG(logger, ...) do { if (logger) { LOG_DEBUG(logger, __VA_ARGS__); } } while(0)

size_t HashTableNaive::index_of(const uint8_t key[16]) const {
    uint64_t h;
    std::memcpy(&h, key, sizeof(h));
    return static_cast<size_t>(h) & table_mask_;
}

void HashTableNaive::build(size_t num_entries) {
    if (num_entries == 0)
        throw std::invalid_argument("num_entries must be > 0");

    size_t capacity = static_cast<size_t>(num_entries / 0.7) + 1;
    size_t pow2 = 1;
    while (pow2 < capacity)
        pow2 <<= 1;

    SlotNaive empty{};
    empty.offset = NAIVE_SLOT_EMPTY;
    slots_.assign(pow2, empty);
    table_mask_ = pow2 - 1;
    count_      = 0;
}

bool HashTableNaive::insert(const uint8_t key[16], uint32_t offset) {
    size_t idx = index_of(key);

    while (slots_[idx].offset != NAIVE_SLOT_EMPTY) {
        if (ht_key_match(&slots_[idx], key))
            return false;
        idx = (idx + 1) & table_mask_;
    }

    std::memcpy(slots_[idx].key, key, 12);
    slots_[idx].offset = offset;
    ++count_;
    return true;
}

std::string_view HashTableNaive::lookup(const uint8_t* query_hash, const char* pool) const {
    size_t idx = index_of(query_hash);

    while (slots_[idx].offset != NAIVE_SLOT_EMPTY) {
        size_t next = (idx + 1) & table_mask_;
        __builtin_prefetch(&slots_[next], 0, 1);
        if (ht_key_match(&slots_[idx], query_hash))
            return std::string_view(pool + slots_[idx].offset);
        idx = next;
    }

    return {};
}

BuildStats HashTableNaive::load(std::istream& src, PasswordPool& pool, quill::Logger* logger) {
    BuildStats stats;
    auto t_start = std::chrono::steady_clock::now();

    size_t valid_count = 0;
    std::string line;

    while (std::getline(src, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) { stats.skip_empty++; continue; }

        if (line.find('\0') != std::string::npos) {
            stats.skip_null_byte++;
            if (stats.sample_null_byte.size() < 5) {
                std::string display;
                for (unsigned char c : line)
                    display += (c == '\0') ? std::string("[NUL]") : std::string(1, c);
                stats.sample_null_byte.push_back(display);
            }
            continue;
        }

        if (line.size() > TINY_PTR_MAX_LEN) {
            stats.skip_too_long++;
            if (line.size() > stats.max_rejected_len)
                stats.max_rejected_len = line.size();
            HLOG_DEBUG(logger, "[naive] skipping password over max length ({} chars)", line.size());
            continue;
        }

        valid_count++;
    }

    if (valid_count == 0) {
        HLOG_WARN(logger, "[naive] no valid passwords found in wordlist");
        return stats;
    }

    build(valid_count);

    src.clear();
    src.seekg(0);

    size_t lines_processed = 0;

    while (std::getline(src, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty() || line.find('\0') != std::string::npos || line.size() > TINY_PTR_MAX_LEN)
            continue;

        uint32_t offset = static_cast<uint32_t>(pool.size());
        uint32_t tp = pool.add(line);
        if (tp == 0xFFFFFFFF) {
            HLOG_WARN(logger, "[naive] password pool full, stopping load early");
            break;
        }

        uint8_t hash[16];
        unsigned int hlen = 16;
        EVP_Digest(line.data(), line.size(), hash, &hlen, EVP_md5(), nullptr);

        if (!insert(hash, offset))
            stats.skip_duplicate++;
        else
            stats.loaded++;

        lines_processed++;
        if (lines_processed % 1000000 == 0)
            HLOG_DEBUG(logger, "[naive] load progress: {} entries processed", lines_processed);
    }

    auto t_end = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    size_t total_skipped = stats.total_skipped();
    double skip_pct = (stats.loaded + total_skipped) > 0
        ? 100.0 * total_skipped / (stats.loaded + total_skipped) : 0.0;

    HLOG_INFO(logger, "[naive] hash table build complete in {:.1f} ms", elapsed_ms);
    HLOG_INFO(logger, "[naive]   entries loaded:          {}", stats.loaded);
    HLOG_INFO(logger, "[naive]   total skipped:           {} ({:.1f}%)", total_skipped, skip_pct);
    HLOG_INFO(logger, "[naive]     duplicates:            {}", stats.skip_duplicate);
    HLOG_INFO(logger, "[naive]     too long (>{} chars):  {}", TINY_PTR_MAX_LEN, stats.skip_too_long);
    HLOG_INFO(logger, "[naive]     empty lines:           {}", stats.skip_empty);
    HLOG_INFO(logger, "[naive]     embedded null bytes:   {}", stats.skip_null_byte);

    if (stats.max_rejected_len > 0)
        HLOG_INFO(logger, "[naive]   longest rejected password: {} chars", stats.max_rejected_len);

    if (!stats.sample_null_byte.empty()) {
        HLOG_WARN(logger, "[naive]   null-byte samples (first {}):", stats.sample_null_byte.size());
        for (const auto& s : stats.sample_null_byte)
            HLOG_WARN(logger, "[naive]     {}", s);
    }

    HLOG_INFO(logger, "[naive]   slot size: {} bytes", sizeof(SlotNaive));
    HLOG_INFO(logger, "[naive]   table capacity: {} slots ({} MB)",
        slots_.size(), (slots_.size() * sizeof(SlotNaive)) / (1024 * 1024));
#ifdef __SSE2__
    HLOG_INFO(logger, "[naive]   optimizations: SIMD key compare (SSE2), prefetch on lookup probe");
#else
    HLOG_INFO(logger, "[naive]   optimizations: none (SSE2 not available, using memcmp fallback)");
#endif

    return stats;
}
