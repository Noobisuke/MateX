#pragma once
#include "../board/position.hpp"
#include "../move/move.hpp"
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------
// Perft (Stage 9): count the leaf nodes of the legal move tree to a fixed depth.
//
// Perft is the standard proof that move generation is correct: a single missed or
// invented move, a wrong castling/en-passant/promotion rule, or a make/unmake bug
// changes a count that is published for well-known positions.
//
// Only generateLegalMoves + makeMove/unmakeMove are used. No draw rules, no history:
// perft counts every legal move sequence of the given length, nothing else.
//
// All functions take the Position by non-const reference because they make and unmake
// moves on it; it is restored exactly before returning.
// ---------------------------------------------------------------------

// Reference implementation: visits every leaf. perft(pos, 0) == 1.
uint64_t perft(Position& pos, int depth);

// Same result, but at depth 1 it counts the legal moves instead of making each one.
// Faster; kept separate so the plain version stays the unquestionable reference.
uint64_t perftBulk(Position& pos, int depth);

// One root move and the number of leaf nodes below it (perft of depth-1 after the move).
struct DivideEntry {
    Move move;
    uint64_t nodes = 0;
};

// "divide": per-root-move node counts. Comparing these with a reference engine's divide
// pinpoints the first move (and then, by repeating on that position, the exact position)
// where the two generators disagree. Returns an empty list for depth < 1.
std::vector<DivideEntry> divide(Position& pos, int depth);

// Breakdown of the LAST ply of the tree (the moves that reach the leaves), using the
// conventional perft-table categories.
struct PerftStats {
    uint64_t nodes = 0;
    uint64_t captures = 0;     // includes en passant and promotion captures
    uint64_t enPassant = 0;
    uint64_t castles = 0;
    uint64_t promotions = 0;   // quiet and capturing promotions
    uint64_t checks = 0;       // moves after which the opponent is in check (incl. discovered/double)
    uint64_t checkmates = 0;

    bool operator==(const PerftStats&) const = default;
    PerftStats& operator+=(const PerftStats& o);
};

// perftDetailed(pos, 0) has nodes == 1 and every other counter 0.
PerftStats perftDetailed(Position& pos, int depth);