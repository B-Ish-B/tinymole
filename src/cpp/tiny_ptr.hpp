/*
 * @author Ismail Alwahsh
 * @since May 10, 2026
 * @description: Tiny pointer encoding used by the hash table slots. Packs a
 * 27-bit pool byte offset and a 5-bit password length into a single uint32,
 * keeping each hash table slot at 24 bytes instead of 32. PasswordPool stores
 * all password strings in a flat char vector and returns tiny_ptr handles.
 * Max supported password length is 31 bytes; max pool size is 128 MB.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

static constexpr uint8_t  TINY_PTR_MAX_LEN  = 31;
static constexpr uint32_t TINY_PTR_MAX_POOL = (1u << 27); // 128 MB

inline uint32_t encode_tiny_ptr(uint32_t offset, uint8_t length) {
    return (offset << 5) | (length & 0x1F);
}

inline uint32_t get_offset(uint32_t tiny_ptr) {
    return tiny_ptr >> 5;
}

inline uint8_t get_length(uint32_t tiny_ptr) {
    return tiny_ptr & 0x1F;
}

struct PasswordPool {
    // Returns encoded tiny_ptr on success, or 0xFFFFFFFF to signal skip.
    // Skips empty passwords, passwords over 31 chars, and passwords that
    // would overflow the 27-bit offset field.
    uint32_t add(std::string_view pw) {
        if (pw.empty() || pw.size() > TINY_PTR_MAX_LEN)
            return 0xFFFFFFFF;
        uint32_t offset = static_cast<uint32_t>(data_.size());
        if (offset + pw.size() + 1 > TINY_PTR_MAX_POOL)
            return 0xFFFFFFFF;
        data_.insert(data_.end(), pw.begin(), pw.end());
        data_.push_back('\0');
        return encode_tiny_ptr(offset, static_cast<uint8_t>(pw.size()));
    }

    std::string_view get(uint32_t tiny_ptr) const {
        return std::string_view(data_.data() + get_offset(tiny_ptr), get_length(tiny_ptr));
    }

    const char* base() const { return data_.data(); }
    size_t size()       const { return data_.size(); }

private:
    std::vector<char> data_;
};
