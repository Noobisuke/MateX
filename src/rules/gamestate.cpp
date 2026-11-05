#include "gamestate.hpp"
#include "../board/attacks.hpp"
#include "../move/legal.hpp"

namespace {

constexpr Bitboard DARK_SQUARES = 0xAA55AA55AA55AA55ULL;  // a1 is dark
constexpr Bitboard LIGHT_SQUARES = ~DARK_SQUARES;

}  // namespace

const char* toString(GameResult result) {
    switch (result) {
        case GameResult::ONGOING: return "ongoing";
        case GameResult::CHECKMATE: return "checkmate";
        case GameResult::STALEMATE: return "stalemate";
        case GameResult::DEAD_POSITION: return "draw: dead position";
        case GameResult::SEVENTY_FIVE_MOVE_RULE: return "draw: seventy-five-move rule";
        case GameResult::FIVEFOLD_REPETITION: return "draw: fivefold repetition";
    }
    return "?";
}

bool isCheckmate(const Position& pos) {
    return isInCheck(pos, pos.sideToMove()) && !hasLegalMove(pos);
}

bool isStalemate(const Position& pos) {
    return !isInCheck(pos, pos.sideToMove()) && !hasLegalMove(pos);
}

bool isDeadPosition(const Position& pos) {
    const Bitboard heavy = pos.pieces(WHITE, PAWN) | pos.pieces(BLACK, PAWN) |
                           pos.pieces(WHITE, ROOK) | pos.pieces(BLACK, ROOK) |
                           pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN);
    if (heavy != EMPTY_BB) return false;

    const Bitboard knights = pos.pieces(WHITE, KNIGHT) | pos.pieces(BLACK, KNIGHT);
    const Bitboard bishops = pos.pieces(WHITE, BISHOP) | pos.pieces(BLACK, BISHOP);

    if (popCount(knights) + popCount(bishops) <= 1) return true;  // K v K, K+minor v K

    // Only bishops, all on one square colour: no mate is possible whatever the players do.
    if (knights == EMPTY_BB) {
        return (bishops & DARK_SQUARES) == EMPTY_BB || (bishops & LIGHT_SQUARES) == EMPTY_BB;
    }
    return false;
}

bool isFiftyMoveClaimable(const Position& pos) {
    return pos.halfmoveClock() >= 100 && hasLegalMove(pos);
}

bool isSeventyFiveMoveDraw(const Position& pos) {
    return pos.halfmoveClock() >= 150 && hasLegalMove(pos);
}

GameStatus evaluateGameStatus(const Position& pos, const GameHistory& history) {
    GameStatus status;
    status.inCheck = isInCheck(pos, pos.sideToMove());

    if (!hasLegalMove(pos)) {
        status.result = status.inCheck ? GameResult::CHECKMATE : GameResult::STALEMATE;
        return status;
    }
    if (isDeadPosition(pos)) {
        status.result = GameResult::DEAD_POSITION;
        return status;
    }
    if (pos.halfmoveClock() >= 150) {
        status.result = GameResult::SEVENTY_FIVE_MOVE_RULE;
        return status;
    }

    const int seen = history.occurrences(pos);
    if (seen >= 5) {
        status.result = GameResult::FIVEFOLD_REPETITION;
        return status;
    }

    status.fiftyMoveClaimable = pos.halfmoveClock() >= 100;
    status.threefoldClaimable = seen >= 3;
    return status;
}