/*
 * @author Ismail Alwahsh
 * @since May 10, 2026
 * @description: std::unordered_map hash table implementation. Stores passwords
 * directly in the map with no pool. Node-based with per-entry heap allocation.
 * Used as the second baseline to show the overhead of pointer chasing and heap
 * fragmentation versus the contiguous open-addressed pool tables.
 */

#include "src/cpp/hash_table_stdmap.hpp"

#include <chrono>
#include <cstring>
#include <string>

#include <openssl/evp.h>
#include "quill/Backend.h"
#include "quill/LogMacros.h"

#define HLOG_INFO(logger, ...)  do { if (logger) { LOG_INFO(logger,  __VA_ARGS__); } } while(0)
#define HLOG_WARN(logger, ...)  do { if (logger) { LOG_WARNING(logger, __VA_ARGS__); } } while(0)
#define HLOG_DEBUG(logger, ...) do { if (logger) { LOG_DEBUG(logger, __VA_ARGS__); } } while(0)

std::string_view HashTableStdMap::lookup(const uint8_t* query_hash, const char*) const {
    std::string key(reinterpret_cast<const char*>(query_hash), 16);
    auto it = map_.find(key);
    if (it == map_.end()) return {};
    return std::string_view(it->second);
}

BuildStats HashTableStdMap::load(std::istream& src, quill::Logger* logger) {
    BuildStats stats;
    auto t_start = std::chrono::steady_clock::now();

    // Pass 1: count valid entries so we can reserve capacity and avoid rehashing.
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
            continue;
        }

        valid_count++;
    }

    if (valid_count == 0) {
        HLOG_WARN(logger, "[stdmap] no valid passwords found in wordlist");
        return stats;
    }

    map_.reserve(valid_count);

    // Pass 2: insert entries.
    src.clear();
    src.seekg(0);

    size_t lines_processed = 0;

    while (std::getline(src, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty() || line.find('\0') != std::string::npos || line.size() > TINY_PTR_MAX_LEN)
            continue;

        uint8_t hash[16];
        unsigned int hlen = 16;
        EVP_Digest(line.data(), line.size(), hash, &hlen, EVP_md5(), nullptr);

        std::string key(reinterpret_cast<const char*>(hash), 16);
        auto [it, inserted] = map_.emplace(std::move(key), line);
        if (!inserted)
            stats.skip_duplicate++;
        else
            stats.loaded++;

        lines_processed++;
        if (lines_processed % 1000000 == 0)
            HLOG_DEBUG(logger, "[stdmap] load progress: {} entries processed", lines_processed);
    }

    auto t_end = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    size_t total_skipped = stats.total_skipped();
    double skip_pct = (stats.loaded + total_skipped) > 0
        ? 100.0 * total_skipped / (stats.loaded + total_skipped) : 0.0;

    HLOG_INFO(logger, "[stdmap] hash table build complete in {:.1f} ms", elapsed_ms);
    HLOG_INFO(logger, "[stdmap]   entries loaded:          {}", stats.loaded);
    HLOG_INFO(logger, "[stdmap]   total skipped:           {} ({:.1f}%)", total_skipped, skip_pct);
    HLOG_INFO(logger, "[stdmap]     duplicates:            {}", stats.skip_duplicate);
    HLOG_INFO(logger, "[stdmap]     too long (>{} chars):  {}", TINY_PTR_MAX_LEN, stats.skip_too_long);
    HLOG_INFO(logger, "[stdmap]     empty lines:           {}", stats.skip_empty);
    HLOG_INFO(logger, "[stdmap]     embedded null bytes:   {}", stats.skip_null_byte);

    if (stats.max_rejected_len > 0)
        HLOG_INFO(logger, "[stdmap]   longest rejected password: {} chars", stats.max_rejected_len);

    if (!stats.sample_null_byte.empty()) {
        HLOG_WARN(logger, "[stdmap]   null-byte samples (first {}):", stats.sample_null_byte.size());
        for (const auto& s : stats.sample_null_byte)
            HLOG_WARN(logger, "[stdmap]     {}", s);
    }

    HLOG_INFO(logger, "[stdmap]   storage: node-based heap allocation, no contiguous pool");
    HLOG_INFO(logger, "[stdmap]   entries in map: {}", map_.size());

    return stats;
}
