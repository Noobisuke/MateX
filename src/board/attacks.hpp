#pragma once
#include "position.hpp"

// ---------------------------------------------------------------------
// Attack detection
//
// Determines whether a given square is attacked by a given color,
// regardless of whose turn it is and regardless of whether a piece
// actually sits on that square. This is the foundation for check
// detection, legal move filtering (Stage 5), and castling legality
// (Stage 6).
// ---------------------------------------------------------------------

// Must be called once before any attack queries (sets up the
// precomputed knight/king attack tables). Safe to call multiple times.
void initAttackTables();

// True if `square` is attacked by any piece of color `byColor` in the
// current position `pos`. "Attacked" means a piece of that color could
// capture on that square right now, ignoring whose turn it actually is
// and ignoring whether doing so would expose that side's own king.
bool isSquareAttacked(const Position& pos, int square, Color byColor);

// True if the side `color`'s king is currently attacked by the
// opposing color. Requires that `color` actually has a king on the
// board (always true for any legitimately-reached position).
bool isInCheck(const Position& pos, Color color);

// --- Lower-level helpers, exposed for testing and future reuse -------------

// Pseudo-attack squares for a knight standing on `sq`, ignoring occupancy.
Bitboard knightAttacksFrom(int sq);

// Pseudo-attack squares for a king standing on `sq`, ignoring occupancy.
Bitboard kingAttacksFrom(int sq);

// Squares a pawn of color `c` standing on `sq` attacks (diagonals only).
Bitboard pawnAttacksFrom(int sq, Color c);

// Sliding-piece attacks from `sq` given the board's full occupancy,
// stopping at (and including) the first blocker in each direction.
Bitboard bishopAttacksFrom(int sq, Bitboard occupied);
Bitboard rookAttacksFrom(int sq, Bitboard occupied);
Bitboard queenAttacksFrom(int sq, Bitboard occupied);
