#include "attacks.hpp"
#include <array>

namespace attack_detail {
Bitboard knightTable[64];
Bitboard kingTable[64];
Bitboard pawnTable[2][64];
Magic bishopMagic[64];
Magic rookMagic[64];
}  // namespace attack_detail

namespace {

using attack_detail::Magic;

constexpr int KNIGHT_DELTAS[8][2] = {
    {1, 2}, {2, 1}, {2, -1}, {1, -2},
    {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
};

constexpr int KING_DELTAS[8][2] = {
    {1, 0}, {1, 1}, {0, 1}, {-1, 1},
    {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
};

constexpr int BISHOP_DIRS[4][2] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
constexpr int ROOK_DIRS[4][2]   = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

// Magic numbers found once by a search (fixed seed) and checked by tests/test_stage20.cpp against a ray walk for every
// square and every blocker pattern, so no search is needed at start-up.
constexpr Bitboard ROOK_MAGICS[64] = {
    0x1080004008801020ULL, 0x0840092002c03000ULL, 0x1900200010400900ULL, 0x0880100008000480ULL,
    0x4200100420080200ULL, 0x8100020100080400ULL, 0x0200040110886200ULL, 0x0200008040220411ULL,
    0x0404800084400220ULL, 0x0000401000402000ULL, 0x0086001081220440ULL, 0x0408800800100280ULL,
    0x000a001201040820ULL, 0x8848800200840080ULL, 0x4001000100040200ULL, 0x0442000102105084ULL,
    0x9080010020804100ULL, 0x0040404000201009ULL, 0x0000808010002009ULL, 0x2200090021d00100ULL,
    0x0008008008040080ULL, 0x0004004002010040ULL, 0x0011040008015042ULL, 0x00000a0001768104ULL,
    0x0000800080204009ULL, 0x2010004140002001ULL, 0x9800200280100080ULL, 0x1000100080080080ULL,
    0x0442000a00049020ULL, 0x2100040080020080ULL, 0x0800120400900148ULL, 0x0010040a00128541ULL,
    0x2800804000800030ULL, 0x1010002000400041ULL, 0x4000200011004100ULL, 0x0610008410800800ULL,
    0x0400802402800800ULL, 0xc100020080800400ULL, 0x0002000802000401ULL, 0x0182085882000401ULL,
    0x0220204000808000ULL, 0x2860100040024022ULL, 0x0001002004110040ULL, 0x99101042000a0020ULL,
    0x0004080004008080ULL, 0x0010040002008080ULL, 0x2012004881020004ULL, 0x8300842444820011ULL,
    0x0088403882010200ULL, 0x0820400080210100ULL, 0x0110910040a00300ULL, 0x0801100280080480ULL,
    0x0242009008200600ULL, 0x1002000489500200ULL, 0x0040800200010080ULL, 0x0091800041000080ULL,
    0x0000209300488001ULL, 0x04c1002414824001ULL, 0x020020000b001041ULL, 0x7000100004200901ULL,
    0x8002002004100802ULL, 0x30010002084c0007ULL, 0x0888221800813004ULL, 0x4000002840840112ULL,
};
constexpr Bitboard BISHOP_MAGICS[64] = {
    0xa010041108003100ULL, 0x006082020a002900ULL, 0x6810010619200000ULL, 0x08281a0520000408ULL,
    0x0001104001000400ULL, 0x0018901008048400ULL, 0x00040a0210245280ULL, 0x000200210808a402ULL,
    0x9140048410821200ULL, 0x0800091010820041ULL, 0x20504804832202c0ULL, 0x0100091401081000ULL,
    0x8021011140000012ULL, 0x0810020804450400ULL, 0x208b0542109008a2ULL, 0x0080084a08040204ULL,
    0x0040e2a80811244cULL, 0x2505022008008108ULL, 0x0430220100420040ULL, 0x010a040420220040ULL,
    0x1105000290400000ULL, 0x0093001200822120ULL, 0x4000a62048043004ULL, 0x280120048a015004ULL,
    0x006090002a020814ULL, 0x44042000240800d0ULL, 0x01102800040a4400ULL, 0x1004080080220040ULL,
    0x0001001011004024ULL, 0x0010044000805040ULL, 0x0914041200820100ULL, 0x0004821012821480ULL,
    0x0024040500c05021ULL, 0x0088611002080200ULL, 0x0116080a00040020ULL, 0x4000020080080080ULL,
    0x2450450140840040ULL, 0x0000880201484100ULL, 0x0222020404020092ULL, 0x8081110600002e00ULL,
    0x2842101105000801ULL, 0x1100809008001025ULL, 0x00020202221c0400ULL, 0x0422014022009020ULL,
    0x0210046102100c00ULL, 0xc004008082029102ULL, 0x00aa461801101200ULL, 0x0404080080201108ULL,
    0x020542108c205002ULL, 0x0410544804100100ULL, 0x0040910841100000ULL, 0x0400200042021100ULL,
    0x00004204850400c0ULL, 0x0200100410a42102ULL, 0x1040020801210102ULL, 0x0805040410420000ULL,
    0x2884804130100200ULL, 0x800c262201242000ULL, 0x1058000194108800ULL, 0x0014221054420204ULL,
    0x0104000012a02200ULL, 0x0200881003300100ULL, 0x0140400202840100ULL, 0x0402020801010201ULL,
};

Bitboard rookStorage[102400];  // sum over squares of 2^(relevant rook bits)
Bitboard bishopStorage[5248];  // same for bishops

Bitboard computeLeaperAttacks(int sq, const int deltas[8][2]) {
    Bitboard bb = EMPTY_BB;
    int f = fileOf(sq);
    int r = rankOf(sq);
    for (int i = 0; i < 8; ++i) {
        int nf = f + deltas[i][0];
        int nr = r + deltas[i][1];
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
            setBit(bb, squareOf(nf, nr));
        }
    }
    return bb;
}

// Walks the rays square by square. Only used to fill the tables (and, in the tests, as the reference).
Bitboard computeSlidingAttacks(int sq, Bitboard occupied, const int dirs[4][2]) {
    Bitboard bb = EMPTY_BB;
    int f = fileOf(sq);
    int r = rankOf(sq);
    for (int d = 0; d < 4; ++d) {
        int nf = f + dirs[d][0];
        int nr = r + dirs[d][1];
        while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
            int nsq = squareOf(nf, nr);
            setBit(bb, nsq);
            if (testBit(occupied, nsq)) {
                // The blocker's square itself IS attacked; nothing beyond it is.
                break;
            }
            nf += dirs[d][0];
            nr += dirs[d][1];
        }
    }
    return bb;
}

Bitboard computePawnAttacks(int sq, Color c) {
    Bitboard bb = EMPTY_BB;
    int f = fileOf(sq);
    int r = rankOf(sq);
    int forward = (c == WHITE) ? 1 : -1;
    int nr = r + forward;
    if (nr < 0 || nr > 7) return bb;
    if (f - 1 >= 0) setBit(bb, squareOf(f - 1, nr));
    if (f + 1 <= 7) setBit(bb, squareOf(f + 1, nr));
    return bb;
}

// The squares on a slider's rays whose occupancy can change the attack set: every ray square except the last one
// before the board edge (a piece on the edge square blocks nothing further).
Bitboard relevantMask(int sq, const int dirs[4][2]) {
    Bitboard bb = EMPTY_BB;
    const int f = fileOf(sq), r = rankOf(sq);
    for (int d = 0; d < 4; ++d) {
        int nf = f + dirs[d][0], nr = r + dirs[d][1];
        while (nf + dirs[d][0] >= 0 && nf + dirs[d][0] < 8 && nr + dirs[d][1] >= 0 && nr + dirs[d][1] < 8) {
            setBit(bb, squareOf(nf, nr));
            nf += dirs[d][0];
            nr += dirs[d][1];
        }
    }
    return bb;
}

void buildMagics(Magic* out, const Bitboard* magics, Bitboard* storage, const int dirs[4][2]) {
    size_t offset = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const Bitboard mask = relevantMask(sq, dirs);
        const int bits = popCount(mask);
        out[sq] = Magic{mask, magics[sq], storage + offset, 64 - bits};
        // Every subset of the mask (Carry-Rippler enumeration), including the empty one.
        Bitboard subset = EMPTY_BB;
        do {
            storage[offset + static_cast<size_t>((subset * magics[sq]) >> (64 - bits))] = computeSlidingAttacks(sq, subset, dirs);
            subset = (subset - mask) & mask;
        } while (subset != EMPTY_BB);
        offset += size_t(1) << bits;
    }
}

bool tablesInitialized = false;

struct AutoInit {  // the tables exist before main() runs, so the inline lookups never see an empty table
    AutoInit() { initAttackTables(); }
} autoInit;

} // namespace

void initAttackTables() {
    if (tablesInitialized) return;
    for (int sq = 0; sq < 64; ++sq) {
        attack_detail::knightTable[sq] = computeLeaperAttacks(sq, KNIGHT_DELTAS);
        attack_detail::kingTable[sq]   = computeLeaperAttacks(sq, KING_DELTAS);
        attack_detail::pawnTable[WHITE][sq] = computePawnAttacks(sq, WHITE);
        attack_detail::pawnTable[BLACK][sq] = computePawnAttacks(sq, BLACK);
    }
    buildMagics(attack_detail::rookMagic, ROOK_MAGICS, rookStorage, ROOK_DIRS);
    buildMagics(attack_detail::bishopMagic, BISHOP_MAGICS, bishopStorage, BISHOP_DIRS);
    tablesInitialized = true;
}

bool isSquareAttacked(const Position& pos, int square, Color byColor) {
    Bitboard occupied = pos.occupancyAll();

    // Pawn trick: a pawn of the OPPOSITE color standing on `square`
    // attacks exactly the squares from which a `byColor` pawn attacks it.
    if (pawnAttacksFrom(square, ~byColor) & pos.pieces(byColor, PAWN)) return true;

    if (knightAttacksFrom(square) & pos.pieces(byColor, KNIGHT)) return true;

    if (kingAttacksFrom(square) & pos.pieces(byColor, KING)) return true;

    Bitboard bishopsQueens = pos.pieces(byColor, BISHOP) | pos.pieces(byColor, QUEEN);
    if (bishopAttacksFrom(square, occupied) & bishopsQueens) return true;

    Bitboard rooksQueens = pos.pieces(byColor, ROOK) | pos.pieces(byColor, QUEEN);
    if (rookAttacksFrom(square, occupied) & rooksQueens) return true;

    return false;
}

bool isInCheck(const Position& pos, Color color) {
    Bitboard kingBB = pos.pieces(color, KING);
    if (kingBB == EMPTY_BB) {
        return false;
    }
    int kingSq = lsbIndex(kingBB);
    return isSquareAttacked(pos, kingSq, ~color);
}
