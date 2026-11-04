#pragma once
#include "types.hpp"
#include "bitboard.hpp"
#include <array>
#include <string>

// ---------------------------------------------------------------------
// Position
//
// Holds everything needed to fully describe a chess position:
//   - piece placement (bitboards + a mailbox array)
//   - side to move
//   - castling rights
//   - en passant target square
//   - halfmove clock (for 50-move rule)
//   - fullmove number
//
// Design note: we store piece placement TWICE, in two different
// representations:
//
//   1. pieceBB[12]      - one bitboard per (color, piece type), plus
//      occupancy[2]        derived "all white / all black / all
//      occupancyAll         pieces" bitboards.
//
//   2. board[64]        - a plain mailbox array: board[sq] tells you
//                          directly which Piece (or NO_PIECE) sits on
//                          a square, in O(1), no bit-scanning needed.
//
// This costs a small amount of redundancy but pays off constantly:
// bitboards are what move generation, attack detection, and
// evaluation want; the mailbox is what "what's on e4?" and debug
// printing want. We keep them in sync any time the position changes,
// and Stage 1 includes a consistency check to make sure they never
// drift apart.
// ---------------------------------------------------------------------

class Position {
public:
    Position();

    // Resets to the standard chess starting position.
    void setStartingPosition();

    // Clears everything to an empty board (no pieces, white to move,
    // no castling rights, no en passant, clocks at 0).
    void clear();

    // --- Piece placement queries -------------------------------------------------
    Piece pieceOn(int sq) const { return board_[sq]; }
    Bitboard pieces(Color c, PieceType pt) const { return pieceBB_[makePiece(c, pt)]; }
    Bitboard pieces(Piece p) const { return pieceBB_[p]; }
    Bitboard occupancy(Color c) const { return occupancyByColor_[c]; }
    Bitboard occupancyAll() const { return occupancyAll_; }

    // --- Piece placement mutation -------------------------------------------------
    // Low-level primitives. Stage 1 only needs to set up a position;
    // make/unmake move logic (Stage 5) will build on these.
    void putPiece(Piece p, int sq);
    void removePiece(int sq);

    // --- State accessors -----------------------------------------------------------
    Color sideToMove() const { return sideToMove_; }
    void setSideToMove(Color c) { sideToMove_ = c; }

    CastlingRights castlingRights() const { return castlingRights_; }
    void setCastlingRights(CastlingRights cr) { castlingRights_ = cr; }

    int enPassantSquare() const { return enPassantSquare_; }
    void setEnPassantSquare(int sq) { enPassantSquare_ = sq; }

    int halfmoveClock() const { return halfmoveClock_; }
    void setHalfmoveClock(int hc) { halfmoveClock_ = hc; }

    int fullmoveNumber() const { return fullmoveNumber_; }
    void setFullmoveNumber(int fm) { fullmoveNumber_ = fm; }

    // --- Debugging -------------------------------------------------------------
    // Human-readable 8x8 board, e.g. for printing to console.
    std::string toString() const;

    // Verifies that pieceBB_/occupancy_* and board_ describe the exact
    // same position. Returns true if consistent. Used by tests.
    bool isConsistent() const;

private:
    void recomputeOccupancy();

    std::array<Piece, 64> board_;       // mailbox: square -> piece
    std::array<Bitboard, 12> pieceBB_;  // indexed by Piece (0-11)
    std::array<Bitboard, 2> occupancyByColor_; // indexed by Color
    Bitboard occupancyAll_;

    Color sideToMove_;
    CastlingRights castlingRights_;
    int enPassantSquare_;  // NO_SQUARE if none
    int halfmoveClock_;
    int fullmoveNumber_;
};
