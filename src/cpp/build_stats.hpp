/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: BuildStats struct returned by HashTable::load. Tracks how many
 * passwords were loaded and how many were skipped (too long, empty, contain a
 * null byte, or duplicate). Sample null-byte passwords are stored for logging.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct BuildStats {
    size_t loaded           = 0;
    size_t skip_too_long    = 0;
    size_t skip_empty       = 0;
    size_t skip_null_byte   = 0;
    size_t skip_duplicate   = 0;
    size_t max_rejected_len = 0;
    std::vector<std::string> sample_null_byte; // up to 5 examples with [NUL] markers

    size_t total_skipped() const {
        return skip_too_long + skip_empty + skip_null_byte + skip_duplicate;
    }
};
