/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: Google Test unit tests for the tiny pointer encode/decode
 * functions and PasswordPool. Covers boundary values, round-trip correctness,
 * and rejection of passwords that exceed the 31-byte or 128 MB limits.
 */

#include <gtest/gtest.h>
#include "src/cpp/hashtable/tiny_ptr.hpp"

TEST(TinyPtr, RoundTripMinValues) {
    uint32_t tp = encode_tiny_ptr(0, 1);
    EXPECT_EQ(get_offset(tp), 0u);
    EXPECT_EQ(get_length(tp), 1u);
}

TEST(TinyPtr, RoundTripMaxLength) {
    uint32_t tp = encode_tiny_ptr(0, TINY_PTR_MAX_LEN);
    EXPECT_EQ(get_offset(tp), 0u);
    EXPECT_EQ(get_length(tp), TINY_PTR_MAX_LEN);
}

TEST(TinyPtr, RoundTripLargeOffset) {
    uint32_t offset = (1u << 27) - 1;
    uint32_t tp = encode_tiny_ptr(offset, 1);
    EXPECT_EQ(get_offset(tp), offset);
    EXPECT_EQ(get_length(tp), 1u);
}

TEST(TinyPtr, LengthFieldIsolated) {
    uint32_t tp = encode_tiny_ptr(100, 15);
    EXPECT_EQ(get_length(tp), 15u);
    EXPECT_EQ(get_offset(tp), 100u);
}

TEST(PasswordPool, AddAndGet) {
    PasswordPool pool;
    uint32_t tp = pool.add("hello");
    EXPECT_NE(tp, 0xFFFFFFFFu);
    EXPECT_EQ(pool.get(tp), "hello");
}

TEST(PasswordPool, MultipleEntries) {
    PasswordPool pool;
    uint32_t tp1 = pool.add("first");
    uint32_t tp2 = pool.add("second");
    EXPECT_EQ(pool.get(tp1), "first");
    EXPECT_EQ(pool.get(tp2), "second");
}

TEST(PasswordPool, SkipsEmpty) {
    PasswordPool pool;
    EXPECT_EQ(pool.add(""), 0xFFFFFFFFu);
}

TEST(PasswordPool, SkipsOverMaxLen) {
    PasswordPool pool;
    std::string too_long(TINY_PTR_MAX_LEN + 1, 'a');
    EXPECT_EQ(pool.add(too_long), 0xFFFFFFFFu);
}

TEST(PasswordPool, AcceptsExactMaxLen) {
    PasswordPool pool;
    std::string exact(TINY_PTR_MAX_LEN, 'a');
    uint32_t tp = pool.add(exact);
    EXPECT_NE(tp, 0xFFFFFFFFu);
    EXPECT_EQ(pool.get(tp), exact);
}

TEST(PasswordPool, BasePointerValid) {
    PasswordPool pool;
    pool.add("test");
    EXPECT_NE(pool.base(), nullptr);
    EXPECT_EQ(std::string_view(pool.base(), 4), "test");
}
