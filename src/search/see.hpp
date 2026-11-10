#pragma once
// Stage 17: Static Exchange Evaluation.
//
// Problem: a capture can look great by MVV-LVA (pawn takes... no, QUEEN takes PAWN) and still lose material because
// the pawn was defended. Searching the whole capture sequence on that square would cost a full search; SEE answers
// "what does this capture win or lose if both sides keep recapturing on the square, always with their LEAST valuable
// attacker, and either side may stop when continuing would lose?" using only bitboards, no make/unmake.
//
// see(pos, move) is the material balance (centipawns) for the side making `move`: >0 wins material, 0 an even
// trade, <0 loses material. Uses the evaluation's piece values and 20000 for the king (a king may only recapture when
// the other side has nothing left to retake, otherwise that capture would be illegal). X-ray attackers (a rook behind
// a rook, a bishop behind a queen) join once the piece in front has captured.
//
// Known simplifications, like every SEE: pins and checks are ignored (a pinned piece is still counted as an
// attacker), and promotions are valued only as the promotion gain. It is a heuristic for ordering and pruning, not a
// proof; the test suite compares it with an exhaustive exchange search and reports the agreement.
#include "../board/position.hpp"
#include "../move/move.hpp"

int see(const Position& pos, const Move& move);

// True when see(pos, move) >= threshold. (No early exit: the value is always exact for the least-valuable-attacker
// swap order, which keeps it testable against an exhaustive exchange search.)
bool seeAtLeast(const Position& pos, const Move& move, int threshold);

// Value used for a piece type inside SEE (king = 20000).
int seePieceValue(PieceType pt);