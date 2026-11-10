#pragma once
#include "move.hpp"
#include "../board/position.hpp"

// ---------------------------------------------------------------------
// Make / unmake (Stage 7)
//
// makeMove applies a move to a Position in place and returns an UndoInfo
// holding exactly what cannot be re-derived from the move itself. The
// caller keeps the UndoInfo (typically a local variable, one per ply) and
// hands it back to unmakeMove, which restores the EXACT previous position.
//
// Invariant (tested on many positions and every legal move):
//     Position before = pos;
//     UndoInfo u = makeMove(pos, m);
//     unmakeMove(pos, m, u);
//     pos == before
//
// Preconditions: `m` was generated for `pos` (pseudo-legal for the side to
// move). Neither function checks legality; use generateLegalMoves for that.
// Unmake must be given the same move and UndoInfo, in reverse (LIFO) order.
// ---------------------------------------------------------------------

struct UndoInfo {
    Piece captured = NO_PIECE;        // piece removed by the move (incl. e.p. victim)
    CastlingRights castling = 0;      // rights before the move
    int enPassantSquare = NO_SQUARE;  // ep square before the move
    int halfmoveClock = 0;            // halfmove clock before the move
};

UndoInfo makeMove(Position& pos, const Move& move);
void unmakeMove(Position& pos, const Move& move, const UndoInfo& undo);

// ---------------------------------------------------------------------
// Null move (Stage 17): "pass the turn". Only the search uses it (null-move pruning); it is not a chess move.
// The side to move flips, the en passant square is cleared and the halfmove clock advances (as after a quiet
// move); placement and castling rights are untouched. The Zobrist base key follows through setSideToMove.
// Precondition: the side to move is NOT in check (the opponent could otherwise capture the king).
// Unmake restores every field exactly: Position before = pos; auto u = makeNullMove(pos); unmakeNullMove(pos, u);
// leaves pos == before.
// ---------------------------------------------------------------------
struct NullUndo {
    int enPassantSquare = NO_SQUARE;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
};

NullUndo makeNullMove(Position& pos);
void unmakeNullMove(Position& pos, const NullUndo& undo);