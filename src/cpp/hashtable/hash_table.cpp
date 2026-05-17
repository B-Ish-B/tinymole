/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Tiny pointer hash table implementation. Handles table sizing,
 * linear probe insert and lookup, and wordlist loading. Reports skip counts
 * (too long, empty, null byte, duplicate) through BuildStats for logging.
 */

#include "src/cpp/hashtable/hash_table.hpp"
#include "src/cpp/hashtable/ht_ops.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>
#include "quill/Backend.h"
#include "quill/LogMacros.h"

// Null-safe log wrappers. Tests pass logger=nullptr so every log site
// must go through these rather than calling LOG_* directly.
#define HLOG_INFO(logger, ...)  do { if (logger) { LOG_INFO(logger,  __VA_ARGS__); } } while(0)
#define HLOG_WARN(logger, ...)  do { if (logger) { LOG_WARNING(logger, __VA_ARGS__); } } while(0)
#define HLOG_DEBUG(logger, ...) do { if (logger) { LOG_DEBUG(logger, __VA_ARGS__); } } while(0)

size_t HashTable::index_of(const uint8_t key[16]) const {
    uint64_t h;
    std::memcpy(&h, key, sizeof(h));
    return static_cast<size_t>(h) & table_mask_;
}

void HashTable::build(size_t num_entries) {
    if (num_entries == 0)
        throw std::invalid_argument("num_entries must be > 0");

    size_t capacity = static_cast<size_t>(num_entries / 0.7) + 1;

    size_t pow2 = 1;
    while (pow2 < capacity)
        pow2 <<= 1;

    slots_.assign(pow2, Slot{});
    table_mask_ = pow2 - 1;
    count_      = 0;
}

bool HashTable::insert(const uint8_t key[16], uint32_t tiny_ptr) {
    size_t idx = index_of(key);

    while (slots_[idx].tiny_ptr != 0) {
        if (ht_key_match(&slots_[idx], key))
            return false;
        idx = (idx + 1) & table_mask_;
    }

    std::memcpy(slots_[idx].key, key, 12);
    slots_[idx].tiny_ptr = tiny_ptr;
    ++count_;
    return true;
}

std::string_view HashTable::lookup(const uint8_t* query_hash, const char* pool) const {
    size_t idx = index_of(query_hash);

    while (slots_[idx].tiny_ptr != 0) {
        size_t next = (idx + 1) & table_mask_;
        __builtin_prefetch(&slots_[next], 0, 1);
        if (ht_key_match(&slots_[idx], query_hash)) {
            return std::string_view(pool + get_offset(slots_[idx].tiny_ptr),
                                    get_length(slots_[idx].tiny_ptr));
        }
        idx = next;
    }

    return {};
}

BuildStats HashTable::load(std::istream& src, PasswordPool& pool, quill::Logger* logger) {
    BuildStats stats;
    auto t_start = std::chrono::steady_clock::now();

    // Pass 1: validate lines, collect rejection stats, count insertable entries.
    // We must know the count before calling build() to size the table correctly.
    size_t valid_count = 0;
    std::string line;

    while (std::getline(src, line)) {
        // Strip Windows line endings
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) {
            stats.skip_empty++;
            continue;
        }

        // Check for embedded null bytes. getline reads them as regular characters.
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

        if (line.size() > TINY_PTR_MAX_LEN) {
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

    build(valid_count);

    // Pass 2: insert entries into the table.
    src.clear();
    src.seekg(0);

    size_t lines_processed = 0;

    while (std::getline(src, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty() || line.find('\0') != std::string::npos || line.size() > TINY_PTR_MAX_LEN)
            continue;

        uint32_t tp = pool.add(line);
        if (tp == 0xFFFFFFFF) {
            // pool capacity exhausted (128 MB limit)
            HLOG_WARN(logger, "password pool full, stopping load early");
            break;
        }

        uint8_t hash[16];
        unsigned int hlen = 16;
        EVP_Digest(line.data(), line.size(), hash, &hlen, EVP_md5(), nullptr);

        if (!insert(hash, tp))
            stats.skip_duplicate++;
        else
            stats.loaded++;

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

    HLOG_INFO(logger, "hash table build complete in {:.1f} ms", elapsed_ms);
    HLOG_INFO(logger, "  entries loaded:          {}", stats.loaded);
    HLOG_INFO(logger, "  total skipped:           {} ({:.1f}%)", total_skipped, skip_pct);
    HLOG_INFO(logger, "    duplicates:            {}", stats.skip_duplicate);
    HLOG_INFO(logger, "    too long (>{} chars):  {}", TINY_PTR_MAX_LEN, stats.skip_too_long);
    HLOG_INFO(logger, "    empty lines:           {}", stats.skip_empty);
    HLOG_INFO(logger, "    embedded null bytes:   {}", stats.skip_null_byte);

    if (stats.max_rejected_len > 0)
        HLOG_INFO(logger, "  longest rejected password: {} chars", stats.max_rejected_len);

    if (!stats.sample_null_byte.empty()) {
        HLOG_WARN(logger, "  null-byte password samples (first {}):", stats.sample_null_byte.size());
        for (const auto& s : stats.sample_null_byte)
            HLOG_WARN(logger, "    {}", s);
    }

    HLOG_INFO(logger, "  slot size:               {} bytes (key truncated to 12B, sentinel replaces occupied flag)",
        sizeof(Slot));
    HLOG_INFO(logger, "  table capacity:          {} slots ({} MB)",
        slots_.size(),
        (slots_.size() * sizeof(Slot)) / (1024 * 1024));
#ifdef __SSE2__
    HLOG_INFO(logger, "  optimizations:           SIMD key compare (SSE2), prefetch on lookup probe");
#else
    HLOG_INFO(logger, "  optimizations:           none (SSE2 not available, using memcmp fallback)");
#endif

    return stats;
}
