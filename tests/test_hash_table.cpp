/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Google Test unit tests for the tiny pointer hash table. Covers
 * insert, lookup, duplicate rejection, load factor handling, and large insert
 * correctness. Hash inputs are real MD5 digests computed via OpenSSL.
 */

#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <cstring>
#include <sstream>
#include <string>

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table.hpp"

static void md5_of(const std::string& s, uint8_t out[16]) {
    unsigned int len = 16;
    EVP_Digest(s.data(), s.size(), out, &len, EVP_md5(), nullptr);
}

TEST(HashTable, BuildAndLookupSingle) {
    PasswordPool pool;
    HashTable    table;

    uint32_t tp = pool.add("password");
    ASSERT_NE(tp, 0xFFFFFFFFu);

    uint8_t hash[16];
    md5_of("password", hash);

    table.build(1);
    table.insert(hash, tp);

    auto result = table.lookup(hash, pool.base());
    EXPECT_EQ(result, "password");
}

TEST(HashTable, MissReturnsEmpty) {
    PasswordPool pool;
    HashTable    table;

    uint32_t tp = pool.add("hello");
    ASSERT_NE(tp, 0xFFFFFFFFu);

    uint8_t hash_hello[16], hash_other[16];
    md5_of("hello",    hash_hello);
    md5_of("notexist", hash_other);

    table.build(1);
    table.insert(hash_hello, tp);

    auto result = table.lookup(hash_other, pool.base());
    EXPECT_TRUE(result.empty());
}

TEST(HashTable, DuplicateInsertReturnsFalse) {
    PasswordPool pool;
    HashTable    table;

    uint32_t tp = pool.add("dup");
    ASSERT_NE(tp, 0xFFFFFFFFu);

    uint8_t hash[16];
    md5_of("dup", hash);

    table.build(2);
    EXPECT_TRUE(table.insert(hash, tp));
    EXPECT_FALSE(table.insert(hash, tp));
    EXPECT_EQ(table.entry_count(), 1u);
}

TEST(HashTable, LargeInsertAndLookup) {
    const size_t N = 100000;
    PasswordPool pool;
    HashTable    table;
    table.build(N);

    std::vector<std::string> words(N);
    for (size_t i = 0; i < N; ++i) {
        words[i] = "pw" + std::to_string(i);
        uint32_t tp = pool.add(words[i]);
        ASSERT_NE(tp, 0xFFFFFFFFu);
        uint8_t hash[16];
        md5_of(words[i], hash);
        table.insert(hash, tp);
    }

    EXPECT_EQ(table.entry_count(), N);

    for (size_t i = 0; i < N; ++i) {
        uint8_t hash[16];
        md5_of(words[i], hash);
        auto result = table.lookup(hash, pool.base());
        EXPECT_EQ(result, words[i]);
    }
}

TEST(HashTable, CapacityIsPowerOfTwo) {
    HashTable table;
    table.build(1000);
    size_t cap = table.capacity();
    EXPECT_GT(cap, 0u);
    EXPECT_EQ(cap & (cap - 1), 0u);
}

TEST(HashTable, BuildThrowsOnZero) {
    HashTable table;
    EXPECT_THROW(table.build(0), std::invalid_argument);
}

// load() tests — logger is nullptr throughout so no Quill setup is needed

TEST(HashTable, LoadBasic) {
    std::istringstream src("password\nhello\nworld\n");
    PasswordPool pool;
    HashTable    table;
    BuildStats   stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 3u);
    EXPECT_EQ(stats.total_skipped(), 0u);
    EXPECT_EQ(table.entry_count(), 3u);
}

TEST(HashTable, LoadLookupAfterLoad) {
    std::istringstream src("hunter2\nletmein\n");
    PasswordPool pool;
    HashTable    table;
    table.load(src, pool);

    uint8_t hash[16];
    unsigned int hlen = 16;
    EVP_Digest("hunter2", 7, hash, &hlen, EVP_md5(), nullptr);

    auto result = table.lookup(hash, pool.base());
    EXPECT_EQ(result, "hunter2");
}

TEST(HashTable, LoadSkipsEmpty) {
    std::istringstream src("abc\n\ndef\n\n");
    PasswordPool pool;
    HashTable    table;
    BuildStats   stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 2u);
    EXPECT_EQ(stats.skip_empty, 2u);
}

TEST(HashTable, LoadSkipsTooLong) {
    std::string long_pw(TINY_PTR_MAX_LEN + 1, 'x');
    std::istringstream src("short\n" + long_pw + "\n");
    PasswordPool pool;
    HashTable    table;
    BuildStats   stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_too_long, 1u);
    EXPECT_EQ(stats.max_rejected_len, TINY_PTR_MAX_LEN + 1);
}

TEST(HashTable, LoadSkipsDuplicates) {
    std::istringstream src("same\nsame\nsame\n");
    PasswordPool pool;
    HashTable    table;
    BuildStats   stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_duplicate, 2u);
}

TEST(HashTable, LoadNullByteSample) {
    std::string line_with_null = "ab";
    line_with_null += '\0';
    line_with_null += "cd\n";
    std::istringstream src(line_with_null);
    PasswordPool pool;
    HashTable    table;
    BuildStats   stats = table.load(src, pool);

    EXPECT_EQ(stats.skip_null_byte, 1u);
    EXPECT_EQ(stats.sample_null_byte.size(), 1u);
    EXPECT_NE(stats.sample_null_byte[0].find("[NUL]"), std::string::npos);
}

TEST(HashTable, LoadWindowsLineEndings) {
    std::istringstream src("abc\r\ndef\r\n");
    PasswordPool pool;
    HashTable    table;
    BuildStats   stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 2u);

    uint8_t hash[16];
    unsigned int hlen = 16;
    EVP_Digest("abc", 3, hash, &hlen, EVP_md5(), nullptr);
    EXPECT_EQ(table.lookup(hash, pool.base()), "abc");
}
