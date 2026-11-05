#pragma once
#include "../board/position.hpp"
#include "../move/move.hpp"

// ---------------------------------------------------------------------
// Special rules (Stage 6)
//
// Pure helpers for the rules that go beyond "does the king stay safe":
// castling safety, castling-right bookkeeping and the en passant target
// square. They do not modify a Position; Stage 7's make/unmake will call
// the two "...After" functions to update state.
// ---------------------------------------------------------------------

// Castling SAFETY (the part Stage 4's structural generator leaves out).
// For a CASTLE_KINGSIDE / CASTLE_QUEENSIDE move, true iff the king's
//   - starting square  (not castling out of check),
//   - transit square   (f1/f8 or d1/d8, not castling through attack),
//   - landing square   (g1/g8 or c1/c8)
// are all unattacked by the opponent. Attacks on the ROOK, and on b1/b8
// (queenside), do not matter. Returns true for non-castling moves.
// Assumes the structural conditions already hold (Stage 4).
bool castlingPathSafe(const Position& pos, const Move& move);

// Castling rights remaining after `move` is played. Clears rights when a
// king moves/castles, a rook leaves its corner, or anything lands on a
// rook's corner (rook captured, including by a promoting pawn).
// Rights are tracked explicitly, never inferred from piece placement.
CastlingRights castlingRightsAfter(CastlingRights rights, const Move& move);

// Castling rights that can really still be exercised structurally: the stored right AND the
// king on e1/e8 AND the rook on its corner. Stored rights may be stale for positions loaded
// from a hand-written FEN. Used for repetition (Stage 8); temporary obstacles (pieces in the
// way, attacked squares) do NOT remove a right here, matching the FIDE definition.
CastlingRights effectiveCastlingRights(const Position& pos);

// En passant target square after `move`: the skipped square for a double
// pawn push, NO_SQUARE for every other move. Set after EVERY double push
// (FEN-specification convention), whether or not a capture is possible.
int enPassantSquareAfter(const Move& move);