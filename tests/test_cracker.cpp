/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Google Test integration tests for the cracker. Tests
 * partition_candidates distribution, single and multi-thread crack correctness,
 * not-found behavior, and algorithm selection (MD5, SHA-1, SHA-256).
 */

#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <cstring>
#include <string>
#include <vector>

#include "src/cpp/hashtable/tiny_ptr.hpp"
#include "src/cpp/hashtable/hash_table.hpp"
#include "src/cpp/cracker/cracker.hpp"

static void md5_of(const std::string& s, uint8_t out[16]) {
    unsigned int len = 16;
    EVP_Digest(s.data(), s.size(), out, &len, EVP_md5(), nullptr);
}

// --- partition_candidates ---

TEST(PartitionCandidates, EvenSplit) {
    std::vector<std::string> cands = {"a", "b", "c", "d"};
    auto parts = partition_candidates(cands, 2);
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].size(), 2u);
    EXPECT_EQ(parts[1].size(), 2u);
}

TEST(PartitionCandidates, RoundRobinOrder) {
    std::vector<std::string> cands = {"a", "b", "c", "d", "e"};
    auto parts = partition_candidates(cands, 2);
    // thread 0 gets indices 0, 2, 4 -> "a", "c", "e"
    // thread 1 gets indices 1, 3    -> "b", "d"
    EXPECT_EQ(parts[0][0], "a");
    EXPECT_EQ(parts[0][1], "c");
    EXPECT_EQ(parts[0][2], "e");
    EXPECT_EQ(parts[1][0], "b");
    EXPECT_EQ(parts[1][1], "d");
}

TEST(PartitionCandidates, FewerCandsThanThreads) {
    std::vector<std::string> cands = {"only"};
    auto parts = partition_candidates(cands, 4);
    ASSERT_EQ(parts.size(), 4u);
    EXPECT_EQ(parts[0].size(), 1u);
    EXPECT_EQ(parts[1].size(), 0u);
    EXPECT_EQ(parts[2].size(), 0u);
    EXPECT_EQ(parts[3].size(), 0u);
}

TEST(PartitionCandidates, EmptyCandidates) {
    std::vector<std::string> cands;
    auto parts = partition_candidates(cands, 3);
    ASSERT_EQ(parts.size(), 3u);
    for (const auto& p : parts) EXPECT_TRUE(p.empty());
}

// --- crack ---

static void setup_table(const std::vector<std::string>& words,
                        PasswordPool& pool, HashTable& table) {
    table.build(words.size());
    for (const auto& w : words) {
        uint32_t tp = pool.add(w);
        if (tp == 0xFFFFFFFFu) continue;
        uint8_t hash[16];
        md5_of(w, hash);
        table.insert(hash, tp);
    }
}

TEST(Crack, FindsPasswordSingleThread) {
    PasswordPool pool;
    HashTable    table;
    setup_table({"hunter2", "password", "letmein"}, pool, table);

    uint8_t target[16];
    md5_of("password", target);

    std::vector<std::string> cands = {"hunter2", "password", "letmein"};
    std::string result = crack(cands, table, pool, target, 1);
    EXPECT_EQ(result, "password");
}

TEST(Crack, FindsPasswordMultiThread) {
    PasswordPool pool;
    HashTable    table;
    setup_table({"hunter2", "password", "letmein"}, pool, table);

    uint8_t target[16];
    md5_of("letmein", target);

    std::vector<std::string> cands = {"hunter2", "password", "letmein"};
    std::string result = crack(cands, table, pool, target, 4);
    EXPECT_EQ(result, "letmein");
}

TEST(Crack, ReturnsEmptyOnMiss) {
    PasswordPool pool;
    HashTable    table;
    setup_table({"hunter2", "password"}, pool, table);

    uint8_t target[16];
    md5_of("notinthelist", target);

    std::vector<std::string> cands = {"hunter2", "password"};
    std::string result = crack(cands, table, pool, target, 2);
    EXPECT_TRUE(result.empty());
}

TEST(Crack, EmptyCandidatesReturnsMiss) {
    PasswordPool pool;
    HashTable    table;
    setup_table({"password"}, pool, table);

    uint8_t target[16];
    md5_of("password", target);

    std::vector<std::string> cands;
    std::string result = crack(cands, table, pool, target, 2);
    EXPECT_TRUE(result.empty());
}

TEST(Crack, FirstCandidateIsTarget) {
    PasswordPool pool;
    HashTable    table;
    setup_table({"abc", "def", "ghi"}, pool, table);

    uint8_t target[16];
    md5_of("abc", target);

    std::vector<std::string> cands = {"abc", "def", "ghi"};
    std::string result = crack(cands, table, pool, target, 2);
    EXPECT_EQ(result, "abc");
}

TEST(Crack, LastCandidateIsTarget) {
    const size_t N = 100;
    std::vector<std::string> words;
    words.reserve(N);
    for (size_t i = 0; i < N; ++i)
        words.push_back("pw" + std::to_string(i));

    PasswordPool pool;
    HashTable    table;
    setup_table(words, pool, table);

    uint8_t target[16];
    md5_of(words.back(), target);

    std::string result = crack(words, table, pool, target, 4);
    EXPECT_EQ(result, words.back());
}

TEST(Crack, FallbackToCandidateWhenNotInTable) {
    // Build an empty table, but feed a candidate whose hash matches target.
    // worker falls back to the candidate string when table.lookup returns empty.
    PasswordPool pool;
    HashTable    table;
    table.build(1);

    uint8_t target[16];
    md5_of("fallback", target);

    std::vector<std::string> cands = {"fallback"};
    std::string result = crack(cands, table, pool, target, 1);
    EXPECT_EQ(result, "fallback");
}
