#include "src/cpp/hash_table.hpp"

#include <cstring>
#include <stdexcept>

// Takes the first 8 bytes of the MD5 key reinterpreted as a uint64_t.
// The MD5 output is already a well-distributed hash so no further mixing
// is needed.
size_t HashTable::index_of(const uint8_t key[16]) const {
    uint64_t h;
    std::memcpy(&h, key, sizeof(h));
    return static_cast<size_t>(h) & table_mask_;
}

void HashTable::build(size_t num_entries) {
    if (num_entries == 0)
        throw std::invalid_argument("num_entries must be > 0");

    // Target load factor 0.7: capacity = ceil(num_entries / 0.7)
    size_t capacity = static_cast<size_t>(num_entries / 0.7) + 1;

    // Round up to next power of two for fast bitwise modulo
    size_t pow2 = 1;
    while (pow2 < capacity)
        pow2 <<= 1;

    slots_.assign(pow2, Slot{});
    table_mask_ = pow2 - 1;
    count_      = 0;
}

bool HashTable::insert(const uint8_t key[16], uint32_t tiny_ptr) {
    size_t idx = index_of(key);

    while (slots_[idx].occupied) {
        if (std::memcmp(slots_[idx].key, key, 16) == 0)
            return false; // duplicate
        idx = (idx + 1) & table_mask_;
    }

    std::memcpy(slots_[idx].key, key, 16);
    slots_[idx].tiny_ptr = tiny_ptr;
    slots_[idx].occupied = true;
    ++count_;
    return true;
}

std::string_view HashTable::lookup(const uint8_t* query_hash, const char* pool) const {
    size_t idx = index_of(query_hash);

    while (slots_[idx].occupied) {
        if (std::memcmp(slots_[idx].key, query_hash, 16) == 0) {
            uint32_t offset = get_offset(slots_[idx].tiny_ptr);
            uint8_t  length = get_length(slots_[idx].tiny_ptr);
            return std::string_view(pool + offset, length);
        }
        idx = (idx + 1) & table_mask_;
    }

    return {};
}
