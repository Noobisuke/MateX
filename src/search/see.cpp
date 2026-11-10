#include "see.hpp"
#include "../board/attacks.hpp"
#include "../eval/evaluate.hpp"
#include <algorithm>

namespace {

constexpr int KING_SEE_VALUE = 20000;

// Every piece (both colors) that attacks `sq` given the occupancy `occ` (sliders see through removed pieces).
Bitboard attackersTo(const Position& pos, int sq, Bitboard occ) {
    const Bitboard bishopsQueens = pos.pieces(WHITE, BISHOP) | pos.pieces(BLACK, BISHOP) | pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN);
    const Bitboard rooksQueens = pos.pieces(WHITE, ROOK) | pos.pieces(BLACK, ROOK) | pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN);
    Bitboard a = EMPTY_BB;
    a |= pawnAttacksFrom(sq, BLACK) & pos.pieces(WHITE, PAWN);  // squares from which a white pawn attacks sq
    a |= pawnAttacksFrom(sq, WHITE) & pos.pieces(BLACK, PAWN);
    a |= knightAttacksFrom(sq) & (pos.pieces(WHITE, KNIGHT) | pos.pieces(BLACK, KNIGHT));
    a |= kingAttacksFrom(sq) & (pos.pieces(WHITE, KING) | pos.pieces(BLACK, KING));
    a |= bishopAttacksFrom(sq, occ) & bishopsQueens;
    a |= rookAttacksFrom(sq, occ) & rooksQueens;
    return a & occ;
}

// Least valuable piece of `side` among `attackers`; returns its square or -1.
int leastValuableAttacker(const Position& pos, Bitboard attackers, Color side, PieceType& type) {
    for (int pt = PAWN; pt <= KING; ++pt) {
        const Bitboard b = attackers & pos.pieces(side, static_cast<PieceType>(pt));
        if (b != EMPTY_BB) {
            type = static_cast<PieceType>(pt);
            return lsbIndex(b);
        }
    }
    return -1;
}

}  // namespace

int seePieceValue(PieceType pt) { return pt == KING ? KING_SEE_VALUE : pieceValue(pt); }

int see(const Position& pos, const Move& move) {
    const int to = move.to;
    int gain[40];
    int d = 0;

    PieceType onSquare = typeOf(move.piece);  // the piece now standing on `to` that the opponent may capture
    Bitboard occ = pos.occupancyAll();
    clearBit(occ, move.from);

    int first = 0;
    if (move.flag == EN_PASSANT) {
        first = seePieceValue(PAWN);
        const int victim = pos.sideToMove() == WHITE ? to - 8 : to + 8;
        clearBit(occ, victim);
    } else if (move.captured != NO_PIECE) {
        first = seePieceValue(typeOf(move.captured));
    }
    if (move.isPromotion()) {  // the pawn becomes the promoted piece and gains the difference
        first += seePieceValue(move.promotion) - seePieceValue(PAWN);
        onSquare = move.promotion;
    }
    gain[0] = first;

    Bitboard attackers = attackersTo(pos, to, occ);
    Color side = ~pos.sideToMove();
    for (;;) {
        PieceType attackerType;
        const int from = leastValuableAttacker(pos, attackers & occ, side, attackerType);
        if (from < 0) break;
        // A king may only capture when the other side has no attacker left, otherwise it would step into check.
        if (attackerType == KING && (attackers & occ & pos.occupancy(~side)) != EMPTY_BB) break;

        ++d;
        gain[d] = seePieceValue(onSquare) - gain[d - 1];  // what `side` nets if it recaptures here and nothing follows

        clearBit(occ, from);
        // Removing the attacker can uncover a slider behind it (x-ray): recompute the sliders through the new occupancy.
        attackers = attackersTo(pos, to, occ);
        onSquare = attackerType;
        side = ~side;
    }
    while (d > 0) {  // fold the swap list back: each side picks the better of stopping or capturing
        gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        --d;
    }
    return gain[0];
}

bool seeAtLeast(const Position& pos, const Move& move, int threshold) { return see(pos, move) >= threshold; }