#include "legal.hpp"
#include "movegen.hpp"
#include "../board/attacks.hpp"

namespace {

// Applies only the PIECE PLACEMENT effect of `m` to `p` (a scratch copy).
// Side to move, castling rights, en passant square and clocks are left
// alone: none of them influence whether a king is attacked, and updating
// them correctly is the job of the real make/unmake in Stage 7.
void applyPlacement(Position& p, const Move& m, Color mover) {
    const int from = m.from;
    const int to = m.to;

    p.removePiece(from);

    if (m.flag == EN_PASSANT) {
        // The captured pawn is not on `to`; it sits one rank behind it
        // (from the mover's point of view).
        const int capturedSq = (mover == WHITE) ? to - 8 : to + 8;
        p.removePiece(capturedSq);
    }

    if (m.isPromotion()) {
        p.putPiece(makePiece(mover, m.promotion), to);  // putPiece overwrites a captured piece
    } else {
        p.putPiece(m.piece, to);
    }

    if (m.flag == CASTLE_KINGSIDE) {
        const int rank = (mover == WHITE) ? 0 : 7;
        p.removePiece(squareOf(7, rank));
        p.putPiece(makePiece(mover, ROOK), squareOf(5, rank));
    } else if (m.flag == CASTLE_QUEENSIDE) {
        const int rank = (mover == WHITE) ? 0 : 7;
        p.removePiece(squareOf(0, rank));
        p.putPiece(makePiece(mover, ROOK), squareOf(3, rank));
    }
}

}  // namespace

bool leavesKingSafe(const Position& pos, const Move& move) {
    const Color mover = pos.sideToMove();
    Position scratch = pos;  // copy-make: Position is a few hundred bytes
    applyPlacement(scratch, move, mover);
    return !isInCheck(scratch, mover);
}

void generateLegalMoves(const Position& pos, MoveList& moves) {
    MoveList pseudo;
    generatePseudoLegalMoves(pos, pseudo);
    for (size_t i = 0; i < pseudo.size(); ++i) {
        if (leavesKingSafe(pos, pseudo[i])) {
            moves.push(pseudo[i]);
        }
    }
}

bool isLegalMove(const Position& pos, const Move& move) {
    MoveList legal;
    generateLegalMoves(pos, legal);
    for (size_t i = 0; i < legal.size(); ++i) {
        if (legal[i] == move) return true;
    }
    return false;
}