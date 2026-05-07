#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>
#include "quill/Logger.h"
#include "src/cpp/build_stats.hpp"
#include "src/cpp/tiny_ptr.hpp"

// Hash table backed by std::unordered_map<string, string>.
// Node-based, heap-allocated per entry. No contiguous pool.
// Used as the naive baseline to show the cost of heap allocation
// and pointer chasing versus the open-addressed pool approach.

struct HashTableStdMap {
    HashTableStdMap() = default;

    // pool parameter is accepted for call-site consistency with the other
    // implementations but is not used. The map stores passwords directly.
    std::string_view lookup(const uint8_t* query_hash, const char* pool = nullptr) const;

    BuildStats load(std::istream& src, quill::Logger* logger = nullptr);

    size_t entry_count() const { return map_.size(); }

private:
    std::unordered_map<std::string, std::string> map_;
};
