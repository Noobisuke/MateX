#include "evaluate.hpp"
#include "../board/attacks.hpp"
#include <algorithm>
#include <cstdlib>

using namespace evalparams;

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

// ---- Stage 18 parameters (centipawns), indexed by relative rank where it says so (0 = the owner's back rank) ----
constexpr int PASSED_MG[8] = {0, 5, 10, 20, 35, 60, 100, 0};
constexpr int PASSED_EG[8] = {0, 10, 20, 35, 60, 100, 160, 0};
constexpr int CONNECTED[8] = {0, 0, 2, 3, 5, 9, 15, 0};  // per "supported" and per "phalanx" relationship
constexpr int BACKWARD_MG = 9, BACKWARD_EG = 12;
constexpr int OUTPOST_KNIGHT_MG = 25, OUTPOST_KNIGHT_EG = 15;
constexpr int OUTPOST_BISHOP_MG = 12, OUTPOST_BISHOP_EG = 8;
constexpr int ROOK_OPEN_MG = 18, ROOK_OPEN_EG = 8;
constexpr int ROOK_HALF_MG = 9, ROOK_HALF_EG = 6;
constexpr int ROOK_SEVENTH_MG = 12, ROOK_SEVENTH_EG = 20;
constexpr int ATTACK_WEIGHT[6] = {0, 2, 2, 3, 5, 0};  // indexed by PieceType (pawn, knight, bishop, rook, queen, king)
constexpr int KINGZONE_MAX = 250;
constexpr int THREAT_PAWN_MG = 28, THREAT_PAWN_EG = 18;      // a piece attacked by a pawn
constexpr int THREAT_MINOR_MG = 14, THREAT_MINOR_EG = 10;    // a rook or queen attacked by a minor piece
constexpr int THREAT_ROOK_QUEEN_MG = 12, THREAT_ROOK_QUEEN_EG = 8;  // a queen attacked by a rook
constexpr int HANGING_MG = 10, HANGING_EG = 8;               // attacked and undefended
// Mobility with EVAL_MOBILITY: bonus = weight * (safe squares - average for the piece).
constexpr int MOB_BASE[6] = {0, 4, 5, 6, 9, 0};  // indexed by PieceType
constexpr int MOB_MG[6] = {0, 4, 4, 2, 1, 0};
constexpr int MOB_EG[6] = {0, 4, 5, 4, 2, 0};

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = FILE_A << 7;
constexpr Bitboard CENTER_FILES = (FILE_A << 2) | (FILE_A << 3) | (FILE_A << 4) | (FILE_A << 5);

Bitboard fileMask(int f) { return FILE_A << f; }
Bitboard adjacentFiles(int f) {
    Bitboard m = EMPTY_BB;
    if (f > 0) m |= fileMask(f - 1);
    if (f < 7) m |= fileMask(f + 1);
    return m;
}
// All squares on ranks strictly ahead of `sq` for colour c (file masks are applied by the caller).
Bitboard ranksAhead(Color c, int sq) {
    const int r = rankOf(sq);
    if (c == WHITE) return r == 7 ? EMPTY_BB : (~0ULL << (8 * (r + 1)));
    return r == 0 ? EMPTY_BB : ((1ULL << (8 * r)) - 1);
}
Bitboard rankMask(int r) { return 0xFFULL << (8 * r); }
// Relative ranks 1..3 (the second to the fourth rank from the owner's side).
Bitboard ownHalfRanks(Color c) { return c == WHITE ? (rankMask(1) | rankMask(2) | rankMask(3)) : (rankMask(6) | rankMask(5) | rankMask(4)); }

Bitboard pawnAttackSet(Bitboard pawns, Color c) {
    if (c == WHITE) return ((pawns << 7) & ~FILE_H) | ((pawns << 9) & ~FILE_A);
    return ((pawns >> 7) & ~FILE_A) | ((pawns >> 9) & ~FILE_H);
}

int countMinors(const Position& pos, Color c) {
    return popCount(pos.pieces(c, KNIGHT)) + popCount(pos.pieces(c, BISHOP));
}

int chebyshev(int a, int b) { return std::max(std::abs(fileOf(a) - fileOf(b)), std::abs(rankOf(a) - rankOf(b))); }

struct Pair {
    int mg = 0, eg = 0;
    void add(int m, int e) { mg += m; eg += e; }
    void add(int both) { mg += both; eg += both; }
};

// Terms for one colour, from that colour's own point of view, as (middlegame, endgame) pairs.
struct SideScore {
    Pair material, pst, pawns, bishopPair, mobility, kingSafety, outposts, rooks, threats, space;
};

// Attack information about one colour, gathered first for both colours because the finishing pass needs both.
struct SideInfo {
    Bitboard pawnAtt = EMPTY_BB, minorAtt = EMPTY_BB, rookAtt = EMPTY_BB, queenAtt = EMPTY_BB, allAtt = EMPTY_BB;
    int attackers = 0;  // our pieces that attack the squares around the ENEMY king
    int units = 0;      // their weighted strength plus the number of attacked squares
};

constexpr int NO_KING = -1;
int kingSquare(const Position& pos, Color c) {
    const Bitboard k = pos.pieces(c, KING);
    return k == EMPTY_BB ? NO_KING : lsbIndex(k);
}

// First pass for colour `us`: material, piece-square, mobility, outposts, rook placement and the attack sets.
void gather(const Position& pos, Color us, unsigned flags, SideScore& s, SideInfo& info) {
    const Color them = ~us;
    const Bitboard occ = pos.occupancyAll();
    const Bitboard own = pos.occupancy(us);
    const Bitboard ourPawns = pos.pieces(us, PAWN);
    const Bitboard theirPawns = pos.pieces(them, PAWN);
    const Bitboard unsafe = pawnAttackSet(theirPawns, them);
    const Bitboard ourPawnAtt = pawnAttackSet(ourPawns, us);
    info.pawnAtt = ourPawnAtt;
    info.allAtt = ourPawnAtt;
    const int theirKing = kingSquare(pos, them);
    const Bitboard zone = theirKing == NO_KING ? EMPTY_BB : (kingAttacksFrom(theirKing) | (1ULL << theirKing));

    for (int pt = PAWN; pt <= KING; ++pt) {
        Bitboard bb = pos.pieces(us, static_cast<PieceType>(pt));
        while (bb) {
            const int sq = popLsb(bb);
            s.material.add(pieceValue(static_cast<PieceType>(pt)));
            // Both columns come from the tables; only the king has a different endgame table.
            s.pst.add(pstValue(static_cast<PieceType>(pt), us, sq, false), pstValue(static_cast<PieceType>(pt), us, sq, true));
            Bitboard att = EMPTY_BB;
            switch (pt) {
                case KNIGHT: att = knightAttacksFrom(sq); info.minorAtt |= att; break;
                case BISHOP: att = bishopAttacksFrom(sq, occ); info.minorAtt |= att; break;
                case ROOK: att = rookAttacksFrom(sq, occ); info.rookAtt |= att; break;
                case QUEEN: att = queenAttacksFrom(sq, occ); info.queenAtt |= att; break;
                case KING: att = kingAttacksFrom(sq); break;
                default: break;
            }
            info.allAtt |= att;
            if (pt == PAWN || pt == KING) continue;

            // Mobility: squares not occupied by our own pieces and not attacked by enemy pawns.
            const int safe = popCount(att & ~own & ~unsafe);
            if (flags & EVAL_MOBILITY) {
                s.mobility.add(MOB_MG[pt] * (safe - MOB_BASE[pt]), MOB_EG[pt] * (safe - MOB_BASE[pt]));
            } else {
                const int w = pt == KNIGHT ? MOBILITY_KNIGHT : pt == BISHOP ? MOBILITY_BISHOP : pt == ROOK ? MOBILITY_ROOK : MOBILITY_QUEEN;
                s.mobility.add(w * safe);
            }

            if (flags & EVAL_KINGZONE) {
                const int hit = popCount(att & zone);
                if (hit > 0) {
                    ++info.attackers;
                    info.units += ATTACK_WEIGHT[pt] + hit;
                }
            }

            const int rel = us == WHITE ? rankOf(sq) : 7 - rankOf(sq);
            const int f = fileOf(sq);
            if ((flags & EVAL_OUTPOST) && (pt == KNIGHT || pt == BISHOP) && rel >= 3 && rel <= 5 && testBit(ourPawnAtt, sq) &&
                (theirPawns & adjacentFiles(f) & ranksAhead(us, sq)) == EMPTY_BB) {
                if (pt == KNIGHT) s.outposts.add(OUTPOST_KNIGHT_MG, OUTPOST_KNIGHT_EG);
                else s.outposts.add(OUTPOST_BISHOP_MG, OUTPOST_BISHOP_EG);
            }

            if ((flags & EVAL_ROOK) && pt == ROOK) {
                const Bitboard file = fileMask(f);
                if ((ourPawns & file) == EMPTY_BB) {
                    if ((theirPawns & file) == EMPTY_BB) s.rooks.add(ROOK_OPEN_MG, ROOK_OPEN_EG);
                    else s.rooks.add(ROOK_HALF_MG, ROOK_HALF_EG);
                }
                if (rel == 6 && ((theirPawns & rankMask(rankOf(sq))) != EMPTY_BB || (theirKing != NO_KING && (us == WHITE ? rankOf(theirKing) == 7 : rankOf(theirKing) == 0))))
                    s.rooks.add(ROOK_SEVENTH_MG, ROOK_SEVENTH_EG);
            }
        }
    }
}

// Second pass: terms that need both sides' attack sets.
void finish(const Position& pos, Color us, unsigned flags, SideScore& s, const SideInfo& mine, const SideInfo& theirs) {
    const Color them = ~us;
    const Bitboard occ = pos.occupancyAll();
    const Bitboard ourPawns = pos.pieces(us, PAWN);
    const Bitboard theirPawns = pos.pieces(them, PAWN);
    const Bitboard theirPawnAtt = theirs.pawnAtt;
    const int ourKing = kingSquare(pos, us);
    const int theirKing = kingSquare(pos, them);

    // Doubled pawns.
    for (int f = 0; f < 8; ++f) {
        const int n = popCount(ourPawns & fileMask(f));
        if (n > 1) s.pawns.add(-DOUBLED_PAWN * (n - 1));
    }
    Bitboard pb = ourPawns;
    while (pb) {
        const int sq = popLsb(pb);
        const int f = fileOf(sq);
        const int rel = us == WHITE ? rankOf(sq) : 7 - rankOf(sq);
        const bool isolated = (ourPawns & adjacentFiles(f)) == EMPTY_BB;
        if (isolated) s.pawns.add(-ISOLATED_PAWN);
        const Bitboard front = ranksAhead(us, sq) & (fileMask(f) | adjacentFiles(f));
        const bool passed = (theirPawns & front) == EMPTY_BB;
        if (passed) {
            if (flags & EVAL_PASSED) {
                int mg = PASSED_MG[rel], eg = PASSED_EG[rel];
                if (rel >= 2 && rel <= 6) {
                    const int stop = us == WHITE ? sq + 8 : sq - 8;
                    if (ourKing != NO_KING && theirKing != NO_KING)
                        eg += ((chebyshev(theirKing, stop) * 5 - chebyshev(ourKing, stop) * 3) * (rel - 1)) / 2;
                    if (testBit(occ, stop)) {
                        mg -= PASSED_MG[rel] / 4;
                        eg -= PASSED_EG[rel] / 4;
                    } else if ((ranksAhead(us, sq) & fileMask(f) & occ) == EMPTY_BB) {
                        mg += PASSED_MG[rel] / 4;
                        eg += PASSED_EG[rel] / 3;
                    }
                }
                s.pawns.add(mg, eg);
            } else {
                s.pawns.add(PASSED_PAWN[rel]);
            }
        }
        if (flags & EVAL_PAWNS) {
            const bool supported = testBit(mine.pawnAtt, sq);
            const bool phalanx = (ourPawns & adjacentFiles(f) & rankMask(rankOf(sq))) != EMPTY_BB;
            const int c = CONNECTED[rel] * ((supported ? 1 : 0) + (phalanx ? 1 : 0));
            s.pawns.add(c, c);
            // Backward: no friendly pawn beside or behind on the neighbouring files, and the square in front is
            // controlled by an enemy pawn (so the pawn cannot advance safely).
            if (!isolated && !passed && rel >= 1 && rel <= 5 && (ourPawns & adjacentFiles(f) & ~ranksAhead(us, sq)) == EMPTY_BB) {
                const int stop = us == WHITE ? sq + 8 : sq - 8;
                if (testBit(theirPawnAtt, stop)) s.pawns.add(-BACKWARD_MG, -BACKWARD_EG);
            }
        }
    }

    if (popCount(pos.pieces(us, BISHOP)) >= 2) s.bishopPair.add(BISHOP_PAIR);

    // King safety: pawn shield (middlegame only; the phase fades it out).
    if (ourKing != NO_KING) {
        const int kf = fileOf(ourKing), kr = rankOf(ourKing);
        const int dir = us == WHITE ? 1 : -1;
        const int r1 = kr + dir, r2 = kr + 2 * dir;
        if (r1 >= 0 && r1 <= 7) {
            for (int f = kf - 1; f <= kf + 1; ++f) {
                if (f < 0 || f > 7) continue;
                if (testBit(ourPawns, squareOf(f, r1))) s.kingSafety.add(SHIELD_NEAR, 0);
                else if (r2 >= 0 && r2 <= 7 && testBit(ourPawns, squareOf(f, r2))) s.kingSafety.add(SHIELD_FAR, 0);
                else s.kingSafety.add(SHIELD_MISSING, 0);
            }
        }
    }
    // Pressure on our king zone: only counted with two or more attackers, and growing quickly with their strength.
    if ((flags & EVAL_KINGZONE) && theirs.attackers >= 2) {
        const int pen = std::min(KINGZONE_MAX, theirs.units * theirs.units / 4);
        s.kingSafety.add(-pen, 0);
    }

    if (flags & EVAL_THREATS) {
        const Bitboard theirPieces = pos.pieces(them, KNIGHT) | pos.pieces(them, BISHOP) | pos.pieces(them, ROOK) | pos.pieces(them, QUEEN);
        const Bitboard pawnHit = mine.pawnAtt & theirPieces;
        s.threats.add(THREAT_PAWN_MG * popCount(pawnHit), THREAT_PAWN_EG * popCount(pawnHit));
        const Bitboard heavy = pos.pieces(them, ROOK) | pos.pieces(them, QUEEN);
        const int minorHit = popCount(mine.minorAtt & heavy & ~pawnHit);
        s.threats.add(THREAT_MINOR_MG * minorHit, THREAT_MINOR_EG * minorHit);
        const int rookQueen = popCount(mine.rookAtt & pos.pieces(them, QUEEN) & ~pawnHit);
        s.threats.add(THREAT_ROOK_QUEEN_MG * rookQueen, THREAT_ROOK_QUEEN_EG * rookQueen);
        const int hanging = popCount(mine.allAtt & theirPieces & ~theirs.allAtt & ~pawnHit);
        s.threats.add(HANGING_MG * hanging, HANGING_EG * hanging);
    }

    if (flags & EVAL_SPACE) {
        const int pieces = countMinors(pos, us) + popCount(pos.pieces(us, ROOK)) + popCount(pos.pieces(us, QUEEN));
        const Bitboard safe = CENTER_FILES & ownHalfRanks(us) & ~ourPawns & ~theirPawnAtt;
        s.space.add(popCount(safe) * pieces / 4, 0);
    }
}

int taper(const Pair& p, int phase) { return (p.mg * phase + p.eg * (PHASE_MAX - phase)) / PHASE_MAX; }

Pair diff(const Pair& w, const Pair& b) {
    Pair d;
    d.mg = w.mg - b.mg;
    d.eg = w.eg - b.eg;
    return d;
}

}  // namespace

int pieceValue(PieceType pt) {
    switch (pt) {
        case PAWN: return PAWN_VALUE;
        case KNIGHT: return KNIGHT_VALUE;
        case BISHOP: return BISHOP_VALUE;
        case ROOK: return ROOK_VALUE;
        case QUEEN: return QUEEN_VALUE;
        default: return 0;
    }
}

int pstValue(PieceType pt, Color c, int sq, bool endgame) {
    const int s = c == WHITE ? sq : (sq ^ 56);
    const int f = fileOf(s);
    const int idx = rankOf(s) * 4 + (f < 4 ? f : 7 - f);
    const int table = (pt == KING && endgame) ? 6 : static_cast<int>(pt);
    return HALF[table][idx];
}

bool isEndgame(const Position& pos) {
    for (int c = 0; c < 2; ++c) {
        const Color col = static_cast<Color>(c);
        if (pos.pieces(col, QUEEN) == EMPTY_BB) continue;
        if (pos.pieces(col, ROOK) != EMPTY_BB || countMinors(pos, col) > 1) return false;
    }
    return true;
}

int gamePhase(const Position& pos) {
    int phase = 0;
    for (int c = 0; c < 2; ++c) {
        const Color col = static_cast<Color>(c);
        phase += PHASE_KNIGHT * popCount(pos.pieces(col, KNIGHT)) + PHASE_BISHOP * popCount(pos.pieces(col, BISHOP)) +
                 PHASE_ROOK * popCount(pos.pieces(col, ROOK)) + PHASE_QUEEN * popCount(pos.pieces(col, QUEEN));
    }
    return std::min(phase, PHASE_MAX);
}

namespace {
unsigned g_evalFlags = EVAL_BASIC;

struct FlagName { const char* name; unsigned flag; };
constexpr FlagName FLAG_NAMES[] = {
    {"taper", EVAL_TAPER}, {"mobility", EVAL_MOBILITY}, {"passed", EVAL_PASSED}, {"pawns", EVAL_PAWNS}, {"outpost", EVAL_OUTPOST},
    {"rook", EVAL_ROOK}, {"kingzone", EVAL_KINGZONE}, {"threats", EVAL_THREATS}, {"space", EVAL_SPACE},
};
}  // namespace

unsigned evalFlags() { return g_evalFlags; }
void setEvalFlags(unsigned flags) { g_evalFlags = flags & EVAL_ALL; }
// Chosen from the self-play ladder (README, Stage 18): all nine features together beat the basic evaluation by about
// +135 Elo (200 games), and all nine were not worse than the six individually non-negative ones (+26, error about 42).
unsigned evalDefault() { return EVAL_ALL; }

bool parseEvalFlags(const std::string& text, unsigned& flags) {
    unsigned f = EVAL_BASIC;
    size_t i = 0;
    bool ok = true;
    while (i <= text.size()) {
        size_t j = text.find(',', i);
        if (j == std::string::npos) j = text.size();
        std::string tok = text.substr(i, j - i);
        i = j + 1;
        if (tok.empty()) continue;
        const bool remove = tok[0] == '-';
        if (remove || tok[0] == '+') tok.erase(0, 1);
        unsigned bits = 0;
        if (tok == "none" || tok == "basic") bits = 0;
        else if (tok == "all") bits = EVAL_ALL;
        else if (tok == "default") bits = evalDefault();
        else {
            bool found = false;
            for (const FlagName& n : FLAG_NAMES) if (tok == n.name) { bits = n.flag; found = true; }
            if (!found) { ok = false; continue; }
        }
        if (remove) f &= ~bits; else f |= bits;
    }
    if (ok) flags = f;
    return ok;
}

std::string evalFlagsToString(unsigned flags) {
    std::string s;
    for (const FlagName& n : FLAG_NAMES) {
        if (!(flags & n.flag)) continue;
        if (!s.empty()) s += ',';
        s += n.name;
    }
    return s.empty() ? "none" : s;
}

EvalTerms evaluateTerms(const Position& pos, unsigned flags) {
    const int phase = (flags & EVAL_TAPER) ? gamePhase(pos) : (isEndgame(pos) ? 0 : PHASE_MAX);
    SideScore w, b;
    SideInfo wi, bi;
    gather(pos, WHITE, flags, w, wi);
    gather(pos, BLACK, flags, b, bi);
    finish(pos, WHITE, flags, w, wi, bi);
    finish(pos, BLACK, flags, b, bi, wi);
    EvalTerms t;
    t.material = taper(diff(w.material, b.material), phase);
    t.pst = taper(diff(w.pst, b.pst), phase);
    t.pawns = taper(diff(w.pawns, b.pawns), phase);
    t.bishopPair = taper(diff(w.bishopPair, b.bishopPair), phase);
    t.mobility = taper(diff(w.mobility, b.mobility), phase);
    t.kingSafety = taper(diff(w.kingSafety, b.kingSafety), phase);
    t.outposts = taper(diff(w.outposts, b.outposts), phase);
    t.rooks = taper(diff(w.rooks, b.rooks), phase);
    t.threats = taper(diff(w.threats, b.threats), phase);
    t.space = taper(diff(w.space, b.space), phase);
    return t;
}

EvalTerms evaluateTerms(const Position& pos) { return evaluateTerms(pos, g_evalFlags); }

int evaluate(const Position& pos, unsigned flags) {
    const int white = evaluateTerms(pos, flags).total();
    return pos.sideToMove() == WHITE ? white : -white;
}

int evaluate(const Position& pos) { return evaluate(pos, g_evalFlags); }