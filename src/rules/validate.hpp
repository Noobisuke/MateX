#pragma once
#include "../board/position.hpp"
#include <string>

// ---------------------------------------------------------------------
// Position validation (audit fix)
//
// loadFEN only checks FEN SYNTAX, so it happily loads positions that can
// never occur in a real game (no king, two kings, a pawn on rank 1, the
// side NOT to move already in check, an en passant square with no pawn
// behind it...). Move generation on such positions gives nonsense, e.g.
// "capturing the king" becomes a legal move. Call validatePosition on any
// position that comes from outside (FEN arguments, UCI `position`).
//
// Checked:
//   - exactly one king per side, and the kings are not adjacent
//   - no pawns on rank 1 or 8; at most 8 pawns and 16 pieces per side
//   - the side NOT to move is not in check
//   - en passant square (if set): correct rank for the side to move,
//     empty, the pawn that just double-pushed stands in front of it, and
//     the square that pawn came from is empty
// NOT checked (deliberately): castling rights vs. king/rook placement.
// Rights are stored as written and never inferred (see Stage 6).
// ---------------------------------------------------------------------

// Returns true if `pos` could arise in a legal game (as far as the checks above
// can tell). Otherwise returns false and, if `error` is non-null, says why.
bool validatePosition(const Position& pos, std::string* error = nullptr);