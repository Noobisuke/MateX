#include "repetition.hpp"
#include <algorithm>
#include "special.hpp"
#include "../board/attacks.hpp"
#include "../move/legal.hpp"
#include "../move/movegen.hpp"

int effectiveEnPassantSquare(const Position& pos) {
    const int ep = pos.enPassantSquare();
    if (ep == NO_SQUARE) return NO_SQUARE;

    const Color stm = pos.sideToMove();
    // Cheap rejection: some pawn of the side to move must attack the ep square.
    if ((pawnAttacksFrom(ep, ~stm) & pos.pieces(stm, PAWN)) == EMPTY_BB) return NO_SQUARE;

    MoveList legal;
    generateLegalMoves(pos, legal);
    for (size_t i = 0; i < legal.size(); ++i) {
        if (legal[i].flag == EN_PASSANT) return ep;
    }
    return NO_SQUARE;
}

RepetitionKey makeRepetitionKey(const Position& pos) {
    RepetitionKey key;
    for (int p = 0; p < 12; ++p) key.pieces[p] = pos.pieces(static_cast<Piece>(p));
    key.side = pos.sideToMove();
    key.castling = effectiveCastlingRights(pos);
    key.enPassant = static_cast<int8_t>(effectiveEnPassantSquare(pos));
    return key;
}

void GameHistory::reset(const Position& start) {
    keys_.clear();
    keys_.push_back(makeRepetitionKey(start));
}

void GameHistory::push(const Position& afterMove) {
    keys_.push_back(makeRepetitionKey(afterMove));
}

void GameHistory::pop() {
    if (!keys_.empty()) keys_.pop_back();
}

int GameHistory::occurrences(const Position& current) const {
    if (keys_.empty()) return 0;
    const RepetitionKey key = makeRepetitionKey(current);

    // Positions before the last pawn move or capture cannot recur: look back at most
    // halfmoveClock plies, two at a time (same side to move).
    const size_t last = keys_.size() - 1;
    const size_t reach = std::min<size_t>(static_cast<size_t>(current.halfmoveClock()), last);

    int count = 0;
    for (size_t back = 0; back <= reach; back += 2) {
        if (keys_[last - back] == key) ++count;
    }
    return count;
}