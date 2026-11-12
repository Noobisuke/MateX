#include "special.hpp"
#include "../board/attacks.hpp"

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

CastlingRights effectiveCastlingRights(const Position& pos) {
    const CastlingRights stored = pos.castlingRights();
    int out = 0;
    if ((stored & WHITE_KINGSIDE) && pos.pieceOn(E1) == WHITE_KING && pos.pieceOn(H1) == WHITE_ROOK)
        out |= WHITE_KINGSIDE;
    if ((stored & WHITE_QUEENSIDE) && pos.pieceOn(E1) == WHITE_KING && pos.pieceOn(A1) == WHITE_ROOK)
        out |= WHITE_QUEENSIDE;
    if ((stored & BLACK_KINGSIDE) && pos.pieceOn(E8) == BLACK_KING && pos.pieceOn(H8) == BLACK_ROOK)
        out |= BLACK_KINGSIDE;
    if ((stored & BLACK_QUEENSIDE) && pos.pieceOn(E8) == BLACK_KING && pos.pieceOn(A8) == BLACK_ROOK)
        out |= BLACK_QUEENSIDE;
    return static_cast<CastlingRights>(out);
}

int enPassantSquareAfter(const Move& move) {
    if (move.flag != DOUBLE_PAWN_PUSH) return NO_SQUARE;
    return (move.from + move.to) / 2;
}
