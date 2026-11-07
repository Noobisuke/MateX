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
    // Insufficient material is an automatic draw. The fifty-move rule is only a CLAIM (clock 100) until
    // the automatic seventy-five-move draw (150): the root with clock 100..149 must still pick a move, while
    // inside the tree a clock of 100 is scored as a draw (the engine assumes the side that benefits claims it).
    const int clockLimit = ply == 0 ? 150 : 100;
    if (isDeadPosition(pos) || pos.halfmoveClock() >= clockLimit) {
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

// Fail-soft alpha-beta in negamax form. Window: only scores strictly between alpha and beta are exact.
//   value >= beta : fail-high, the opponent will avoid this node (a lower bound on the true score)
//   value <= alpha: fail-low, no move here improves our best line (an upper bound on the true score)
int alphaBeta(Position& pos, int depth, int ply, int alpha, int beta, PVLine& pv, uint64_t& nodes) {
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
        const int value = -alphaBeta(pos, depth - 1, ply + 1, -beta, -alpha, childPV, nodes);
        unmakeMove(pos, move, undo);

        if (value > best) {
            best = value;
            if (value > alpha) {  // improves the window: this is (so far) the principal variation
                alpha = value;
                pv.moves[0] = move;
                std::copy(childPV.moves, childPV.moves + childPV.length, pv.moves + 1);
                pv.length = childPV.length + 1;
                if (alpha >= beta) break;  // beta cutoff
            }
        }
    }
    return best;
}

}  // namespace

static SearchResult searchImpl(Position& pos, int depth, bool useAlphaBeta);

int mateInMoves(int score) {
    if (!isMateScore(score)) return 0;
    if (score > 0) return (MATE_SCORE - score + 1) / 2;
    return -((MATE_SCORE + score + 1) / 2);
}

SearchResult search(Position& pos, int depth) { return searchImpl(pos, depth, true); }
SearchResult searchMinimax(Position& pos, int depth) { return searchImpl(pos, depth, false); }

static SearchResult searchImpl(Position& pos, int depth, bool useAlphaBeta) {
    depth = std::clamp(depth, 0, MAX_SEARCH_DEPTH);
    SearchResult result;
    result.depth = depth;

    PVLine pv;
    result.score = useAlphaBeta ? alphaBeta(pos, depth, 0, -INFINITE_SCORE, INFINITE_SCORE, pv, result.nodes)
                                : negamax(pos, depth, 0, pv, result.nodes);
    result.pv.assign(pv.moves, pv.moves + pv.length);
    if (pv.length > 0) {
        result.hasMove = true;
        result.bestMove = pv.moves[0];
    }
    return result;
}