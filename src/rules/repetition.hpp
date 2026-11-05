#pragma once
#include "../board/position.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------
// Repetition tracking (Stage 8)
//
// FIDE (Art. 9.2): two positions are the SAME when
//   - the same pieces stand on the same squares,
//   - the same side is to move,
//   - the possible moves are the same: castling rights that still exist and
//     the right to capture en passant (only if such a capture is really legal).
// The halfmove clock and the fullmove number are NOT part of the identity.
//
// A RepetitionKey is a plain, exact snapshot of exactly those things. Stage 16
// (Zobrist hashing) can later replace it with a 64-bit hash; the rule it
// encodes stays the same.
// ---------------------------------------------------------------------

struct RepetitionKey {
    std::array<Bitboard, 12> pieces{};
    Color side = WHITE;
    CastlingRights castling = 0;   // effective rights (king and rook still on their squares)
    int8_t enPassant = NO_SQUARE;  // only if a LEGAL en passant capture exists

    bool operator==(const RepetitionKey&) const = default;
};

// The en passant square if the side to move has a legal en passant capture, else NO_SQUARE.
// A stored ep square after a double push is ignored when no capture is possible (no adjacent
// pawn) or none is legal (e.g. both pawns pinned along the rank).
int effectiveEnPassantSquare(const Position& pos);

RepetitionKey makeRepetitionKey(const Position& pos);

// ---------------------------------------------------------------------
// GameHistory: the keys of every position reached in the game, oldest first.
//
//   GameHistory h;  h.reset(pos);            // the starting position counts as occurrence 1
//   makeMove(pos, m);  h.push(pos);          // after every move
//   h.pop();  unmakeMove(pos, m, undo);      // when taking a move back
//
// Kept separate from UndoInfo on purpose: UndoInfo is per-ply state restored by unmake, this is the
// game's memory. GAME rules use occurrences() with thresholds 3 (claimable) and 5 (automatic). A
// SEARCH (Stage 11+) will use the same query with a much lower threshold (a position seen once
// before is already treated as a draw there, because it can be repeated at will).
// ---------------------------------------------------------------------

class GameHistory {
public:
    void reset(const Position& start);
    void push(const Position& afterMove);
    void pop();
    size_t size() const { return keys_.size(); }

    // How many times the CURRENT position has occurred, counting itself.
    // Precondition: `current` is the position most recently reset/pushed. Only positions since
    // the last pawn move or capture are examined (earlier ones cannot recur), and only those an
    // even number of plies back (same side to move).
    int occurrences(const Position& current) const;

private:
    std::vector<RepetitionKey> keys_;
};