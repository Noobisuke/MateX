// Stage 3 tests: attack detection

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include <iostream>

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do { \
    ++testsRun; \
    if (!(cond)) { \
        ++testsFailed; \
        std::cout << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while (0)

Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) {
        std::cout << "TEST SETUP FAILED to load FEN '" << fen << "': " << err << "\n";
    }
    return pos;
}

// --- Knight attacks ----------------------------------------------------------

void testKnightAttacksCenter() {
    // Knight on e4 (a central square) should have all 8 jumps available.
    Bitboard bb = knightAttacksFrom(E4);
    CHECK(popCount(bb) == 8, "Knight on e4 should attack 8 squares");
    int expected[8] = { D2, F2, C3, G3, C5, G5, D6, F6 };
    for (int sq : expected) {
        CHECK(testBit(bb, sq), std::string("Knight on e4 should attack ") + squareToString(sq));
    }
}

void testKnightAttacksCorner() {
    // Knight in the corner (a1) only has 2 legal jumps.
    Bitboard bb = knightAttacksFrom(A1);
    CHECK(popCount(bb) == 2, "Knight on a1 should attack exactly 2 squares");
    CHECK(testBit(bb, B3), "Knight on a1 should attack b3");
    CHECK(testBit(bb, C2), "Knight on a1 should attack c2");
}

// --- King attacks --------------------------------------------------------------

void testKingAttacksCenter() {
    Bitboard bb = kingAttacksFrom(E4);
    CHECK(popCount(bb) == 8, "King on e4 should attack 8 squares");
}

void testKingAttacksCorner() {
    Bitboard bb = kingAttacksFrom(A1);
    CHECK(popCount(bb) == 3, "King on a1 should attack exactly 3 squares");
    CHECK(testBit(bb, A2), "King on a1 should attack a2");
    CHECK(testBit(bb, B1), "King on a1 should attack b1");
    CHECK(testBit(bb, B2), "King on a1 should attack b2");
}

// --- Pawn attacks --------------------------------------------------------------

void testPawnAttacksWhite() {
    Bitboard bb = pawnAttacksFrom(E4, WHITE);
    CHECK(popCount(bb) == 2, "White pawn on e4 should attack 2 squares");
    CHECK(testBit(bb, D5), "White pawn on e4 should attack d5");
    CHECK(testBit(bb, F5), "White pawn on e4 should attack f5");
}

void testPawnAttacksBlack() {
    Bitboard bb = pawnAttacksFrom(E4, BLACK);
    CHECK(popCount(bb) == 2, "Black pawn on e4 should attack 2 squares");
    CHECK(testBit(bb, D3), "Black pawn on e4 should attack d3");
    CHECK(testBit(bb, F3), "Black pawn on e4 should attack f3");
}

void testPawnAttacksEdgeOfBoard() {
    // Pawn on the a-file only has one diagonal attack.
    Bitboard bb = pawnAttacksFrom(A4, WHITE);
    CHECK(popCount(bb) == 1, "White pawn on a4 should attack only 1 square (no wraparound)");
    CHECK(testBit(bb, B5), "White pawn on a4 should attack b5");

    Bitboard bbH = pawnAttacksFrom(H4, WHITE);
    CHECK(popCount(bbH) == 1, "White pawn on h4 should attack only 1 square (no wraparound)");
    CHECK(testBit(bbH, G5), "White pawn on h4 should attack g5");
}

// --- Sliding piece attacks: blocking behavior --------------------------------

void testRookAttacksOpenBoard() {
    Bitboard occ = EMPTY_BB;
    setBit(occ, E4); // the rook itself
    Bitboard bb = rookAttacksFrom(E4, occ);
    CHECK(popCount(bb) == 14, "Rook on e4 on an otherwise empty board should attack 14 squares");
}

void testRookAttacksBlocked() {
    Bitboard occ = EMPTY_BB;
    setBit(occ, E4);
    setBit(occ, E6); // blocker two squares up
    Bitboard bb = rookAttacksFrom(E4, occ);
    CHECK(testBit(bb, E5), "Rook should attack e5 (empty square before blocker)");
    CHECK(testBit(bb, E6), "Rook should attack e6 (the blocker itself)");
    CHECK(!testBit(bb, E7), "Rook should NOT attack e7 (beyond the blocker)");
    CHECK(!testBit(bb, E8), "Rook should NOT attack e8 (beyond the blocker)");
}

void testBishopAttacksBlocked() {
    Bitboard occ = EMPTY_BB;
    setBit(occ, E4);
    setBit(occ, G6); // blocker on the diagonal
    Bitboard bb = bishopAttacksFrom(E4, occ);
    CHECK(testBit(bb, F5), "Bishop should attack f5 (empty square before blocker)");
    CHECK(testBit(bb, G6), "Bishop should attack g6 (the blocker itself)");
    CHECK(!testBit(bb, H7), "Bishop should NOT attack h7 (beyond the blocker)");
}

void testQueenAttacksIsUnionOfRookAndBishop() {
    Bitboard occ = EMPTY_BB;
    setBit(occ, E4);
    Bitboard rook = rookAttacksFrom(E4, occ);
    Bitboard bishop = bishopAttacksFrom(E4, occ);
    Bitboard queen = queenAttacksFrom(E4, occ);
    CHECK(queen == (rook | bishop), "Queen attacks should equal rook attacks | bishop attacks");
}

// --- isSquareAttacked / isInCheck on real positions --------------------------

void testStartingPositionNoChecks() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(!isInCheck(pos, WHITE), "Neither side should be in check at game start (White)");
    CHECK(!isInCheck(pos, BLACK), "Neither side should be in check at game start (Black)");
}

void testStartingPositionAttackedSquares() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    // White pawns attack rank 3 diagonals.
    CHECK(isSquareAttacked(pos, B3, WHITE), "b3 should be attacked by White at game start (from a2/c2 pawns)");
    // Black's back rank pieces are not attacking White's back rank at game start.
    CHECK(!isSquareAttacked(pos, E1, BLACK), "e1 should not be attacked by Black at game start");
    // e4 is not attacked by anything at move 1.
    CHECK(!isSquareAttacked(pos, E4, WHITE), "e4 should not yet be attacked by White at game start");
    CHECK(!isSquareAttacked(pos, E4, BLACK), "e4 should not yet be attacked by Black at game start");
}

void testFoolsMateCheckmatePosition() {
    // 1. f3 e5 2. g4 Qh4# - White king on e1 is in check from the black queen on h4.
    Position pos = fromFEN("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    CHECK(isInCheck(pos, WHITE), "White king should be in check in the fool's mate position");
    CHECK(!isInCheck(pos, BLACK), "Black king should not be in check (it's White's king that's mated)");
    CHECK(isSquareAttacked(pos, E1, BLACK), "e1 (white king square) should be attacked by Black");
}

void testDiscoveredCheckStylePosition() {
    // White rook on e-file, black king on e8, black pawn/piece removed
    // from between them - simple open-file rook check.
    Position pos = fromFEN("4k3/8/8/8/8/8/8/4R2K w - - 0 1");
    CHECK(isInCheck(pos, BLACK), "Black king on e8 should be in check from rook on e1 down an open file");
}

void testKnightCheck() {
    // White knight on f6 attacks e8 (a valid knight jump) — black king in check.
    Position pos = fromFEN("4k3/8/5N2/8/8/8/8/4K3 b - - 0 1");
    CHECK(isInCheck(pos, BLACK), "Black king on e8 should be in check from knight on f6");
}

void testPawnCheck() {
    // Black king on e5, white pawn on d4 attacks e5.
    Position pos = fromFEN("8/8/8/4k3/3P4/8/8/4K3 b - - 0 1");
    CHECK(isInCheck(pos, BLACK), "Black king on e5 should be in check from white pawn on d4");
}

void testBlockedSlidingPieceDoesNotCheck() {
    // White rook on e1, own pawn on e4 blocking the file, black king on e8.
    // The rook's attack should stop at e4 and not reach e8.
    Position pos = fromFEN("4k3/8/8/8/4P3/8/8/4R2K b - - 0 1");
    CHECK(!isInCheck(pos, BLACK), "Black king should NOT be in check; White's own pawn blocks the rook's file");
}

void testEnemyPieceBlocksSlidingAttackToo() {
    // Same idea but the blocker is an enemy piece - still blocks.
    Position pos = fromFEN("4k3/8/8/8/4p3/8/8/4R2K b - - 0 1");
    CHECK(!isInCheck(pos, BLACK), "Black king should NOT be in check; black's own pawn blocks the rook's file");
}

void testKingsCannotAttackThroughDistance() {
    // Kings far apart should not "attack" each other.
    Position pos = fromFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    CHECK(!isSquareAttacked(pos, E8, WHITE), "White king far away should not attack e8");
    CHECK(!isSquareAttacked(pos, E1, BLACK), "Black king far away should not attack e1");
}

void testAdjacentKingsAttackEachOther() {
    // Illegal position in a real game, but a good unit test of the primitive:
    // kings next to each other should register as attacking one another.
    Position pos = fromFEN("8/8/8/3k4/3K4/8/8/8 w - - 0 1");
    CHECK(isSquareAttacked(pos, D5, WHITE), "Adjacent white king should attack d5");
    CHECK(isSquareAttacked(pos, D4, BLACK), "Adjacent black king should attack d4");
}

void testDoubleCheckPosition() {
    // A position with simultaneous rook + knight check on the black king.
    Position pos = fromFEN("4k3/8/5N2/8/8/8/8/4R1K1 b - - 0 1");
    CHECK(isInCheck(pos, BLACK), "Black king should be in check (double check from rook and knight)");
    CHECK(isSquareAttacked(pos, E8, WHITE), "e8 should be attacked by White");
    // Verify both attackers individually contribute (rook via file, knight via jump).
    Bitboard occ = pos.occupancyAll();
    CHECK(testBit(rookAttacksFrom(E1, occ), E8), "Rook on e1 should reach e8 along the open e-file");
    CHECK(testBit(knightAttacksFrom(F6), E8), "Knight on f6 should attack e8");
}

int main() {
    initAttackTables();

    testKnightAttacksCenter();
    testKnightAttacksCorner();
    testKingAttacksCenter();
    testKingAttacksCorner();
    testPawnAttacksWhite();
    testPawnAttacksBlack();
    testPawnAttacksEdgeOfBoard();
    testRookAttacksOpenBoard();
    testRookAttacksBlocked();
    testBishopAttacksBlocked();
    testQueenAttacksIsUnionOfRookAndBishop();

    testStartingPositionNoChecks();
    testStartingPositionAttackedSquares();
    testFoolsMateCheckmatePosition();
    testDiscoveredCheckStylePosition();
    testKnightCheck();
    testPawnCheck();
    testBlockedSlidingPieceDoesNotCheck();
    testEnemyPieceBlocksSlidingAttackToo();
    testKingsCannotAttackThroughDistance();
    testAdjacentKingsAttackEachOther();
    testDoubleCheckPosition();

    std::cout << "\n" << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cout << testsFailed << " CHECK(S) FAILED\n";
        return 1;
    }
}
