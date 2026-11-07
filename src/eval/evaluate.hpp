#pragma once
// Stage 10: static evaluation.
//
// evaluate() returns centipawns from the SIDE TO MOVE's point of view (negamax convention).
// evaluateTerms() returns each term as WHITE minus BLACK.
// Evaluation never returns mate scores; search owns those (Stage 11).
// Tapered evaluation replaces the binary endgame switch in Stage 18.

#include "../board/position.hpp"

namespace evalparams {
constexpr int PAWN_VALUE = 100;
constexpr int KNIGHT_VALUE = 320;
constexpr int BISHOP_VALUE = 330;
constexpr int ROOK_VALUE = 500;
constexpr int QUEEN_VALUE = 900;

constexpr int DOUBLED_PAWN = 15;   // penalty per extra pawn on a file
constexpr int ISOLATED_PAWN = 12;  // penalty per isolated pawn
constexpr int PASSED_PAWN[8] = {0, 10, 15, 25, 40, 65, 100, 0};  // by relative rank (0 = own back rank)

constexpr int BISHOP_PAIR = 30;

constexpr int MOBILITY_KNIGHT = 4;  // per safe square
constexpr int MOBILITY_BISHOP = 3;
constexpr int MOBILITY_ROOK = 2;
constexpr int MOBILITY_QUEEN = 1;

constexpr int SHIELD_NEAR = 10;   // own pawn directly in front of the king (per file, 3 files)
constexpr int SHIELD_FAR = 5;     // own pawn two ranks in front
constexpr int SHIELD_MISSING = -10;
}  // namespace evalparams

struct EvalTerms {
    int material = 0;
    int pst = 0;
    int pawns = 0;
    int bishopPair = 0;
    int mobility = 0;
    int kingSafety = 0;
    int total() const { return material + pst + pawns + bishopPair + mobility + kingSafety; }
};

int pieceValue(PieceType pt);                                  // KING -> 0
int pstValue(PieceType pt, Color c, int sq, bool endgame);     // table score for one piece (own POV)
bool isEndgame(const Position& pos);                           // binary phase flag
EvalTerms evaluateTerms(const Position& pos);                  // White minus Black
int evaluate(const Position& pos);                             // side-to-move POV