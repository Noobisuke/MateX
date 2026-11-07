#include "perft.hpp"
#include "../board/attacks.hpp"
#include "../move/legal.hpp"
#include "../move/makemove.hpp"

uint64_t perft(Position& pos, int depth) {
    if (depth <= 0) return 1;

    MoveList moves;
    generateLegalMoves(pos, moves);

    uint64_t nodes = 0;
    for (size_t i = 0; i < moves.size(); ++i) {
        const UndoInfo undo = makeMove(pos, moves[i]);
        nodes += perft(pos, depth - 1);
        unmakeMove(pos, moves[i], undo);
    }
    return nodes;
}

uint64_t perftBulk(Position& pos, int depth) {
    if (depth <= 0) return 1;

    MoveList moves;
    generateLegalMoves(pos, moves);
    if (depth == 1) return moves.size();

    uint64_t nodes = 0;
    for (size_t i = 0; i < moves.size(); ++i) {
        const UndoInfo undo = makeMove(pos, moves[i]);
        nodes += perftBulk(pos, depth - 1);
        unmakeMove(pos, moves[i], undo);
    }
    return nodes;
}

std::vector<DivideEntry> divide(Position& pos, int depth) {
    std::vector<DivideEntry> result;
    if (depth < 1) return result;

    MoveList moves;
    generateLegalMoves(pos, moves);
    for (size_t i = 0; i < moves.size(); ++i) {
        const UndoInfo undo = makeMove(pos, moves[i]);
        result.push_back({moves[i], perft(pos, depth - 1)});
        unmakeMove(pos, moves[i], undo);
    }
    return result;
}

PerftStats& PerftStats::operator+=(const PerftStats& o) {
    nodes += o.nodes;
    captures += o.captures;
    enPassant += o.enPassant;
    castles += o.castles;
    promotions += o.promotions;
    checks += o.checks;
    checkmates += o.checkmates;
    return *this;
}

PerftStats perftDetailed(Position& pos, int depth) {
    PerftStats stats;
    if (depth <= 0) {
        stats.nodes = 1;
        return stats;
    }

    MoveList moves;
    generateLegalMoves(pos, moves);
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move& m = moves[i];
        const UndoInfo undo = makeMove(pos, m);
        if (depth == 1) {
            ++stats.nodes;
            if (m.isCapture()) ++stats.captures;
            if (m.flag == EN_PASSANT) ++stats.enPassant;
            if (m.isCastle()) ++stats.castles;
            if (m.isPromotion()) ++stats.promotions;
            if (isInCheck(pos, pos.sideToMove())) {
                ++stats.checks;
                if (!hasLegalMove(pos)) ++stats.checkmates;
            }
        } else {
            stats += perftDetailed(pos, depth - 1);
        }
        unmakeMove(pos, m, undo);
    }
    return stats;
}