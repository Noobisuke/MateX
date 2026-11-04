#pragma once
#include "move.hpp"
#include "../board/position.hpp"

// ---------------------------------------------------------------------
// Pseudo-legal move generation
//
// Generates every move that follows each piece's movement rules and
// doesn't land on a friendly piece. It does NOT filter out moves that
// leave the mover's own king in check - that is Stage 5.
//
// Castling note: only the *structural* conditions are checked here
// (right still held, king and rook on their home squares, squares
// between them empty). "King not in check / doesn't cross or land on
// an attacked square" belongs to Stage 6. Until then, generated
// castling moves are NOT guaranteed fully legal.
//
// Requires initAttackTables() (or any attack helper, which lazily
// initializes) to have been usable; the helpers self-initialize.
// ---------------------------------------------------------------------

// Generates all pseudo-legal moves for the side to move, appending to `moves`.
void generatePseudoLegalMoves(const Position& pos, MoveList& moves);

// Per-piece generators, exposed for unit tests and later reuse
// (e.g. quiescence search will want captures-only generation).
void generatePawnMoves(const Position& pos, MoveList& moves);
void generateKnightMoves(const Position& pos, MoveList& moves);
void generateBishopMoves(const Position& pos, MoveList& moves);
void generateRookMoves(const Position& pos, MoveList& moves);
void generateQueenMoves(const Position& pos, MoveList& moves);
void generateKingMoves(const Position& pos, MoveList& moves);  // includes castling