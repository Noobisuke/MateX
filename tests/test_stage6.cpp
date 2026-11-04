// Stage 6 tests: special rules (castling safety, castling rights, en passant, promotion)

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/move/movegen.hpp"
#include "../src/move/legal.hpp"
#include "../src/rules/special.hpp"
#include <iostream>
#include <string>

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do { \
    ++testsRun; \
    if (!(cond)) { \
        ++testsFailed; \
        std::cout << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while (0)

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) {
        std::cout << "TEST SETUP FAILED to load FEN '" << fen << "': " << err << "\n";
    }
    return pos;
}

static MoveList legalOf(const Position& pos) {
    MoveList m;
    generateLegalMoves(pos, m);
    return m;
}

static MoveList pseudoOf(const Position& pos) {
    MoveList m;
    generatePseudoLegalMoves(pos, m);
    return m;
}

static const Move* find(const MoveList& moves, const std::string& uci) {
    for (size_t i = 0; i < moves.size(); ++i)
        if (moves[i].toString() == uci) return &moves[i];
    return nullptr;
}

static bool has(const MoveList& moves, const std::string& uci) { return find(moves, uci) != nullptr; }

static int movesFrom(const MoveList& moves, int sq) {
    int n = 0;
    for (size_t i = 0; i < moves.size(); ++i) if (moves[i].from == sq) ++n;
    return n;
}

// Legal castling availability for a FEN: (kingside, queenside) for the side to move.
struct Castles { bool k; bool q; };
static Castles castles(const std::string& fen) {
    Position pos = fromFEN(fen);
    MoveList m = legalOf(pos);
    Square kFrom = pos.sideToMove() == WHITE ? E1 : E8;
    Square kTo = pos.sideToMove() == WHITE ? G1 : G8;
    Square qTo = pos.sideToMove() == WHITE ? C1 : C8;
    Castles c{false, false};
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i].from == kFrom && m[i].flag == CASTLE_KINGSIDE && m[i].to == kTo) c.k = true;
        if (m[i].from == kFrom && m[i].flag == CASTLE_QUEENSIDE && m[i].to == qTo) c.q = true;
    }
    return c;
}

// =============================== CASTLING ===============================

void testCastlingBothSidesAvailable() {
    Castles w = castles("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    CHECK(w.k && w.q, "White can castle both ways on an empty back rank");
    Castles b = castles("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    CHECK(b.k && b.q, "Black can castle both ways");
}

void testCastlingRightsRequired() {
    Castles none = castles("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1");
    CHECK(!none.k && !none.q, "No rights: no castling");
    Castles kOnly = castles("r3k2r/8/8/8/8/8/8/R3K2R w K - 0 1");
    CHECK(kOnly.k && !kOnly.q, "Only K right: kingside only");
    Castles qOnly = castles("r3k2r/8/8/8/8/8/8/R3K2R w Q - 0 1");
    CHECK(!qOnly.k && qOnly.q, "Only Q right: queenside only");
    Castles bq = castles("r3k2r/8/8/8/8/8/8/R3K2R b q - 0 1");
    CHECK(!bq.k && bq.q, "Black q only: queenside only");
}

void testCastlingNotInferredFromPlacement() {
    // Rights written in the FEN are kept as written, but a king off e1 cannot castle.
    Position pos = fromFEN("r3k2r/8/8/8/8/8/4K3/R6R w KQkq - 0 1");
    CHECK(pos.castlingRights() == (WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE),
          "FEN rights stored as written");
    MoveList m = legalOf(pos);
    CHECK(!has(m, "e2g2") && !has(m, "e1g1") && !has(m, "e1c1"), "King on e2 cannot castle");
    // Right held but rook missing.
    Castles c = castles("4k3/8/8/8/8/8/8/4K2R w KQ - 0 1");
    CHECK(c.k && !c.q, "Q right without an a1 rook: no queenside castle");
    // Right NOT held although king and rooks are home: structure alone is not enough.
    Castles n = castles("r3k2r/8/8/8/8/8/8/R3K2R w kq - 0 1");
    CHECK(!n.k && !n.q, "King/rooks home but no white rights: no castling");
}

void testCastlingBlockedByPieces() {
    Castles c1 = castles("4k3/8/8/8/8/8/8/RN2K2R w KQ - 0 1");
    CHECK(c1.k && !c1.q, "Knight on b1 blocks queenside only");
    Castles c2 = castles("4k3/8/8/8/8/8/8/R3KB1R w KQ - 0 1");
    CHECK(!c2.k && c2.q, "Bishop on f1 blocks kingside only");
    Castles c3 = castles("4k3/8/8/8/8/8/8/R2QK1NR w KQ - 0 1");
    CHECK(!c3.k && !c3.q, "Pieces on d1 and g1 block both");
}

void testCastlingOutOfCheck() {
    Castles r = castles("4r1k1/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(!r.k && !r.q, "In check from a rook: no castling");
    Castles p = castles("4k3/8/8/8/8/8/3p4/R3K2R w KQ - 0 1");
    CHECK(!p.k && !p.q, "In check from a pawn: no castling");
    Castles bl = castles("r3k2r/8/8/8/8/8/8/4R2K b kq - 0 1");
    CHECK(!bl.k && !bl.q, "Black in check from a rook: no castling");
}

void testCastlingThroughAttackedSquare() {
    Castles f = castles("5r1k/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(!f.k && f.q, "f1 attacked: kingside illegal, queenside legal");
    Castles d = castles("3r3k/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(f.q && d.k && !d.q, "d1 attacked: queenside illegal, kingside legal");
    Castles bf = castles("r3k2r/8/8/8/8/8/8/5R1K b kq - 0 1");
    CHECK(!bf.k && bf.q, "Black: f8 attacked by Rf1: kingside illegal, queenside legal");
}

void testCastlingOntoAttackedSquare() {
    Castles g = castles("6rk/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(!g.k && g.q, "g1 attacked: kingside illegal, queenside legal");
    Castles c = castles("2r4k/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(c.k && !c.q, "c1 attacked: queenside illegal, kingside legal");
}

void testCastlingByKnightAndBishopAttacks() {
    Castles n = castles("4k3/8/8/8/8/4n3/8/R3K2R w KQ - 0 1");
    CHECK(!n.k && !n.q, "Knight e3 covers d1 and f1: both castles illegal");
    // Bishop on a6 attacks f1 (a6-b5-c4-d3-e2-f1): kingside illegal.
    Castles b = castles("4k3/8/b7/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(!b.k && b.q, "Bishop a6 covers f1: kingside illegal, queenside legal");
}

void testQueensideBFileMayBeAttacked() {
    // b1 attacked by a rook on b8, but b1 is only required to be EMPTY.
    Castles c = castles("1r5k/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(c.q && c.k, "b1 attacked does not prevent queenside castling");
}

void testAttackedRookDoesNotPreventCastling() {
    Castles h = castles("4k2r/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(h.k && h.q, "Black rook attacking Rh1 does not stop castling");
    Castles a = castles("r3k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    CHECK(a.k && a.q, "Black rook attacking Ra1 does not stop castling");
}

void testCastlingMoveShape() {
    Position pos = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    MoveList m = legalOf(pos);
    const Move* k = find(m, "e1g1");
    const Move* q = find(m, "e1c1");
    CHECK(k && k->flag == CASTLE_KINGSIDE && k->piece == WHITE_KING, "e1g1 is CASTLE_KINGSIDE by the king");
    CHECK(q && q->flag == CASTLE_QUEENSIDE && q->piece == WHITE_KING, "e1c1 is CASTLE_QUEENSIDE by the king");
}

void testCastlingPathSafeDirect() {
    Position pos = fromFEN("5r1k/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    MoveList p = pseudoOf(pos);
    const Move* k = find(p, "e1g1");
    const Move* q = find(p, "e1c1");
    CHECK(k && q, "Both castles are structurally pseudo-legal");
    CHECK(k && !castlingPathSafe(pos, *k), "castlingPathSafe rejects kingside (f1 attacked)");
    CHECK(q && castlingPathSafe(pos, *q), "castlingPathSafe accepts queenside");
    const Move* quiet = find(p, "e1e2");
    CHECK(quiet && castlingPathSafe(pos, *quiet), "castlingPathSafe is true for non-castling moves");
}

// ============================ CASTLING RIGHTS ============================

static const CastlingRights ALL = WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE;

void testRightsAfterKingMove() {
    Position pos = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    MoveList m = legalOf(pos);
    CHECK(castlingRightsAfter(ALL, *find(m, "e1e2")) == (BLACK_KINGSIDE | BLACK_QUEENSIDE),
          "White king move clears both white rights only");
    CHECK(castlingRightsAfter(ALL, *find(m, "e1g1")) == (BLACK_KINGSIDE | BLACK_QUEENSIDE),
          "Castling clears both white rights");
    CHECK(castlingRightsAfter(ALL, *find(m, "e1c1")) == (BLACK_KINGSIDE | BLACK_QUEENSIDE),
          "Queenside castling clears both white rights");
}

void testRightsAfterRookMove() {
    Position pos = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    MoveList m = legalOf(pos);
    CHECK(castlingRightsAfter(ALL, *find(m, "a1a2")) == (ALL & ~WHITE_QUEENSIDE), "Ra1 move clears Q only");
    CHECK(castlingRightsAfter(ALL, *find(m, "h1h2")) == (ALL & ~WHITE_KINGSIDE), "Rh1 move clears K only");
    Position bp = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    MoveList bm = legalOf(bp);
    CHECK(castlingRightsAfter(ALL, *find(bm, "a8a7")) == (ALL & ~BLACK_QUEENSIDE), "Ra8 move clears q only");
    CHECK(castlingRightsAfter(ALL, *find(bm, "h8h7")) == (ALL & ~BLACK_KINGSIDE), "Rh8 move clears k only");
    CHECK(castlingRightsAfter(ALL, *find(bm, "e8e7")) == (WHITE_KINGSIDE | WHITE_QUEENSIDE),
          "Black king move clears both black rights only");
}

void testRightsAfterRookCaptured() {
    // Rh1 takes the h8 rook: Black loses the kingside right, White also loses K (own rook left h1).
    Position pos = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    MoveList m = legalOf(pos);
    const Move* cap = find(m, "h1h8");
    CHECK(cap && cap->isCapture(), "h1h8 is a capture");
    CHECK(castlingRightsAfter(ALL, *cap) == (WHITE_QUEENSIDE | BLACK_QUEENSIDE),
          "Rxh8 clears white K (rook left h1) and black k (rook captured)");
}

void testRightsAfterPromotionCapturingRook() {
    Position pos = fromFEN("r3k3/1P6/8/8/8/8/8/4K3 w q - 0 1");
    MoveList m = legalOf(pos);
    const Move* cap = find(m, "b7a8q");
    CHECK(cap && cap->flag == PROMOTION_CAPTURE, "b7xa8=Q is a promotion capture");
    CHECK(cap && castlingRightsAfter(BLACK_QUEENSIDE, *cap) == 0, "Promotion capture on a8 clears black q");
}

void testRightsUnchangedByOtherMoves() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    MoveList m = legalOf(pos);
    CHECK(castlingRightsAfter(ALL, *find(m, "g1f3")) == ALL, "Knight move keeps all rights");
    CHECK(castlingRightsAfter(ALL, *find(m, "e2e4")) == ALL, "Pawn push keeps all rights");
    CHECK(castlingRightsAfter(0, *find(m, "e2e4")) == 0, "No rights stays no rights");
}

// ============================== EN PASSANT ==============================

void testEnPassantSquareAfterMove() {
    MoveList m = legalOf(fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    CHECK(enPassantSquareAfter(*find(m, "e2e4")) == E3, "e2e4 sets ep square e3");
    CHECK(enPassantSquareAfter(*find(m, "e2e3")) == NO_SQUARE, "Single push sets no ep square");
    CHECK(enPassantSquareAfter(*find(m, "g1f3")) == NO_SQUARE, "Knight move sets no ep square");
    MoveList b = legalOf(fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"));
    CHECK(enPassantSquareAfter(*find(b, "d7d5")) == D6, "d7d5 sets ep square d6");
}

void testEnPassantOnlyWhenCapturePossible() {
    CHECK(!has(pseudoOf(fromFEN("4k3/8/8/8/8/8/8/4K3 w - d6 0 1")), "e5d6"),
          "No pawns: no en passant move");
    Position wrongRank = fromFEN("4k3/8/8/8/3pP3/8/8/4K3 w - e3 0 1");
    int ep = 0;
    MoveList m = legalOf(wrongRank);
    for (size_t i = 0; i < m.size(); ++i) if (m[i].flag == EN_PASSANT) ++ep;
    CHECK(ep == 0, "White to move with ep square on rank 3: no en passant move");
}

void testEnPassantBlackCapture() {
    MoveList m = legalOf(fromFEN("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1"));
    const Move* ep = find(m, "d4e3");
    CHECK(ep && ep->flag == EN_PASSANT, "Black d4xe3 en passant generated");
    CHECK(ep && ep->captured == WHITE_PAWN, "Captures a white pawn");
}

void testEnPassantHorizontalPinBlack() {
    // Black king a4, pawn d4 | white pawn e4, rook h4: dxe3 e.p. would expose the king.
    Position pos = fromFEN("8/8/8/8/k2pP2R/8/8/7K b - e3 0 1");
    CHECK(find(pseudoOf(pos), "d4e3") != nullptr, "d4xe3 e.p. is pseudo-legal");
    MoveList m = legalOf(pos);
    CHECK(!has(m, "d4e3"), "d4xe3 e.p. is illegal: both pawns leave rank 4");
    CHECK(has(m, "d4d3"), "d4d3 remains legal");
}

void testEnPassantDiagonalDiscoveredCheck() {
    // Bishop b8 -> h2 diagonal is blocked only by the black pawn e5.
    // f5xe6 e.p. removes e5 (not replaced on the diagonal) and exposes Kh2.
    Position pos = fromFEN("1b2k3/8/8/4pP2/8/8/7K/8 w - e6 0 1");
    CHECK(find(pseudoOf(pos), "f5e6") != nullptr, "f5xe6 e.p. is pseudo-legal");
    MoveList m = legalOf(pos);
    CHECK(!has(m, "f5e6"), "f5xe6 e.p. is illegal: removing e5 opens the b8-h2 diagonal");
    CHECK(has(m, "f5f6"), "f5f6 is legal: e5 still blocks");
}

// ============================== PROMOTION ==============================

void testQuietPromotionFourPieces() {
    MoveList m = legalOf(fromFEN("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(movesFrom(m, A7) == 4, "Quiet promotion: exactly 4 moves, got " + std::to_string(movesFrom(m, A7)));
    const Move* q = find(m, "a7a8q");
    const Move* r = find(m, "a7a8r");
    const Move* b = find(m, "a7a8b");
    const Move* n = find(m, "a7a8n");
    CHECK(q && q->promotion == QUEEN && q->flag == PROMOTION, "a7a8q: queen, PROMOTION");
    CHECK(r && r->promotion == ROOK, "a7a8r: rook");
    CHECK(b && b->promotion == BISHOP, "a7a8b: bishop");
    CHECK(n && n->promotion == KNIGHT, "a7a8n: knight");
    CHECK(!has(m, "a7a8k") && !has(m, "a7a8p") && !has(m, "a7a8"), "No king/pawn/bare promotion");
}

void testPromotionCaptureFourPieces() {
    MoveList m = legalOf(fromFEN("1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(movesFrom(m, A7) == 8, "Push (4) + capture (4) = 8, got " + std::to_string(movesFrom(m, A7)));
    const Move* c = find(m, "a7b8q");
    CHECK(c && c->flag == PROMOTION_CAPTURE && c->captured == BLACK_KNIGHT, "a7xb8=Q captures the knight");
    CHECK(has(m, "a7b8r") && has(m, "a7b8b") && has(m, "a7b8n"), "All four capture promotions");
}

void testBothCapturesAndPushPromotion() {
    MoveList m = legalOf(fromFEN("r1r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(movesFrom(m, B7) == 12, "bxa8, bxc8 and b8 = 3 targets x 4 pieces = 12, got " +
          std::to_string(movesFrom(m, B7)));
}

void testBlackPromotion() {
    MoveList m = legalOf(fromFEN("4k3/8/8/8/8/8/p7/4K3 b - - 0 1"));
    CHECK(movesFrom(m, A2) == 4, "Black promotion: 4 moves");
    CHECK(has(m, "a2a1q") && has(m, "a2a1r") && has(m, "a2a1b") && has(m, "a2a1n"),
          "Black a2a1 with q/r/b/n");
    const Move* q = find(m, "a2a1q");
    CHECK(q && q->piece == BLACK_PAWN && q->promotion == QUEEN, "Black pawn promotes to a queen (type)");
}

void testPromotionAnswersCheck() {
    // Rook h8 checks Ka8. Promotions on g8 block, gxh8 captures the checker.
    MoveList m = legalOf(fromFEN("K6r/6P1/8/8/8/8/8/4k3 w - - 0 1"));
    CHECK(has(m, "g7g8q") && has(m, "g7g8n"), "Promotion on g8 blocks the check");
    CHECK(has(m, "g7h8q") && has(m, "g7h8r"), "Promotion capture takes the checker");
    CHECK(!has(m, "a8b8"), "Kb8 stays on the checking rank");
    CHECK(m.size() == 10, "4 blocks + 4 captures + Ka7 + Kb7 = 10, got " + std::to_string(m.size()));
}

void testPinnedPawnCannotPromote() {
    MoveList m = legalOf(fromFEN("8/K2P3r/8/8/8/8/8/7k w - - 0 1"));
    CHECK(movesFrom(m, D7) == 0, "Pawn pinned along the 7th rank cannot promote");
}

int main() {
    initAttackTables();

    testCastlingBothSidesAvailable();
    testCastlingRightsRequired();
    testCastlingNotInferredFromPlacement();
    testCastlingBlockedByPieces();
    testCastlingOutOfCheck();
    testCastlingThroughAttackedSquare();
    testCastlingOntoAttackedSquare();
    testCastlingByKnightAndBishopAttacks();
    testQueensideBFileMayBeAttacked();
    testAttackedRookDoesNotPreventCastling();
    testCastlingMoveShape();
    testCastlingPathSafeDirect();

    testRightsAfterKingMove();
    testRightsAfterRookMove();
    testRightsAfterRookCaptured();
    testRightsAfterPromotionCapturingRook();
    testRightsUnchangedByOtherMoves();

    testEnPassantSquareAfterMove();
    testEnPassantOnlyWhenCapturePossible();
    testEnPassantBlackCapture();
    testEnPassantHorizontalPinBlack();
    testEnPassantDiagonalDiscoveredCheck();

    testQuietPromotionFourPieces();
    testPromotionCaptureFourPieces();
    testBothCapturesAndPushPromotion();
    testBlackPromotion();
    testPromotionAnswersCheck();
    testPinnedPawnCannotPromote();

    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << testsFailed << " TESTS FAILED\n";
    return 1;
}