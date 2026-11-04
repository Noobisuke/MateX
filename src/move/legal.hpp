#pragma once
#include "move.hpp"
#include "../board/position.hpp"

// ---------------------------------------------------------------------
// Legal move generation (Stage 5)
//
// A move is LEGAL if, after playing it, the mover's own king is not
// attacked. This module filters the Stage 4 pseudo-legal moves with a
// copy-make test: copy the position, apply the piece-placement effect of
// the move, ask isInCheck(mover). Simple and obviously correct; slow.
// Stage 7 (make/unmake) will replace the copy, Stage 9 perft will tell us
// whether anything faster (pin/check masks) is worth the complexity.
//
// This one rule automatically covers: pins, double check, king stepping
// onto attacked squares (including "behind" the king on a slider's ray),
// blocking/capturing a checker, and the en passant discovered check.
//
// Castling (Stage 6): castling moves must additionally pass
// castlingPathSafe() (rules/special.hpp): king not in check and not
// crossing or landing on an attacked square.
// ---------------------------------------------------------------------

// Would playing `move` (assumed pseudo-legal for pos.sideToMove()) leave
// the mover's king safe? Pure query; `pos` is not modified.
bool leavesKingSafe(const Position& pos, const Move& move);

// Generates every legal move for the side to move, appending to `moves`.
void generateLegalMoves(const Position& pos, MoveList& moves);

// True if `move` is in the legal move list of `pos` (compares from, to,
// promotion and flag, same as Move::operator==).
bool isLegalMove(const Position& pos, const Move& move);