#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <sstream>
#include <string>

#include "src/cpp/hash_table_stdmap.hpp"

static void md5_of(const std::string& s, uint8_t out[16]) {
    unsigned int len = 16;
    EVP_Digest(s.data(), s.size(), out, &len, EVP_md5(), nullptr);
}

TEST(HashTableStdMap, LookupAfterLoad) {
    std::istringstream src("password\nhello\nworld\n");
    HashTableStdMap table;
    table.load(src);

    uint8_t hash[16];
    md5_of("password", hash);
    EXPECT_EQ(table.lookup(hash), "password");
}

TEST(HashTableStdMap, MissReturnsEmpty) {
    std::istringstream src("hello\n");
    HashTableStdMap table;
    table.load(src);

    uint8_t hash[16];
    md5_of("notexist", hash);
    EXPECT_TRUE(table.lookup(hash).empty());
}

TEST(HashTableStdMap, LoadBasicStats) {
    std::istringstream src("abc\ndef\nghi\n");
    HashTableStdMap table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 3u);
    EXPECT_EQ(stats.total_skipped(), 0u);
    EXPECT_EQ(table.entry_count(), 3u);
}

TEST(HashTableStdMap, LoadSkipsDuplicates) {
    std::istringstream src("same\nsame\nsame\n");
    HashTableStdMap table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_duplicate, 2u);
}

TEST(HashTableStdMap, LoadSkipsTooLong) {
    std::string long_pw(TINY_PTR_MAX_LEN + 1, 'x');
    std::istringstream src("short\n" + long_pw + "\n");
    HashTableStdMap table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 1u);
    EXPECT_EQ(stats.skip_too_long, 1u);
}

TEST(HashTableStdMap, LoadSkipsEmpty) {
    std::istringstream src("abc\n\ndef\n\n");
    HashTableStdMap table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 2u);
    EXPECT_EQ(stats.skip_empty, 2u);
}

TEST(HashTableStdMap, PoolParamIgnored) {
    std::istringstream src("test\n");
    HashTableStdMap table;
    table.load(src);

    uint8_t hash[16];
    md5_of("test", hash);
    // pool pointer ignored, passing nullptr should be fine
    EXPECT_EQ(table.lookup(hash, nullptr), "test");
}

TEST(HashTableStdMap, LoadWindowsLineEndings) {
    std::istringstream src("abc\r\ndef\r\n");
    HashTableStdMap table;
    BuildStats stats = table.load(src);

    EXPECT_EQ(stats.loaded, 2u);
    uint8_t hash[16];
    md5_of("abc", hash);
    EXPECT_EQ(table.lookup(hash), "abc");
}
