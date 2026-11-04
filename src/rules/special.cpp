#include "special.hpp"
#include "../board/attacks.hpp"

namespace {

// Rights that are lost when a move starts from, or ends on, this square.
int rightsLostAt(int sq) {
    switch (sq) {
        case E1: return WHITE_KINGSIDE | WHITE_QUEENSIDE;
        case A1: return WHITE_QUEENSIDE;
        case H1: return WHITE_KINGSIDE;
        case E8: return BLACK_KINGSIDE | BLACK_QUEENSIDE;
        case A8: return BLACK_QUEENSIDE;
        case H8: return BLACK_KINGSIDE;
        default: return 0;
    }
}

}  // namespace

bool castlingPathSafe(const Position& pos, const Move& move) {
    if (!move.isCastle()) return true;

    const Color enemy = ~colorOf(move.piece);
    const int step = (move.flag == CASTLE_KINGSIDE) ? 1 : -1;

    // King start, transit and landing squares (e.g. e1, f1, g1).
    for (int i = 0; i <= 2; ++i) {
        if (isSquareAttacked(pos, move.from + i * step, enemy)) return false;
    }
    return true;
}

CastlingRights castlingRightsAfter(CastlingRights rights, const Move& move) {
    const int lost = rightsLostAt(move.from) | rightsLostAt(move.to);
    return static_cast<CastlingRights>(rights & ~lost);
}

int enPassantSquareAfter(const Move& move) {
    if (move.flag != DOUBLE_PAWN_PUSH) return NO_SQUARE;
    return (move.from + move.to) / 2;
}