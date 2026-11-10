#include "makemove.hpp"
#include "../rules/special.hpp"

namespace {

// Rook squares for a castling move by `mover`.
struct RookSquares { int from; int to; };

RookSquares castlingRook(Color mover, MoveFlag flag) {
    const int rank = (mover == WHITE) ? 0 : 7;
    return flag == CASTLE_KINGSIDE ? RookSquares{squareOf(7, rank), squareOf(5, rank)}
                                   : RookSquares{squareOf(0, rank), squareOf(3, rank)};
}

// Square of the pawn captured by an en passant move landing on `to`.
int enPassantVictimSquare(Color mover, int to) {
    return mover == WHITE ? to - 8 : to + 8;
}

}  // namespace

UndoInfo makeMove(Position& pos, const Move& m) {
    const Color mover = pos.sideToMove();

    UndoInfo undo;
    undo.castling = pos.castlingRights();
    undo.enPassantSquare = pos.enPassantSquare();
    undo.halfmoveClock = pos.halfmoveClock();

    // --- Piece placement ---
    pos.removePiece(m.from);

    if (m.flag == EN_PASSANT) {
        const int victim = enPassantVictimSquare(mover, m.to);
        undo.captured = pos.pieceOn(victim);
        pos.removePiece(victim);
    } else {
        undo.captured = pos.pieceOn(m.to);  // NO_PIECE for quiet moves
        pos.removePiece(m.to);
    }

    pos.putPiece(m.isPromotion() ? makePiece(mover, m.promotion) : m.piece, m.to);

    if (m.isCastle()) {
        const RookSquares r = castlingRook(mover, m.flag);
        pos.removePiece(r.from);
        pos.putPiece(makePiece(mover, ROOK), r.to);
    }

    // --- Game state ---
    pos.setCastlingRights(castlingRightsAfter(undo.castling, m));
    pos.setEnPassantSquare(enPassantSquareAfter(m));
    pos.setHalfmoveClock((typeOf(m.piece) == PAWN || undo.captured != NO_PIECE)
                             ? 0 : undo.halfmoveClock + 1);
    if (mover == BLACK) pos.setFullmoveNumber(pos.fullmoveNumber() + 1);
    pos.setSideToMove(~mover);

    return undo;
}

void unmakeMove(Position& pos, const Move& m, const UndoInfo& undo) {
    // The side that made the move is the opposite of the side to move now.
    const Color mover = ~pos.sideToMove();

    // --- Game state ---
    pos.setSideToMove(mover);
    if (mover == BLACK) pos.setFullmoveNumber(pos.fullmoveNumber() - 1);
    pos.setHalfmoveClock(undo.halfmoveClock);
    pos.setEnPassantSquare(undo.enPassantSquare);
    pos.setCastlingRights(undo.castling);

    // --- Piece placement (exact inverse of makeMove) ---
    if (m.isCastle()) {
        const RookSquares r = castlingRook(mover, m.flag);
        pos.removePiece(r.to);
        pos.putPiece(makePiece(mover, ROOK), r.from);
    }

    pos.removePiece(m.to);       // the moved (or promoted) piece
    pos.putPiece(m.piece, m.from);  // m.piece is the original piece: a pawn for promotions

    if (m.flag == EN_PASSANT) {
        pos.putPiece(undo.captured, enPassantVictimSquare(mover, m.to));
    } else if (undo.captured != NO_PIECE) {
        pos.putPiece(undo.captured, m.to);
    }
}

NullUndo makeNullMove(Position& pos) {
    NullUndo u;
    u.enPassantSquare = pos.enPassantSquare();
    u.halfmoveClock = pos.halfmoveClock();
    u.fullmoveNumber = pos.fullmoveNumber();
    if (pos.sideToMove() == BLACK) pos.setFullmoveNumber(u.fullmoveNumber + 1);
    pos.setSideToMove(~pos.sideToMove());
    pos.setEnPassantSquare(NO_SQUARE);
    pos.setHalfmoveClock(u.halfmoveClock + 1);
    return u;
}

void unmakeNullMove(Position& pos, const NullUndo& undo) {
    pos.setSideToMove(~pos.sideToMove());
    pos.setEnPassantSquare(undo.enPassantSquare);
    pos.setHalfmoveClock(undo.halfmoveClock);
    pos.setFullmoveNumber(undo.fullmoveNumber);
}