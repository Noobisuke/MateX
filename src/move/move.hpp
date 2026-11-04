#pragma once
#include "../board/types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------
// Move representation
//
// A Move records everything needed to apply it to a Position and later
// reverse it (Stage 7), without re-deriving information from the board.
// It is a plain struct rather than a packed integer: clarity first;
// packing is a mechanical optimization we can revisit if profiling asks.
// ---------------------------------------------------------------------

enum MoveFlag : uint8_t {
    QUIET,              // normal, non-capturing move
    DOUBLE_PAWN_PUSH,   // pawn advances two squares from its start rank
    CAPTURE,            // ordinary capture
    EN_PASSANT,         // en passant capture (captured pawn isn't on `to`)
    CASTLE_KINGSIDE,
    CASTLE_QUEENSIDE,
    PROMOTION,          // quiet pawn promotion
    PROMOTION_CAPTURE   // promotion that also captures
};

struct Move {
    int8_t from = -1;
    int8_t to = -1;
    Piece piece = NO_PIECE;               // the piece that is moving
    Piece captured = NO_PIECE;            // piece captured, or NO_PIECE
    PieceType promotion = NO_PIECE_TYPE;  // promotion piece type, or NO_PIECE_TYPE
    MoveFlag flag = QUIET;

    bool isCapture() const {
        return flag == CAPTURE || flag == EN_PASSANT || flag == PROMOTION_CAPTURE;
    }
    bool isPromotion() const {
        return flag == PROMOTION || flag == PROMOTION_CAPTURE;
    }
    bool isCastle() const {
        return flag == CASTLE_KINGSIDE || flag == CASTLE_QUEENSIDE;
    }

    bool operator==(const Move& other) const {
        return from == other.from && to == other.to &&
               promotion == other.promotion && flag == other.flag;
    }

    // UCI-style coordinate notation, e.g. "e2e4", "e7e8q".
    std::string toString() const;
};

// ---------------------------------------------------------------------
// MoveList
//
// Fixed-capacity container. No reachable chess position has more than
// ~220 moves, so 256 is ample. A fixed array avoids a heap allocation
// per node, which matters once perft/search call this constantly.
// ---------------------------------------------------------------------

class MoveList {
public:
    static constexpr size_t CAPACITY = 256;

    void push(const Move& m) {
        if (count_ < CAPACITY) {
            moves_[count_++] = m;
        }
    }

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }

    Move& operator[](size_t i) { return moves_[i]; }
    const Move& operator[](size_t i) const { return moves_[i]; }

    auto begin() { return moves_.begin(); }
    auto end() { return moves_.begin() + count_; }
    auto begin() const { return moves_.begin(); }
    auto end() const { return moves_.begin() + count_; }

private:
    std::array<Move, CAPACITY> moves_{};
    size_t count_ = 0;
};