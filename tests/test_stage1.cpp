// Stage 1 tests: board representation
//
// Minimal, dependency-free test harness (no external test framework
// needed yet). Each test prints PASS/FAIL. We'll likely switch to a
// real framework (e.g. Catch2 or GoogleTest) in a later stage once
// the test suite grows, but for Stage 1 this keeps things simple and
// transparent.

#include "../src/board/position.hpp"
#include "../src/board/bitboard.hpp"
#include <iostream>
#include <cassert>

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do { \
    ++testsRun; \
    if (!(cond)) { \
        ++testsFailed; \
        std::cout << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while (0)

void testStartingPositionPieceCounts() {
    Position pos;
    pos.setStartingPosition();

    CHECK(popCount(pos.pieces(WHITE, PAWN)) == 8, "White should have 8 pawns");
    CHECK(popCount(pos.pieces(BLACK, PAWN)) == 8, "Black should have 8 pawns");
    CHECK(popCount(pos.pieces(WHITE, KNIGHT)) == 2, "White should have 2 knights");
    CHECK(popCount(pos.pieces(WHITE, BISHOP)) == 2, "White should have 2 bishops");
    CHECK(popCount(pos.pieces(WHITE, ROOK)) == 2, "White should have 2 rooks");
    CHECK(popCount(pos.pieces(WHITE, QUEEN)) == 1, "White should have 1 queen");
    CHECK(popCount(pos.pieces(WHITE, KING)) == 1, "White should have 1 king");

    CHECK(popCount(pos.occupancy(WHITE)) == 16, "White should have 16 pieces total");
    CHECK(popCount(pos.occupancy(BLACK)) == 16, "Black should have 16 pieces total");
    CHECK(popCount(pos.occupancyAll()) == 32, "There should be 32 pieces total");
}

void testStartingPositionSpecificSquares() {
    Position pos;
    pos.setStartingPosition();

    CHECK(pos.pieceOn(A1) == WHITE_ROOK, "a1 should be a white rook");
    CHECK(pos.pieceOn(E1) == WHITE_KING, "e1 should be the white king");
    CHECK(pos.pieceOn(D1) == WHITE_QUEEN, "d1 should be the white queen");
    CHECK(pos.pieceOn(E8) == BLACK_KING, "e8 should be the black king");
    CHECK(pos.pieceOn(D8) == BLACK_QUEEN, "d8 should be the black queen");
    CHECK(pos.pieceOn(A8) == BLACK_ROOK, "a8 should be a black rook");
    CHECK(pos.pieceOn(E4) == NO_PIECE, "e4 should be empty at game start");
    CHECK(pos.pieceOn(E2) == WHITE_PAWN, "e2 should be a white pawn");
    CHECK(pos.pieceOn(E7) == BLACK_PAWN, "e7 should be a black pawn");
}

void testStartingPositionMetadata() {
    Position pos;
    pos.setStartingPosition();

    CHECK(pos.sideToMove() == WHITE, "White should move first");
    CHECK(pos.castlingRights() == (WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE),
          "All castling rights should be available at game start");
    CHECK(pos.enPassantSquare() == NO_SQUARE, "No en passant square at game start");
    CHECK(pos.halfmoveClock() == 0, "Halfmove clock should start at 0");
    CHECK(pos.fullmoveNumber() == 1, "Fullmove number should start at 1");
}

void testConsistencyAfterMutation() {
    Position pos;
    pos.setStartingPosition();
    CHECK(pos.isConsistent(), "Starting position should be internally consistent");

    // Simulate removing a pawn and placing a queen (as if it had promoted),
    // then check bitboards/mailbox agree.
    pos.removePiece(E2);
    CHECK(pos.pieceOn(E2) == NO_PIECE, "e2 should be empty after removePiece");
    CHECK(pos.isConsistent(), "Position should remain consistent after removePiece");

    pos.putPiece(WHITE_QUEEN, E5);
    CHECK(pos.pieceOn(E5) == WHITE_QUEEN, "e5 should hold the newly placed queen");
    CHECK(pos.isConsistent(), "Position should remain consistent after putPiece");

    // Overwriting an occupied square should not corrupt state (putPiece
    // is defensive: it removes any existing piece first).
    pos.putPiece(BLACK_KNIGHT, E5);
    CHECK(pos.pieceOn(E5) == BLACK_KNIGHT, "e5 should now hold a black knight");
    CHECK(popCount(pos.pieces(WHITE, QUEEN)) == 1, "Original white queen (d1) should be untouched");
    CHECK(pos.isConsistent(), "Position should remain consistent after overwrite");
}

void testClearAndEmptyBoard() {
    Position pos;
    pos.setStartingPosition();
    pos.clear();

    CHECK(pos.occupancyAll() == EMPTY_BB, "Board should be empty after clear()");
    CHECK(pos.sideToMove() == WHITE, "Side to move should reset to White after clear()");
    CHECK(pos.castlingRights() == 0, "Castling rights should reset to none after clear()");
    CHECK(pos.enPassantSquare() == NO_SQUARE, "En passant should reset after clear()");
    for (int sq = 0; sq < 64; ++sq) {
        CHECK(pos.pieceOn(sq) == NO_PIECE, "All squares should be empty after clear()");
    }
    CHECK(pos.isConsistent(), "Empty board should be internally consistent");
}

void testSquareStringConversions() {
    CHECK(squareFromString("a1") == A1, "a1 string should map to A1");
    CHECK(squareFromString("h8") == H8, "h8 string should map to H8");
    CHECK(squareFromString("e4") == E4, "e4 string should map to E4");
    CHECK(squareToString(A1) == "a1", "A1 should stringify to a1");
    CHECK(squareToString(H8) == "h8", "H8 should stringify to h8");
    CHECK(squareToString(E4) == "e4", "E4 should stringify to e4");
}

void testBitboardHelpers() {
    Bitboard bb = EMPTY_BB;
    setBit(bb, E4);
    CHECK(testBit(bb, E4), "testBit should see the bit we just set");
    CHECK(popCount(bb) == 1, "popCount should be 1 after setting a single bit");

    setBit(bb, A1);
    CHECK(popCount(bb) == 2, "popCount should be 2 after setting a second bit");

    int lsb = lsbIndex(bb);
    CHECK(lsb == A1, "lsbIndex should find A1 (index 0) as the lowest set bit");

    int popped = popLsb(bb);
    CHECK(popped == A1, "popLsb should return A1 first");
    CHECK(popCount(bb) == 1, "popLsb should remove the bit it returned");
    CHECK(testBit(bb, E4), "E4 bit should remain after popping A1");

    clearBit(bb, E4);
    CHECK(bb == EMPTY_BB, "Bitboard should be empty after clearing its last bit");
}

int main() {
    testStartingPositionPieceCounts();
    testStartingPositionSpecificSquares();
    testStartingPositionMetadata();
    testConsistencyAfterMutation();
    testClearAndEmptyBoard();
    testSquareStringConversions();
    testBitboardHelpers();

    std::cout << "\n" << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cout << testsFailed << " CHECK(S) FAILED\n";
        return 1;
    }
}
