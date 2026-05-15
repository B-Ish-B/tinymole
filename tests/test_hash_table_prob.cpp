/*
 * @author Ismail Alwahsh
 * @since May 15, 2026
 * Google Test unit tests for the probabilistic tiny pointer hash table.
 * Tests cover: basic insert/lookup, DEREFERENCE correctness (key-dependent),
 * primary bucket overflow to secondary, large insert/lookup, and miss behavior.
 */

#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "src/cpp/hash_table_prob.hpp"

static void md5_of(const std::string& s, uint8_t out[16]) {
    unsigned int len = 16;
    EVP_Digest(s.data(), s.size(), out, &len, EVP_md5(), nullptr);
}

// ---------------------------------------------------------------------------
// Basic load and lookup
// ---------------------------------------------------------------------------

TEST(HashTableProb, LoadBasic) {
    std::istringstream src("password\nhello\nworld\n");
    HashTableProb table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 3u);
    EXPECT_EQ(stats.total_skipped(), 0u);
    EXPECT_EQ(table.entry_count(), 3u);
}

TEST(HashTableProb, LookupAfterLoad) {
    std::istringstream src("hunter2\nletmein\n");
    HashTableProb table;
    table.load(src);

    uint8_t hash[16];
    md5_of("hunter2", hash);

    auto result = table.lookup(hash);
    EXPECT_EQ(result, "hunter2");
}

TEST(HashTableProb, MissReturnsEmpty) {
    std::istringstream src("hello\nworld\n");
    HashTableProb table;
    table.load(src);

    uint8_t hash[16];
    md5_of("notexist", hash);

    auto result = table.lookup(hash);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// Skip behavior
// ---------------------------------------------------------------------------

TEST(HashTableProb, LoadSkipsEmpty) {
    std::istringstream src("abc\n\ndef\n\n");
    HashTableProb table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 2u);
    EXPECT_EQ(stats.skip_empty, 2u);
}

TEST(HashTableProb, LoadSkipsTooLong) {
    std::string long_pw(PROB_MAX_LEN + 1, 'x');
    std::istringstream src("short\n" + long_pw + "\n");
    HashTableProb table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_too_long, 1u);
    EXPECT_EQ(stats.max_rejected_len, PROB_MAX_LEN + 1);
}

TEST(HashTableProb, LoadSkipsDuplicates) {
    std::istringstream src("same\nsame\nsame\n");
    HashTableProb table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_duplicate, 2u);
}

TEST(HashTableProb, LoadWindowsLineEndings) {
    std::istringstream src("abc\r\ndef\r\n");
    HashTableProb table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 2u);

    uint8_t hash[16];
    md5_of("abc", hash);
    EXPECT_EQ(table.lookup(hash), "abc");
}

// ---------------------------------------------------------------------------
// DEREFERENCE correctness: the lookup must use the key to find the bucket.
// This test inserts enough entries to guarantee some go to the secondary
// section, and verifies every entry round-trips correctly.
// ---------------------------------------------------------------------------

TEST(HashTableProb, DereferenceKeyDependent) {
    // Build a wordlist large enough that at least some primary buckets fill
    // and overflow to secondary (statistically certain at a few thousand entries).
    const size_t N = 5000;
    std::ostringstream oss;
    std::vector<std::string> words(N);
    for (size_t i = 0; i < N; ++i) {
        words[i] = "kw" + std::to_string(i);
        oss << words[i] << "\n";
    }
    std::istringstream src(oss.str());
    HashTableProb table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, N);

    // Every inserted password must round-trip via DEREFERENCE(key, tiny_ptr).
    for (const auto& w : words) {
        uint8_t hash[16];
        md5_of(w, hash);
        auto result = table.lookup(hash);
        EXPECT_EQ(result, w) << "round-trip failed for: " << w;
    }
}

// ---------------------------------------------------------------------------
// Large insert and lookup
// ---------------------------------------------------------------------------

TEST(HashTableProb, LargeInsertAndLookup) {
    const size_t N = 100000;
    std::ostringstream oss;
    std::vector<std::string> words(N);
    for (size_t i = 0; i < N; ++i) {
        words[i] = "pw" + std::to_string(i);
        oss << words[i] << "\n";
    }
    std::istringstream src(oss.str());
    HashTableProb table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, N);
    EXPECT_EQ(table.entry_count(), N);

    for (const auto& w : words) {
        uint8_t hash[16];
        md5_of(w, hash);
        EXPECT_EQ(table.lookup(hash), w);
    }
}

// ---------------------------------------------------------------------------
// Slot size check
// ---------------------------------------------------------------------------

TEST(HashTableProb, SlotIs20Bytes) {
    EXPECT_EQ(sizeof(ProbSlot), 20u);
}

TEST(HashTableProb, PoolSlotIs32Bytes) {
    EXPECT_EQ(PROB_POOL_SLOT_SIZE, 32u);
}
