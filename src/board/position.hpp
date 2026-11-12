#pragma once
#include "types.hpp"
#include "bitboard.hpp"
#include "zobrist.hpp"
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
// Piece placement is stored TWICE, in two representations:
//   1. pieceBB_[12] + occupancy bitboards (what move generation,
//      attack detection and evaluation want)
//   2. board_[64] mailbox (what "what's on e4?" and printing want)
// Both are updated together; isConsistent() verifies they agree.
// ---------------------------------------------------------------------

class Position {
public:
    Position();

    // Resets to the standard chess starting position.
    void setStartingPosition();

    // Clears everything to an empty board (no pieces, white to move,
    // no castling rights, no en passant, clocks at 0/1).
    void clear();

    // --- Piece placement queries ---
    Piece pieceOn(int sq) const { return board_[sq]; }
    Bitboard pieces(Color c, PieceType pt) const { return pieceBB_[makePiece(c, pt)]; }
    Bitboard pieces(Piece p) const { return pieceBB_[p]; }
    Bitboard occupancy(Color c) const { return occupancyByColor_[c]; }
    Bitboard occupancyAll() const { return occupancyAll_; }

    // --- Piece placement mutation ---
    // Inline since Stage 20: make/unmake call these four times per move.
    void putPiece(Piece p, int sq) {
        // NO_PIECE (12) is not a valid index into pieceBB_/occupancyByColor_; placing "no piece"
        // means emptying the square. (Audit fix: this used to write out of bounds.)
        if (p == NO_PIECE) {
            removePiece(sq);
            return;
        }
        // Defensive: if a piece already sits here, remove it first so the
        // bitboards/mailbox never get out of sync.
        if (board_[sq] != NO_PIECE) {
            removePiece(sq);
        }
        board_[sq] = p;
        setBit(pieceBB_[p], sq);
        // Incremental occupancy update (Stage 7): touch one bit instead of rebuilding.
        setBit(occupancyByColor_[colorOf(p)], sq);
        setBit(occupancyAll_, sq);
        baseKey_ ^= zobrist::KEYS.piece[p][sq];
    }
    void removePiece(int sq) {
        Piece p = board_[sq];
        if (p == NO_PIECE) return;
        board_[sq] = NO_PIECE;
        clearBit(pieceBB_[p], sq);
        clearBit(occupancyByColor_[colorOf(p)], sq);
        clearBit(occupancyAll_, sq);
        baseKey_ ^= zobrist::KEYS.piece[p][sq];
    }

    // --- State accessors ---
    Color sideToMove() const { return sideToMove_; }
    void setSideToMove(Color c) {
        if ((c == BLACK) != (sideToMove_ == BLACK)) baseKey_ ^= zobrist::KEYS.side;
        sideToMove_ = c;
    }

    CastlingRights castlingRights() const { return castlingRights_; }
    void setCastlingRights(CastlingRights cr) {
        baseKey_ ^= zobrist::KEYS.castling[castlingRights_ & 15] ^ zobrist::KEYS.castling[cr & 15];
        castlingRights_ = cr;
    }

    int enPassantSquare() const { return enPassantSquare_; }
    void setEnPassantSquare(int sq) { enPassantSquare_ = sq; }

    int halfmoveClock() const { return halfmoveClock_; }
    void setHalfmoveClock(int hc) { halfmoveClock_ = hc; }

    int fullmoveNumber() const { return fullmoveNumber_; }
    void setFullmoveNumber(int fm) { fullmoveNumber_ = fm; }

    // --- Zobrist key (Stage 16) ---
    // Incrementally maintained hash of placement + side to move + castling rights (NOT en passant).
    // Use hashKey(pos) from zobrist.hpp for the full key including en passant.
    uint64_t baseKey() const { return baseKey_; }

    // Exact equality of every field (placement, side, rights, ep, clocks).
    // Used by tests to prove make/unmake restores the previous position.
    bool operator==(const Position&) const = default;

    // --- Debugging ---
    std::string toString() const;
    bool isConsistent() const;

private:
    std::array<Piece, 64> board_;
    std::array<Bitboard, 12> pieceBB_;
    std::array<Bitboard, 2> occupancyByColor_;
    Bitboard occupancyAll_;

    Color sideToMove_;
    CastlingRights castlingRights_;
    int enPassantSquare_;  // NO_SQUARE if none
    int halfmoveClock_;
    int fullmoveNumber_;

    uint64_t baseKey_;  // see baseKey(); 0 for the empty board with White to move and no castling rights
};