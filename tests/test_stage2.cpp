// Stage 2 tests: FEN parsing and generation

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
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

void checkRoundTrip(const std::string& fen, const char* label) {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, fen, &err);
    CHECK(ok, std::string(label) + ": loadFEN should succeed (" + err + ")");
    if (!ok) return;

    CHECK(pos.isConsistent(), std::string(label) + ": position should be consistent after load");

    std::string regenerated = toFEN(pos);
    CHECK(regenerated == fen, std::string(label) + ": round-trip mismatch. Expected '" +
          fen + "' got '" + regenerated + "'");
}

void testStartingPositionRoundTrip() {
    checkRoundTrip("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                    "Starting position");
}

void testStartingPositionMatchesSetStartingPosition() {
    Position fromFEN;
    loadFEN(fromFEN, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    Position fromMethod;
    fromMethod.setStartingPosition();

    CHECK(toFEN(fromFEN) == toFEN(fromMethod),
          "FEN-loaded starting position should equal setStartingPosition()");

    for (int sq = 0; sq < 64; ++sq) {
        CHECK(fromFEN.pieceOn(sq) == fromMethod.pieceOn(sq),
              "Square contents should match between FEN load and setStartingPosition()");
    }
}

void testEnPassantPosition() {
    // After 1. e4, en passant target is e3.
    checkRoundTrip("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
                    "En passant after 1.e4");

    // After 1. e4 c5 2. Nf3 d5, black just played d7-d5, target d6.
    checkRoundTrip("rnbqkbnr/pp2pppp/8/2ppp3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq d6 0 3",
                    "En passant black double push");
}

void testCastlingRightsVariants() {
    checkRoundTrip("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "All castling rights");
    checkRoundTrip("r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 1", "Partial castling rights");
    checkRoundTrip("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1", "No castling rights");
}

void testPromotionAdjacentPosition() {
    // White pawn on the verge of promoting on a7, black king far away.
    checkRoundTrip("4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "Pawn about to promote");
}

void testCheckAndCheckmatePositions() {
    // Fool's mate final position.
    checkRoundTrip("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
                    "Fool's mate position");
}

void testTacticalMidgamePosition() {
    // A well-known perft/tactics test position (Kiwipete).
    checkRoundTrip("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                    "Kiwipete position");
}

void testHalfmoveAndFullmoveNumbers() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 17 42", &err);
    CHECK(ok, "Should parse nonzero halfmove/fullmove: " + err);
    CHECK(pos.halfmoveClock() == 17, "Halfmove clock should be 17");
    CHECK(pos.fullmoveNumber() == 42, "Fullmove number should be 42");
}

void testLenientFieldCount() {
    // Missing halfmove/fullmove fields should default to 0 and 1.
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -", &err);
    CHECK(ok, "Should tolerate missing halfmove/fullmove fields: " + err);
    CHECK(pos.halfmoveClock() == 0, "Missing halfmove clock should default to 0");
    CHECK(pos.fullmoveNumber() == 1, "Missing fullmove number should default to 1");
}

// --- Invalid FEN handling ---------------------------------------------------

void testInvalidTooFewRanks() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1", &err);
    CHECK(!ok, "Should reject FEN with only 7 ranks");
    CHECK(!err.empty(), "Should provide an error message for missing ranks");
}

void testInvalidBadRankWidth() {
    Position pos;
    std::string err;
    // Rank has 9 files worth of content.
    bool ok = loadFEN(pos, "rnbqkbnrp/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &err);
    CHECK(!ok, "Should reject a rank that overflows 8 files");
}

void testInvalidPieceChar() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "xnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &err);
    CHECK(!ok, "Should reject invalid piece character 'x'");
}

void testInvalidSideToMove() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR z KQkq - 0 1", &err);
    CHECK(!ok, "Should reject invalid side-to-move character 'z'");
}

void testInvalidCastlingChar() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkqX - 0 1", &err);
    CHECK(!ok, "Should reject invalid castling rights character 'X'");
}

void testInvalidEnPassantSquare() {
    Position pos;
    std::string err;
    // e4 is not a valid en passant *target* rank (must be rank 3 or 6).
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e4 0 1", &err);
    CHECK(!ok, "Should reject en passant square not on rank 3 or 6");
}

void testInvalidNegativeHalfmove() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - -1 1", &err);
    CHECK(!ok, "Should reject negative halfmove clock");
}

void testInvalidZeroFullmove() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", &err);
    CHECK(!ok, "Should reject fullmove number of 0 (must be >= 1)");
}

void testEmptyFENRejected() {
    Position pos;
    std::string err;
    bool ok = loadFEN(pos, "", &err);
    CHECK(!ok, "Should reject empty FEN string");
}

void testPositionClearedAfterFailedLoad() {
    Position pos;
    pos.setStartingPosition();
    std::string err;
    bool ok = loadFEN(pos, "not a valid fen at all", &err);
    CHECK(!ok, "Malformed FEN should fail to load");
    CHECK(pos.occupancyAll() == EMPTY_BB, "Position should be cleared (not left half-mutated) after failed load");
}

int main() {
    testStartingPositionRoundTrip();
    testStartingPositionMatchesSetStartingPosition();
    testEnPassantPosition();
    testCastlingRightsVariants();
    testPromotionAdjacentPosition();
    testCheckAndCheckmatePositions();
    testTacticalMidgamePosition();
    testHalfmoveAndFullmoveNumbers();
    testLenientFieldCount();

    testInvalidTooFewRanks();
    testInvalidBadRankWidth();
    testInvalidPieceChar();
    testInvalidSideToMove();
    testInvalidCastlingChar();
    testInvalidEnPassantSquare();
    testInvalidNegativeHalfmove();
    testInvalidZeroFullmove();
    testEmptyFENRejected();
    testPositionClearedAfterFailedLoad();

    std::cout << "\n" << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cout << testsFailed << " CHECK(S) FAILED\n";
        return 1;
    }
}
