/*
 * @author Ismail Alwahsh
 * @since May 15, 2026
 * Shared slot-level helpers used by all open-addressed hash table implementations.
 *
 * ht_key_match: compare the first 12 bytes of a 16-byte slot against a 16-byte
 * MD5 query. On x86-64 with SSE2 (always available) this compiles to a single
 * _mm_cmpeq_epi8 + _mm_movemask_epi8 pair rather than three scalar comparisons.
 * The full 16-byte slot is loaded into an XMM register and compared against the
 * full 16-byte query; only the first 12 result bits are checked, so the value
 * field (bytes 12-15) does not affect the result.
 */

#pragma once

#include <cstdint>
#include <cstring>

#ifdef __SSE2__
#include <immintrin.h>
#endif

// slot_ptr must point to the start of a 16-byte slot struct (key[12] at offset 0).
// query must point to a 16-byte MD5 hash.
// Returns true if bytes 0-11 of the slot match bytes 0-11 of query.
inline bool ht_key_match(const void* slot_ptr, const uint8_t* query) {
#ifdef __SSE2__
    __m128i sk  = _mm_loadu_si128(static_cast<const __m128i*>(slot_ptr));
    __m128i qk  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(query));
    return (_mm_movemask_epi8(_mm_cmpeq_epi8(sk, qk)) & 0x0FFF) == 0x0FFF;
#else
    return std::memcmp(slot_ptr, query, 12) == 0;
#endif
}
