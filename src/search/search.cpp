#include "search.hpp"
#include "../board/attacks.hpp"
#include "../eval/evaluate.hpp"
#include "../move/legal.hpp"
#include "../move/makemove.hpp"
#include "../rules/gamestate.hpp"
#include <algorithm>

namespace {

struct PVLine {
    Move moves[MAX_SEARCH_DEPTH + 1];
    int length = 0;
};

// Terminal or drawn position test that is independent of depth. Returns true and sets `score`.
bool terminalScore(const Position& pos, int ply, int& score) {
    if (!hasLegalMove(pos)) {
        score = isInCheck(pos, pos.sideToMove()) ? -(MATE_SCORE - ply) : 0;  // mated, or stalemate
        return true;
    }
    if (isDeadPosition(pos) || pos.halfmoveClock() >= 100) {  // insufficient material / fifty-move rule
        score = 0;
        return true;
    }
    return false;
}

int negamax(Position& pos, int depth, int ply, PVLine& pv, uint64_t& nodes) {
    ++nodes;
    pv.length = 0;

    int score = 0;
    if (terminalScore(pos, ply, score)) return score;
    if (depth == 0) return evaluate(pos);

    MoveList moves;
    generateLegalMoves(pos, moves);

    int best = -INFINITE_SCORE;
    PVLine childPV;
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        const UndoInfo undo = makeMove(pos, move);
        const int value = -negamax(pos, depth - 1, ply + 1, childPV, nodes);
        unmakeMove(pos, move, undo);

        if (value > best) {  // strict: ties keep the earliest move, so results are deterministic
            best = value;
            pv.moves[0] = move;
            std::copy(childPV.moves, childPV.moves + childPV.length, pv.moves + 1);
            pv.length = childPV.length + 1;
        }
    }
    return best;
}

}  // namespace

int mateInMoves(int score) {
    if (!isMateScore(score)) return 0;
    if (score > 0) return (MATE_SCORE - score + 1) / 2;
    return -((MATE_SCORE + score + 1) / 2);
}

SearchResult search(Position& pos, int depth) {
    depth = std::clamp(depth, 0, MAX_SEARCH_DEPTH);
    SearchResult result;
    result.depth = depth;

    PVLine pv;
    result.score = negamax(pos, depth, 0, pv, result.nodes);
    result.pv.assign(pv.moves, pv.moves + pv.length);
    if (pv.length > 0) {
        result.hasMove = true;
        result.bestMove = pv.moves[0];
    }
    return result;
}