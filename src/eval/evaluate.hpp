#pragma once
// Static evaluation (Stage 10, extended in Stage 18).
//
// evaluate() returns centipawns from the SIDE TO MOVE's point of view (negamax convention).
// evaluateTerms() returns each term as WHITE minus BLACK.
// Evaluation never returns mate scores; search owns those (Stage 11).
//
// Stage 18: every term is a (middlegame, endgame) pair and the position's game phase blends the two. The new
// features are individual flags (EvalFlags) so that each one can be measured on its own. With no flags (EVAL_BASIC)
// the result is exactly the Stage 10 evaluation: the old binary endgame switch is the special case "phase is
// either all middlegame or all endgame".

#include "../board/position.hpp"
#include <string>

namespace evalparams {
constexpr int PAWN_VALUE = 100;
constexpr int KNIGHT_VALUE = 320;
constexpr int BISHOP_VALUE = 330;
constexpr int ROOK_VALUE = 500;
constexpr int QUEEN_VALUE = 900;

constexpr int DOUBLED_PAWN = 15;   // penalty per extra pawn on a file
constexpr int ISOLATED_PAWN = 12;  // penalty per isolated pawn
constexpr int PASSED_PAWN[8] = {0, 10, 15, 25, 40, 65, 100, 0};  // by relative rank (0 = own back rank); basic eval

constexpr int BISHOP_PAIR = 30;

constexpr int MOBILITY_KNIGHT = 4;  // per safe square
constexpr int MOBILITY_BISHOP = 3;
constexpr int MOBILITY_ROOK = 2;
constexpr int MOBILITY_QUEEN = 1;

constexpr int SHIELD_NEAR = 10;   // own pawn directly in front of the king (per file, 3 files)
constexpr int SHIELD_FAR = 5;     // own pawn two ranks in front
constexpr int SHIELD_MISSING = -10;

// Stage 18: game phase. Knight and bishop count 1, rook 2, queen 4: 24 for the starting position (both sides).
constexpr int PHASE_KNIGHT = 1;
constexpr int PHASE_BISHOP = 1;
constexpr int PHASE_ROOK = 2;
constexpr int PHASE_QUEEN = 4;
constexpr int PHASE_MAX = 24;
}  // namespace evalparams

// Evaluation features. Combine with |.
enum EvalFlags : unsigned {
    EVAL_BASIC = 0,              // exactly the Stage 10 evaluation
    EVAL_TAPER = 1,              // blend middlegame/endgame scores by the amount of material left (instead of a switch)
    EVAL_MOBILITY = 2,           // mobility relative to an average piece, with separate middlegame/endgame weights
    EVAL_PASSED = 4,             // stronger passed pawns: endgame-heavy bonus, king distance, blocked / free path
    EVAL_PAWNS = 8,              // connected / phalanx pawns, backward pawns
    EVAL_OUTPOST = 16,           // knights and bishops on pawn-supported squares no enemy pawn can attack
    EVAL_ROOK = 32,              // rooks on open / half-open files and on the seventh rank
    EVAL_KINGZONE = 64,          // pressure on the squares around the king (number and kind of attackers)
    EVAL_THREATS = 128,          // pieces attacked by pawns, minors attacking rooks and queens, undefended pieces
    EVAL_SPACE = 256,            // safe central squares
    EVAL_ALL = 511,
};

// The evaluation used by search is chosen with a process-wide flag set (the engine is single threaded). The library
// default is EVAL_BASIC so that the earlier stages keep their exact behaviour; the engine and `play` select
// `evalDefault()`. The explicit-flags overloads never touch the global setting.
unsigned evalFlags();
void setEvalFlags(unsigned flags);
unsigned evalDefault();  // the recommended set, chosen from the Stage 18 measurements
bool parseEvalFlags(const std::string& text, unsigned& flags);  // "none", "all", "default", "taper,passed", "all,-space"
std::string evalFlagsToString(unsigned flags);

struct EvalTerms {
    int material = 0;
    int pst = 0;
    int pawns = 0;       // pawn structure incl. passed pawns
    int bishopPair = 0;
    int mobility = 0;
    int kingSafety = 0;  // pawn shield (+ king zone pressure with EVAL_KINGZONE)
    int outposts = 0;
    int rooks = 0;
    int threats = 0;
    int space = 0;
    int total() const { return material + pst + pawns + bishopPair + mobility + kingSafety + outposts + rooks + threats + space; }
};

int pieceValue(PieceType pt);                                  // KING -> 0
int pstValue(PieceType pt, Color c, int sq, bool endgame);     // table score for one piece (own POV)
bool isEndgame(const Position& pos);                           // binary phase flag (the Stage 10 rule)
int gamePhase(const Position& pos);                            // 0 (bare kings/pawns) .. 24 (all pieces), Stage 18
EvalTerms evaluateTerms(const Position& pos);                  // White minus Black, global flags
EvalTerms evaluateTerms(const Position& pos, unsigned flags);
int evaluate(const Position& pos);                             // side-to-move POV, global flags
int evaluate(const Position& pos, unsigned flags);