#include "zobrist.hpp"
#include "attacks.hpp"
#include "position.hpp"

uint64_t recomputeBaseKey(const Position& pos) {
    uint64_t key = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = pos.pieceOn(sq);
        if (p != NO_PIECE) key ^= zobrist::KEYS.piece[p][sq];
    }
    if (pos.sideToMove() == BLACK) key ^= zobrist::KEYS.side;
    key ^= zobrist::KEYS.castling[pos.castlingRights() & 15];
    return key;
}

uint64_t hashKey(const Position& pos) {
    uint64_t key = pos.baseKey();
    const int ep = pos.enPassantSquare();
    if (ep != NO_SQUARE) {
        const Color stm = pos.sideToMove();
        // A pawn of the side to move attacks the ep square exactly when it could capture en passant.
        if ((pawnAttacksFrom(ep, ~stm) & pos.pieces(stm, PAWN)) != EMPTY_BB)
            key ^= zobrist::KEYS.epFile[fileOf(ep)];
    }
    return key;
}
