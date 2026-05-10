/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Google Test unit tests for the naive open-addressed hash table.
 * Same test coverage as test_hash_table.cpp to verify behavioral parity between
 * the naive 32-byte-slot implementation and the tiny pointer implementation.
 */

#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <sstream>
#include <string>

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table_naive.hpp"

static void md5_of(const std::string& s, uint8_t out[16]) {
    unsigned int len = 16;
    EVP_Digest(s.data(), s.size(), out, &len, EVP_md5(), nullptr);
}

TEST(HashTableNaive, SlotSize) {
    EXPECT_EQ(sizeof(SlotNaive), 32u);
}

TEST(HashTableNaive, BuildAndLookupSingle) {
    PasswordPool    pool;
    HashTableNaive  table;

    uint64_t offset = static_cast<uint64_t>(pool.size());
    pool.add("password");

    uint8_t hash[16];
    md5_of("password", hash);

    table.build(1);
    table.insert(hash, offset);

    EXPECT_EQ(table.lookup(hash, pool.base()), "password");
}

TEST(HashTableNaive, MissReturnsEmpty) {
    PasswordPool   pool;
    HashTableNaive table;

    uint64_t offset = static_cast<uint64_t>(pool.size());
    pool.add("hello");

    uint8_t hash_hello[16], hash_other[16];
    md5_of("hello",    hash_hello);
    md5_of("notexist", hash_other);

    table.build(1);
    table.insert(hash_hello, offset);

    EXPECT_TRUE(table.lookup(hash_other, pool.base()).empty());
}

TEST(HashTableNaive, DuplicateInsertReturnsFalse) {
    PasswordPool   pool;
    HashTableNaive table;

    uint64_t offset = static_cast<uint64_t>(pool.size());
    pool.add("dup");

    uint8_t hash[16];
    md5_of("dup", hash);

    table.build(2);
    EXPECT_TRUE(table.insert(hash, offset));
    EXPECT_FALSE(table.insert(hash, offset));
    EXPECT_EQ(table.entry_count(), 1u);
}

TEST(HashTableNaive, LoadBasic) {
    std::istringstream src("password\nhello\nworld\n");
    PasswordPool   pool;
    HashTableNaive table;
    BuildStats     stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 3u);
    EXPECT_EQ(stats.total_skipped(), 0u);
}

TEST(HashTableNaive, LoadSkipsDuplicates) {
    std::istringstream src("same\nsame\nsame\n");
    PasswordPool   pool;
    HashTableNaive table;
    BuildStats     stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_duplicate, 2u);
}

TEST(HashTableNaive, LoadLookupAfterLoad) {
    std::istringstream src("hunter2\nletmein\n");
    PasswordPool   pool;
    HashTableNaive table;
    table.load(src, pool);

    uint8_t hash[16];
    md5_of("hunter2", hash);
    EXPECT_EQ(table.lookup(hash, pool.base()), "hunter2");
}

TEST(HashTableNaive, LoadSkipsTooLong) {
    std::string long_pw(TINY_PTR_MAX_LEN + 1, 'x');
    std::istringstream src("short\n" + long_pw + "\n");
    PasswordPool   pool;
    HashTableNaive table;
    BuildStats     stats = table.load(src, pool);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_too_long, 1u);
}

TEST(HashTableNaive, CapacityIsPowerOfTwo) {
    HashTableNaive table;
    table.build(1000);
    size_t cap = table.capacity();
    EXPECT_GT(cap, 0u);
    EXPECT_EQ(cap & (cap - 1), 0u);
}
