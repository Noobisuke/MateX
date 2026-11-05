#include "legal.hpp"
#include "movegen.hpp"
#include "makemove.hpp"
#include "../board/attacks.hpp"
#include "../rules/special.hpp"

namespace {

// Is `move` safe to play on `work`? `work` is restored before returning, so it
// may be reused for the next move. Make/unmake replaces the per-move copy
// used in Stages 5-6.
bool leavesKingSafeInPlace(Position& work, const Move& move) {
    // Castling additionally may not start in check or cross an attacked square.
    if (move.isCastle() && !castlingPathSafe(work, move)) return false;

    const Color mover = work.sideToMove();
    const UndoInfo undo = makeMove(work, move);
    const bool safe = !isInCheck(work, mover);
    unmakeMove(work, move, undo);
    return safe;
}

}  // namespace

bool leavesKingSafe(const Position& pos, const Move& move) {
    Position work = pos;
    return leavesKingSafeInPlace(work, move);
}

void generateLegalMoves(const Position& pos, MoveList& moves) {
    MoveList pseudo;
    generatePseudoLegalMoves(pos, pseudo);

    Position work = pos;  // one scratch copy per call; make/unmake keeps it pristine
    for (size_t i = 0; i < pseudo.size(); ++i) {
        if (leavesKingSafeInPlace(work, pseudo[i])) {
            moves.push(pseudo[i]);
        }
    }
}

bool hasLegalMove(const Position& pos) {
    MoveList pseudo;
    generatePseudoLegalMoves(pos, pseudo);
    Position work = pos;
    for (size_t i = 0; i < pseudo.size(); ++i) {
        if (leavesKingSafeInPlace(work, pseudo[i])) return true;
    }
    return false;
}

bool isLegalMove(const Position& pos, const Move& move) {
    MoveList legal;
    generateLegalMoves(pos, legal);
    for (size_t i = 0; i < legal.size(); ++i) {
        if (legal[i] == move) return true;
    }
    return false;
}