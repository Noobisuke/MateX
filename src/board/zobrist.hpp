#pragma once
#include <cstdint>

// ---------------------------------------------------------------------
// Zobrist hashing (Stage 16)
//
// A 64-bit fingerprint of a position: the XOR of one random key per "feature" that is true.
//   - one key for every (piece, square) pair that is occupied      (12 x 64)
//   - one key if Black is to move
//   - one key per castling-rights mask (the mask is 4 bits)         (16, mask 0 has key 0)
//   - one key per en passant FILE, used only when an ep capture is possible (8)
// Because XOR undoes itself, moving a piece is "XOR out the old feature, XOR in the new one".
// Position keeps the part that depends only on placement, side and castling up to date inside putPiece,
// removePiece, setSideToMove and setCastlingRights, so makeMove/unmakeMove need no changes at all.
//
// The en passant part is added on demand by hashKey(), because whether it counts depends on the pawns:
// two positions that differ only by an ep square nobody can capture on are the SAME position and must hash
// equal (otherwise transpositions are missed). See hashKey() below for the exact rule.
//
// The keys are produced at COMPILE time by splitmix64 from a fixed seed: no static-initialisation order
// problems, and the same position hashes to the same value on every run and every platform.
// ---------------------------------------------------------------------

class Position;

namespace zobrist {

struct Keys {
    uint64_t piece[12][64];
    uint64_t side;         // XOR-ed in when Black is to move
    uint64_t castling[16]; // indexed by the 4-bit rights mask; castling[0] == 0
    uint64_t epFile[8];
};

constexpr uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr Keys makeKeys() {
    Keys k{};
    uint64_t state = 0x4D6174655821ULL;  // fixed seed ("MateX!")
    for (int p = 0; p < 12; ++p)
        for (int sq = 0; sq < 64; ++sq) k.piece[p][sq] = splitmix64(state);
    k.side = splitmix64(state);
    k.castling[0] = 0;
    for (int i = 1; i < 16; ++i) k.castling[i] = splitmix64(state);
    for (int f = 0; f < 8; ++f) k.epFile[f] = splitmix64(state);
    return k;
}

inline constexpr Keys KEYS = makeKeys();

}  // namespace zobrist

// Placement + side + castling key recomputed from scratch (slow; for tests and Position::isConsistent()).
// Always equals Position::baseKey() for a consistent position.
uint64_t recomputeBaseKey(const Position& pos);

// The full position hash used by the transposition table and (later) repetition detection:
//   baseKey()  XOR  epFile[file of the ep square]   when the side to move has a pawn that attacks the ep square.
// The condition is the cheap "a capture is pseudo-legally possible" test, NOT the full legality test of Stage 8.
// Consequence: in the rare case where the only ep capture is illegal (pinned pawn) two FIDE-identical positions
// can get different keys. That only costs a missed match, never a wrong one.
// The halfmove clock and fullmove number are not part of the key.
uint64_t hashKey(const Position& pos);
