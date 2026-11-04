#pragma once
#include "types.hpp"
#include <string>

// ---------------------------------------------------------------------
// Basic bitboard constants
// ---------------------------------------------------------------------
constexpr Bitboard EMPTY_BB = 0ULL;
constexpr Bitboard FULL_BB  = ~0ULL;

// ---------------------------------------------------------------------
// Bit manipulation helpers
//
// These are the "verbs" we'll use constantly once move generation
// begins. For now we only need set/clear/test and a way to count
// and locate bits, which we already use for debug printing and will
// reuse heavily later.
// ---------------------------------------------------------------------

inline void setBit(Bitboard& bb, int sq)   { bb |= (1ULL << sq); }
inline void clearBit(Bitboard& bb, int sq) { bb &= ~(1ULL << sq); }
inline bool testBit(Bitboard bb, int sq)   { return (bb >> sq) & 1ULL; }

// Number of set bits ("population count").
inline int popCount(Bitboard bb) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(bb);
#else
    int count = 0;
    while (bb) { bb &= bb - 1; ++count; }
    return count;
#endif
}

// Index of the least-significant set bit. Undefined if bb == 0.
inline int lsbIndex(Bitboard bb) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(bb);
#else
    int idx = 0;
    while (!(bb & 1ULL)) { bb >>= 1; ++idx; }
    return idx;
#endif
}

// Pop (return and clear) the least-significant set bit.
inline int popLsb(Bitboard& bb) {
    int idx = lsbIndex(bb);
    bb &= bb - 1; // clears the lowest set bit
    return idx;
}

// ---------------------------------------------------------------------
// Debug printing
// ---------------------------------------------------------------------
// Prints an 8x8 grid (rank 8 at top, like a real board) with '1' for
// occupied squares and '.' for empty ones. Useful for visually
// verifying any bitboard's contents during development.
std::string bitboardToString(Bitboard bb);
