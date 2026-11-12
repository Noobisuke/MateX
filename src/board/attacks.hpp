#pragma once
#include "position.hpp"

// ---------------------------------------------------------------------
// Attack detection
//
// Determines whether a given square is attacked by a given color,
// regardless of whose turn it is and regardless of whether a piece
// actually sits on that square.
//
// Stage 20: the attack lookups are inline table reads. Knight, king and pawn attacks are plain tables; bishop and
// rook attacks use "magic bitboards": the occupancy bits that matter on the piece's rays are multiplied by a
// per-square magic number and the top bits index a precomputed attack table.
// ---------------------------------------------------------------------

// Builds the tables. Runs automatically before main(); calling it again is harmless.
void initAttackTables();

bool isSquareAttacked(const Position& pos, int square, Color byColor);
bool isInCheck(const Position& pos, Color color);

namespace attack_detail {
struct Magic {
    Bitboard mask;          // the squares on the rays whose occupancy matters (board edges excluded)
    Bitboard magic;
    const Bitboard* table;  // 2^popcount(mask) entries for this square
    int shift;              // 64 - popcount(mask)
};
extern Bitboard knightTable[64];
extern Bitboard kingTable[64];
extern Bitboard pawnTable[2][64];
extern Magic bishopMagic[64];
extern Magic rookMagic[64];
}  // namespace attack_detail

// --- Lower-level helpers, exposed for testing and reuse ---
inline Bitboard knightAttacksFrom(int sq) { return attack_detail::knightTable[sq]; }
inline Bitboard kingAttacksFrom(int sq) { return attack_detail::kingTable[sq]; }
inline Bitboard pawnAttacksFrom(int sq, Color c) { return attack_detail::pawnTable[c][sq]; }

inline Bitboard bishopAttacksFrom(int sq, Bitboard occupied) {
    const attack_detail::Magic& m = attack_detail::bishopMagic[sq];
    return m.table[((occupied & m.mask) * m.magic) >> m.shift];
}
inline Bitboard rookAttacksFrom(int sq, Bitboard occupied) {
    const attack_detail::Magic& m = attack_detail::rookMagic[sq];
    return m.table[((occupied & m.mask) * m.magic) >> m.shift];
}
inline Bitboard queenAttacksFrom(int sq, Bitboard occupied) { return bishopAttacksFrom(sq, occupied) | rookAttacksFrom(sq, occupied); }
