// Regression tests added by the Stage 1-7 audit.
//
// Bugs found and fixed:
//   1. Position::putPiece(NO_PIECE, sq) indexed pieceBB_[12] (out of bounds) and corrupted the
//      neighbouring occupancy field. It now empties the square.
//   2. loadFEN accepted impossible positions; "capturing the king" was a legal move. Added
//      validatePosition() (src/rules/validate.hpp), used by the CLI tools.
// Plus permanent cross-checks against an independent implementation (python-chess 1.11.2).

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/move/movegen.hpp"
#include "../src/move/legal.hpp"
#include "../src/rules/validate.hpp"
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

static bool valid(const std::string& fen, std::string* why = nullptr) {
    return validatePosition(fromFEN(fen), why);
}

// --- Bug 1: putPiece(NO_PIECE) ------------------------------------------------------

void testPutNoPiece() {
    Position pos = fromFEN("4k3/8/8/8/4P3/8/8/4K3 w - - 0 1");
    pos.putPiece(NO_PIECE, E4);
    CHECK(pos.pieceOn(E4) == NO_PIECE, "putPiece(NO_PIECE) empties the square");
    CHECK(pos.isConsistent(), "Position stays consistent after putPiece(NO_PIECE)");
    CHECK(pos.pieces(WHITE, PAWN) == 0, "pawn bitboard cleared");
    pos.putPiece(NO_PIECE, A1);  // already empty: harmless no-op
    CHECK(pos.isConsistent(), "putPiece(NO_PIECE) on an empty square is a no-op");
    CHECK(toFEN(pos) == "4k3/8/8/8/8/8/8/4K3 w - - 0 1", "FEN after emptying e4");
}

// --- Bug 2: impossible positions ----------------------------------------------------------

void testValidPositionsAccepted() {
    CHECK(valid("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"), "start position valid");
    CHECK(valid("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"), "Kiwipete valid");
    CHECK(valid("4k3/8/8/KPp5/8/8/8/8 w - c6 0 1"), "valid en passant (white to move)");
    CHECK(valid("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1"), "valid en passant (black to move)");
    CHECK(valid("4r1k1/8/8/7Q/8/5n2/8/4K3 w - - 0 1"), "double check position is valid");
}

void testInvalidPositionsRejected() {
    std::string why;
    CHECK(!valid("8/8/8/8/8/8/8/4K3 w - - 0 1", &why) && !why.empty(), "no black king: " + why);
    CHECK(!valid("4k3/8/8/8/8/8/8/8 w - - 0 1"), "no white king");
    CHECK(!valid("4k2k/8/8/8/8/8/8/4K3 w - - 0 1"), "two black kings");
    CHECK(!valid("8/8/8/8/8/8/3kK3/8 w - - 0 1"), "adjacent kings");
    CHECK(!valid("P3k3/8/8/8/8/8/8/4K3 w - - 0 1"), "white pawn on rank 8");
    CHECK(!valid("4k3/8/8/8/8/8/8/p3K3 w - - 0 1"), "black pawn on rank 1");
    CHECK(!valid("4k3/8/PPPPPPPP/P7/8/8/8/4K3 w - - 0 1"), "nine white pawns");
    // The original bug: Black's king is en prise with White to move, so Rxe8 "captured the king".
    CHECK(!valid("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1", &why), "side not to move in check: " + why);
    CHECK(valid("4k3/8/8/8/8/8/4R3/4K3 b - - 0 1"), "same position with Black to move is fine (Black is in check)");
}

void testInvalidEnPassant() {
    CHECK(!valid("4k3/8/8/KPp5/8/8/8/8 w - c3 0 1"), "ep on rank 3 with White to move");
    CHECK(!valid("4k3/8/8/KP6/8/8/8/8 w - c6 0 1"), "ep square but no pawn in front of it");
    CHECK(!valid("4k3/2n5/8/KPp5/8/8/8/8 w - c6 0 1"), "ep origin square c7 occupied");
    CHECK(!valid("4k3/8/2n5/KPp5/8/8/8/8 w - c6 0 1"), "ep square c6 occupied");
    CHECK(!valid("4k3/8/8/8/3pP3/8/8/4K3 b - e6 0 1"), "ep on rank 6 with Black to move");
    CHECK(valid("4k3/8/8/KPp5/8/8/8/8 w - c6 0 1") && validatePosition(fromFEN("4k3/8/8/KPp5/8/8/8/8 w - c6 0 1"), nullptr),
          "null error pointer is allowed");
}

// --- Extremes ---------------------------------------------------------------------------------------

void testMaxMovePosition() {
    // Well-known position with the maximum number of legal moves (218).
    Position pos = fromFEN("R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1");
    MoveList pseudo, legal;
    generatePseudoLegalMoves(pos, pseudo);
    generateLegalMoves(pos, legal);
    CHECK(legal.size() == 218, "218 legal moves, got " + std::to_string(legal.size()));
    CHECK(pseudo.size() < MoveList::CAPACITY, "pseudo-legal list fits in MoveList (no silent truncation)");
    CHECK(valid("R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1"), "the 218-move position is a valid position");
}

// --- Cross-check against python-chess (independent implementation) -----------------------------------

void testLegalMoveCountsMatchReference() {
    struct Case { const char* fen; size_t legal; };
    const Case cases[] = {
        {"7N/8/P7/8/8/8/6K1/1k6 w - - 1 61", 11},
        {"8/5B2/8/2k5/P7/8/2PK4/R7 b - - 2 48", 5},
        {"8/4k3/8/4p3/8/3b4/K5r1/8 w - - 5 35", 3},
        {"8/4k3/8/2b5/2p5/3p4/8/4K3 w - - 4 49", 3},
        {"q7/8/6k1/8/3b4/8/8/3K4 b - - 7 14", 42},
        {"8/4k3/5N2/8/8/2K5/4Q3/8 b - - 11 30", 5},
        {"8/8/8/7k/8/3Q4/2K5/8 w - - 12 20", 30},
        {"5Q2/8/8/8/4k3/8/8/6K1 w - - 29 31", 26},
        {"4N3/8/6k1/8/5P1K/8/8/4N2R w - - 3 41", 16},
        {"8/8/1k6/4P3/1P4P1/8/8/4K3 w - - 1 55", 8},
        {"r7/p3k3/3pb3/8/N6P/8/2Q1B3/4K2R w K - 0 21", 41},
        {"6n1/R5r1/2p3kp/4pb2/8/B3P3/P1PP1P1P/7K w - - 1 25", 28},
        {"8/2B5/6k1/8/5R2/P7/4K2P/8 b - - 26 35", 5},
        {"8/4k3/8/P7/3R4/8/4K3/8 w - - 15 57", 23},
        {"8/8/2P5/4k3/7P/2K5/8/8 b - - 2 52", 7},
        {"1n6/2k1n3/6Np/p1p5/P5P1/7P/5K2/3R4 b - - 0 31", 15},
        {"2r5/p4p2/3pp1p1/8/1N6/2P2P2/1K2k1n1/8 b - - 0 22", 30},
        {"8/3k1K2/8/4p3/8/p7/8/8 b - - 0 52", 7},
        {"8/5R2/2k5/8/8/4K3/1PP5/8 b - - 6 43", 5},
        {"r3qbn1/pBp1pkp1/1p1p4/8/4P2p/2P5/PP1B1PPP/R1NNK2R b KQ - 0 12", 26},
        {"r1r5/2p2p1B/n2p1b2/4k1B1/8/P6P/4K1P1/6NR b - - 0 22", 22},
        {"rnb3kr/3pp3/1p5b/6p1/6P1/4P2P/PPQN4/R3K1NR b KQ - 0 16", 21},
        {"3k4/8/2p3r1/1p6/7P/8/1bP5/5K2 w - - 0 28", 6},
        {"r2k4/2p1N3/1q1p1nBB/p7/4b3/2n2P2/P2K2PP/R5R1 b - - 1 16", 48},
    };
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        MoveList legal;
        generateLegalMoves(pos, legal);
        CHECK(legal.size() == c.legal, std::string("python-chess legal count for ") + c.fen +
              ": expected " + std::to_string(c.legal) + ", got " + std::to_string(legal.size()));
        CHECK(valid(c.fen), std::string("reference position passes validatePosition: ") + c.fen);
    }
}

int main() {
    initAttackTables();

    testPutNoPiece();
    testValidPositionsAccepted();
    testInvalidPositionsRejected();
    testInvalidEnPassant();
    testMaxMovePosition();
    testLegalMoveCountsMatchReference();

    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << testsFailed << " TESTS FAILED\n";
    return 1;
}