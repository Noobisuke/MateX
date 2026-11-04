#pragma once
#include "position.hpp"
#include <string>

// ---------------------------------------------------------------------
// FEN (Forsyth-Edwards Notation)
//
// A single-line, 6-field text format that fully describes a chess
// position. See position.hpp for how each field maps onto our
// Position class's state.
//
// Design note: these are free functions, not Position methods.
// Position's job (Stage 1) is to *be* a position; FEN parsing/
// generation is a separate serialization concern layered on top.
// ---------------------------------------------------------------------

// Parses a FEN string into `pos`. On success, returns true and `pos`
// is fully populated (previous contents are discarded). On failure,
// returns false, `pos` is left cleared (safe/empty), and if
// `errorOut` is non-null it is set to a human-readable reason.
//
// NOTE on the en-passant field: this parser records a double-pawn-push
// target square exactly as written in the FEN, without checking
// whether an en-passant capture is actually legally available (that
// requires check/pin detection, introduced in later stages). This
// matches the traditional/simple FEN interpretation.
bool loadFEN(Position& pos, const std::string& fen, std::string* errorOut = nullptr);

// Generates the FEN string for the given position. Always produces
// well-formed output for any valid Position, so:
//   toFEN(pos-after-loadFEN(fen)) == fen   (for canonical FEN input)
std::string toFEN(const Position& pos);
