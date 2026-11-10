#pragma once
// A frozen copy of the Stage 10 evaluation (before tapering), kept only so that test_stage18 can prove that the new
// evaluation with no features enabled (EVAL_BASIC) returns exactly the same numbers. Do not "improve" this file.
#include "../src/board/attacks.hpp"
#include "../src/eval/evaluate.hpp"

namespace stage10ref {
using namespace evalparams;

inline int pieceValue(PieceType pt);
inline int pstValue(PieceType pt, Color c, int sq, bool endgame);
inline bool isEndgame(const Position& pos);


namespace {

// Half-board tables (files a-d, mirrored to e-h), rank 1 first, from the owner's point of view.
// Based on the public "simplified evaluation function" tables.
constexpr int HALF[7][32] = {
    // pawn
    {0, 0, 0, 0,   5, 10, 10, -20,   5, -5, -10, 0,   0, 0, 0, 20,
     5, 5, 10, 25,   10, 10, 20, 30,   50, 50, 50, 50,   0, 0, 0, 0},
    // knight
    {-50, -40, -30, -30,   -40, -20, 0, 5,   -30, 5, 10, 15,   -30, 0, 15, 20,
     -30, 5, 15, 20,   -30, 0, 10, 15,   -40, -20, 0, 0,   -50, -40, -30, -30},
    // bishop
    {-20, -10, -10, -10,   -10, 5, 0, 0,   -10, 10, 10, 10,   -10, 0, 10, 10,
     -10, 5, 5, 10,   -10, 0, 5, 10,   -10, 0, 0, 0,   -20, -10, -10, -10},
    // rook
    {0, 0, 0, 5,   -5, 0, 0, 0,   -5, 0, 0, 0,   -5, 0, 0, 0,
     -5, 0, 0, 0,   -5, 0, 0, 0,   5, 10, 10, 10,   0, 0, 0, 0},
    // queen
    {-20, -10, -10, -5,   -10, 0, 5, 0,   -10, 5, 5, 5,   -5, 0, 5, 5,
     -5, 0, 5, 5,   -10, 0, 5, 5,   -10, 0, 0, 0,   -20, -10, -10, -5},
    // king, middlegame
    {20, 30, 10, 0,   20, 20, 0, 0,   -10, -20, -20, -20,   -20, -30, -30, -40,
     -30, -40, -40, -50,   -30, -40, -40, -50,   -30, -40, -40, -50,   -30, -40, -40, -50},
    // king, endgame
    {-50, -40, -30, -20,   -30, -30, 0, 0,   -30, -10, 20, 30,   -30, -10, 30, 40,
     -30, -10, 30, 40,   -30, -10, 20, 30,   -30, -20, -10, 0,   -50, -30, -30, -30},
};

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = FILE_A << 7;

Bitboard fileMask(int f) { return FILE_A << f; }
Bitboard adjacentFiles(int f) {
    Bitboard m = EMPTY_BB;
    if (f > 0) m |= fileMask(f - 1);
    if (f < 7) m |= fileMask(f + 1);
    return m;
}
// All squares strictly ahead of `sq` (same file and neighbours' ranks included by caller's mask).
Bitboard ranksAhead(Color c, int sq) {
    const int r = rankOf(sq);
    if (c == WHITE) return r == 7 ? EMPTY_BB : (~0ULL << (8 * (r + 1)));
    return r == 0 ? EMPTY_BB : ((1ULL << (8 * r)) - 1);
}

Bitboard pawnAttackSet(Bitboard pawns, Color c) {
    if (c == WHITE) return ((pawns << 7) & ~FILE_H) | ((pawns << 9) & ~FILE_A);
    return ((pawns >> 7) & ~FILE_A) | ((pawns >> 9) & ~FILE_H);
}

int countMinors(const Position& pos, Color c) {
    return popCount(pos.pieces(c, KNIGHT)) + popCount(pos.pieces(c, BISHOP));
}

// Terms for one colour, from that colour's own point of view.
struct SideScore { int material = 0, pst = 0, pawns = 0, bishopPair = 0, mobility = 0, kingSafety = 0; };

SideScore scoreSide(const Position& pos, Color us, bool endgame) {
    SideScore s;
    const Color them = ~us;
    const Bitboard occ = pos.occupancyAll();
    const Bitboard own = pos.occupancy(us);
    const Bitboard ourPawns = pos.pieces(us, PAWN);
    const Bitboard theirPawns = pos.pieces(them, PAWN);
    const Bitboard unsafe = pawnAttackSet(theirPawns, them);

    for (int pt = PAWN; pt <= KING; ++pt) {
        Bitboard bb = pos.pieces(us, static_cast<PieceType>(pt));
        while (bb) {
            const int sq = popLsb(bb);
            s.material += stage10ref::pieceValue(static_cast<PieceType>(pt));
            s.pst += stage10ref::pstValue(static_cast<PieceType>(pt), us, sq, endgame);
            Bitboard att = EMPTY_BB;
            int w = 0;
            switch (pt) {
                case KNIGHT: att = knightAttacksFrom(sq); w = MOBILITY_KNIGHT; break;
                case BISHOP: att = bishopAttacksFrom(sq, occ); w = MOBILITY_BISHOP; break;
                case ROOK: att = rookAttacksFrom(sq, occ); w = MOBILITY_ROOK; break;
                case QUEEN: att = queenAttacksFrom(sq, occ); w = MOBILITY_QUEEN; break;
                default: break;
            }
            if (w) s.mobility += w * popCount(att & ~own & ~unsafe);
        }
    }

    // Pawn structure.
    for (int f = 0; f < 8; ++f) {
        const int n = popCount(ourPawns & fileMask(f));
        if (n > 1) s.pawns -= DOUBLED_PAWN * (n - 1);
    }
    Bitboard pb = ourPawns;
    while (pb) {
        const int sq = popLsb(pb);
        const int f = fileOf(sq);
        if ((ourPawns & adjacentFiles(f)) == EMPTY_BB) s.pawns -= ISOLATED_PAWN;
        const Bitboard front = ranksAhead(us, sq) & (fileMask(f) | adjacentFiles(f));
        if ((theirPawns & front) == EMPTY_BB) {
            const int rel = us == WHITE ? rankOf(sq) : 7 - rankOf(sq);
            s.pawns += PASSED_PAWN[rel];
        }
    }

    if (popCount(pos.pieces(us, BISHOP)) >= 2) s.bishopPair = BISHOP_PAIR;

    // King safety: pawn shield, middlegame only.
    const Bitboard kingBB = pos.pieces(us, KING);
    if (!endgame && kingBB != EMPTY_BB) {
        const int ksq = lsbIndex(kingBB);
        const int kf = fileOf(ksq), kr = rankOf(ksq);
        const int dir = us == WHITE ? 1 : -1;
        const int r1 = kr + dir, r2 = kr + 2 * dir;
        if (r1 >= 0 && r1 <= 7) {
            for (int f = kf - 1; f <= kf + 1; ++f) {
                if (f < 0 || f > 7) continue;
                if (testBit(ourPawns, squareOf(f, r1))) s.kingSafety += SHIELD_NEAR;
                else if (r2 >= 0 && r2 <= 7 && testBit(ourPawns, squareOf(f, r2))) s.kingSafety += SHIELD_FAR;
                else s.kingSafety += SHIELD_MISSING;
            }
        }
    }
    return s;
}

}  // namespace

inline int pieceValue(PieceType pt) {
    switch (pt) {
        case PAWN: return PAWN_VALUE;
        case KNIGHT: return KNIGHT_VALUE;
        case BISHOP: return BISHOP_VALUE;
        case ROOK: return ROOK_VALUE;
        case QUEEN: return QUEEN_VALUE;
        default: return 0;
    }
}

inline int pstValue(PieceType pt, Color c, int sq, bool endgame) {
    const int s = c == WHITE ? sq : (sq ^ 56);
    const int f = fileOf(s);
    const int idx = rankOf(s) * 4 + (f < 4 ? f : 7 - f);
    const int table = (pt == KING && endgame) ? 6 : static_cast<int>(pt);
    return HALF[table][idx];
}

inline bool isEndgame(const Position& pos) {
    for (int c = 0; c < 2; ++c) {
        const Color col = static_cast<Color>(c);
        if (pos.pieces(col, QUEEN) == EMPTY_BB) continue;
        if (pos.pieces(col, ROOK) != EMPTY_BB || countMinors(pos, col) > 1) return false;
    }
    return true;
}

inline EvalTerms evaluateTerms(const Position& pos) {
    const bool eg = stage10ref::isEndgame(pos);
    const SideScore w = scoreSide(pos, WHITE, eg);
    const SideScore b = scoreSide(pos, BLACK, eg);
    EvalTerms t;
    t.material = w.material - b.material;
    t.pst = w.pst - b.pst;
    t.pawns = w.pawns - b.pawns;
    t.bishopPair = w.bishopPair - b.bishopPair;
    t.mobility = w.mobility - b.mobility;
    t.kingSafety = w.kingSafety - b.kingSafety;
    return t;
}

inline int evaluate(const Position& pos) {
    const int white = stage10ref::evaluateTerms(pos).total();
    return pos.sideToMove() == WHITE ? white : -white;
}

}  // namespace stage10ref