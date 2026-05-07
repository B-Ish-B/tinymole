#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <cstring>
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
