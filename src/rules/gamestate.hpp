#pragma once
#include "repetition.hpp"
#include "../board/position.hpp"

// ---------------------------------------------------------------------
// Game state and draw rules (Stage 8)
//
// AUTOMATIC results (the game ends, nobody has to claim anything):
//   CHECKMATE                 in check, no legal move
//   STALEMATE                 not in check, no legal move
//   DEAD_POSITION             no sequence of legal moves can ever produce checkmate (FIDE 5.2.2)
//   SEVENTY_FIVE_MOVE_RULE    75 moves by each side (halfmove clock >= 150) without a pawn move or
//                             capture                                                (FIDE 9.6.2)
//   FIVEFOLD_REPETITION       same position five times                               (FIDE 9.6.1)
//
// CLAIMABLE draws (the game goes on unless a player claims; reported as flags):
//   fiftyMoveClaimable        halfmove clock >= 100                                  (FIDE 9.3)
//   threefoldClaimable        same position three times                              (FIDE 9.2)
//
// Precedence when several apply: checkmate beats everything (even a position that also reaches
// 150 halfmoves or five repetitions), then stalemate, dead position, 75-move, fivefold.
//
// Dead position here is deliberately CONSERVATIVE: no pawns, rooks or queens on the board, and
// either at most one minor piece in total (K v K, K+B v K, K+N v K) or only bishops that all stand
// on squares of one colour (any number, either side). Anything else (KNN v K, K+B v K+N,
// opposite-coloured bishops, ...) can still end in checkmate if the players cooperate, so it is
// NOT reported as dead.
// ---------------------------------------------------------------------

enum class GameResult {
    ONGOING,
    CHECKMATE,
    STALEMATE,
    DEAD_POSITION,
    SEVENTY_FIVE_MOVE_RULE,
    FIVEFOLD_REPETITION
};

const char* toString(GameResult result);

struct GameStatus {
    GameResult result = GameResult::ONGOING;
    bool inCheck = false;              // the side to move is in check
    bool fiftyMoveClaimable = false;   // only meaningful while result == ONGOING
    bool threefoldClaimable = false;   // only meaningful while result == ONGOING

    bool isOver() const { return result != GameResult::ONGOING; }
};

bool isCheckmate(const Position& pos);
bool isStalemate(const Position& pos);
bool isDeadPosition(const Position& pos);

// Clock-based rules. Both require that the side to move still has a legal move: a position
// that is checkmate (or stalemate) is decided by that, not by the clock.
bool isFiftyMoveClaimable(const Position& pos);
bool isSeventyFiveMoveDraw(const Position& pos);

// Full status of `pos`. `history` must end with `pos` (see GameHistory).
GameStatus evaluateGameStatus(const Position& pos, const GameHistory& history);