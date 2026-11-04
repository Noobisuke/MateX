#include "attacks.hpp"
#include <array>

namespace {

std::array<Bitboard, 64> knightTable{};
std::array<Bitboard, 64> kingTable{};
bool tablesInitialized = false;

// Knight jump offsets expressed as (file delta, rank delta).
constexpr int KNIGHT_DELTAS[8][2] = {
    {1, 2}, {2, 1}, {2, -1}, {1, -2},
    {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
};

// King's 8 neighboring squares.
constexpr int KING_DELTAS[8][2] = {
    {1, 0}, {1, 1}, {0, 1}, {-1, 1},
    {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
};

// Bishop/rook ray directions, expressed as (file delta, rank delta),
// applied repeatedly until we fall off the board or hit a blocker.
constexpr int BISHOP_DIRS[4][2] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
constexpr int ROOK_DIRS[4][2]   = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

Bitboard computeLeaperAttacks(int sq, const int deltas[8][2]) {
    Bitboard bb = EMPTY_BB;
    int f = fileOf(sq);
    int r = rankOf(sq);
    for (int i = 0; i < 8; ++i) {
        int nf = f + deltas[i][0];
        int nr = r + deltas[i][1];
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
            setBit(bb, squareOf(nf, nr));
        }
    }
    return bb;
}

Bitboard computeSlidingAttacks(int sq, Bitboard occupied, const int dirs[4][2]) {
    Bitboard bb = EMPTY_BB;
    int f = fileOf(sq);
    int r = rankOf(sq);
    for (int d = 0; d < 4; ++d) {
        int nf = f + dirs[d][0];
        int nr = r + dirs[d][1];
        while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
            int nsq = squareOf(nf, nr);
            setBit(bb, nsq);
            if (testBit(occupied, nsq)) {
                // A piece here blocks further sliding in this direction,
                // but the blocker's square itself IS attacked (relevant
                // even if the blocker is a friendly piece - it's still
                // "defended" from this square's perspective).
                break;
            }
            nf += dirs[d][0];
            nr += dirs[d][1];
        }
    }
    return bb;
}

} // namespace

void initAttackTables() {
    if (tablesInitialized) return;
    for (int sq = 0; sq < 64; ++sq) {
        knightTable[sq] = computeLeaperAttacks(sq, KNIGHT_DELTAS);
        kingTable[sq]   = computeLeaperAttacks(sq, KING_DELTAS);
    }
    tablesInitialized = true;
}

Bitboard knightAttacksFrom(int sq) {
    initAttackTables();
    return knightTable[sq];
}

Bitboard kingAttacksFrom(int sq) {
    initAttackTables();
    return kingTable[sq];
}

Bitboard pawnAttacksFrom(int sq, Color c) {
    Bitboard bb = EMPTY_BB;
    int f = fileOf(sq);
    int r = rankOf(sq);
    int forward = (c == WHITE) ? 1 : -1;
    int nr = r + forward;
    if (nr < 0 || nr > 7) return bb; // pawn on back rank; no forward attacks

    if (f - 1 >= 0) setBit(bb, squareOf(f - 1, nr));
    if (f + 1 <= 7) setBit(bb, squareOf(f + 1, nr));
    return bb;
}

Bitboard bishopAttacksFrom(int sq, Bitboard occupied) {
    return computeSlidingAttacks(sq, occupied, BISHOP_DIRS);
}

Bitboard rookAttacksFrom(int sq, Bitboard occupied) {
    return computeSlidingAttacks(sq, occupied, ROOK_DIRS);
}

Bitboard queenAttacksFrom(int sq, Bitboard occupied) {
    return bishopAttacksFrom(sq, occupied) | rookAttacksFrom(sq, occupied);
}

bool isSquareAttacked(const Position& pos, int square, Color byColor) {
    Bitboard occupied = pos.occupancyAll();

    // Pawns: is there an enemy(=byColor) pawn on a square that attacks
    // `square`? Equivalent trick: compute where a pawn of the OPPOSITE
    // color standing on `square` would attack, and check if an actual
    // `byColor` pawn sits there. This avoids iterating every pawn.
    if (pawnAttacksFrom(square, ~byColor) & pos.pieces(byColor, PAWN)) return true;

    if (knightAttacksFrom(square) & pos.pieces(byColor, KNIGHT)) return true;

    if (kingAttacksFrom(square) & pos.pieces(byColor, KING)) return true;

    Bitboard bishopsQueens = pos.pieces(byColor, BISHOP) | pos.pieces(byColor, QUEEN);
    if (bishopAttacksFrom(square, occupied) & bishopsQueens) return true;

    Bitboard rooksQueens = pos.pieces(byColor, ROOK) | pos.pieces(byColor, QUEEN);
    if (rookAttacksFrom(square, occupied) & rooksQueens) return true;

    return false;
}

bool isInCheck(const Position& pos, Color color) {
    Bitboard kingBB = pos.pieces(color, KING);
    if (kingBB == EMPTY_BB) {
        // No king on the board — shouldn't happen in a real game, but
        // guard defensively rather than reading garbage.
        return false;
    }
    int kingSq = lsbIndex(kingBB);
    return isSquareAttacked(pos, kingSq, ~color);
}
