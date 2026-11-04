// Stage 5 tests: legal move generation (copy-make legality filter)

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/move/movegen.hpp"
#include "../src/move/legal.hpp"
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

static bool has(const MoveList& moves, const std::string& uci) {
    for (size_t i = 0; i < moves.size(); ++i)
        if (moves[i].toString() == uci) return true;
    return false;
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

static size_t legalCount(const std::string& fen) {
    return legalOf(fromFEN(fen)).size();
}

// Number of moves whose origin square is `sq`.
static int movesFrom(const MoveList& moves, int sq) {
    int n = 0;
    for (size_t i = 0; i < moves.size(); ++i) if (moves[i].from == sq) ++n;
    return n;
}

static std::string str(size_t n) { return std::to_string(n); }

// --- Basics ----------------------------------------------------------------------

void testStartingPosition() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    MoveList m = legalOf(pos);
    CHECK(m.size() == 20, "Start: 20 legal moves, got " + str(m.size()));
    CHECK(isLegalMove(pos, m[0]), "isLegalMove accepts a generated legal move");
}

void testDoesNotMutatePosition() {
    Position pos = fromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    std::string before = toFEN(pos);
    MoveList m = legalOf(pos);
    CHECK(toFEN(pos) == before, "generateLegalMoves leaves the position unchanged (FEN)");
    CHECK(pos.isConsistent(), "Position still consistent after legal generation");
    CHECK(m.size() == 48, "Kiwipete: 48 legal moves, got " + str(m.size()));
}

// --- Single check ------------------------------------------------------------------

void testKingCapturesUnprotectedChecker() {
    // Rook e2 checks Ke1, nothing protects it: Kxe2 legal. d2/f2 stay on the rook's rank.
    MoveList m = legalOf(fromFEN("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1"));
    CHECK(m.size() == 3, "Unprotected checker: Kd1 Kf1 Kxe2 = 3, got " + str(m.size()));
    CHECK(has(m, "e1e2"), "Kxe2 legal");
    CHECK(has(m, "e1d1") && has(m, "e1f1"), "Kd1, Kf1 legal");
    CHECK(!has(m, "e1d2") && !has(m, "e1f2"), "Kd2/Kf2 illegal (rook rank)");
}

void testKingCannotCaptureProtectedChecker() {
    // Same rook on e2, but black king d3 protects it.
    MoveList m = legalOf(fromFEN("8/8/8/8/8/3k4/4r3/4K3 w - - 0 1"));
    CHECK(m.size() == 2, "Protected checker: only Kd1, Kf1, got " + str(m.size()));
    CHECK(!has(m, "e1e2"), "Kxe2 illegal: rook protected by king");
}

void testKingCannotRetreatAlongCheckingRay() {
    // Rook a1 checks Ke1 along rank 1. Kf1 is "behind" the king on the same ray: illegal.
    MoveList m = legalOf(fromFEN("4k3/8/8/8/8/8/8/r3K3 w - - 0 1"));
    CHECK(!has(m, "e1f1"), "Kf1 illegal: x-ray through the king's own square");
    CHECK(!has(m, "e1d1"), "Kd1 illegal: still on the ray");
    CHECK(m.size() == 3, "Kd2 Ke2 Kf2 only, got " + str(m.size()));
}

void testBlockOrMoveKing() {
    // Rook e8 checks Ke1. Be5 and Re2 block; four king moves.
    MoveList m = legalOf(fromFEN("4r1k1/8/8/8/8/2B5/7R/4K3 w - - 0 1"));
    CHECK(m.size() == 6, "Block-or-run: 6 legal, got " + str(m.size()));
    CHECK(has(m, "c3e5"), "Bishop block on e5");
    CHECK(has(m, "h2e2"), "Rook block on e2");
    CHECK(!has(m, "e1e2"), "Ke2 stays on the e-file: illegal");
    CHECK(!has(m, "h2h8"), "Unrelated rook move ignores the check: illegal");
}

void testCaptureKnightChecker() {
    // Knight d3 checks Ke1 (cannot be blocked). Options: capture it with cxd3, or move the king.
    MoveList m = legalOf(fromFEN("4k3/8/8/8/8/3n4/2P5/4K3 w - - 0 1"));
    CHECK(m.size() == 5, "Knight check: Kd1 Kd2 Ke2 Kf1 + cxd3 = 5, got " + str(m.size()));
    CHECK(has(m, "c2d3"), "cxd3 captures the checker");
    CHECK(!has(m, "c2c3") && !has(m, "c2c4"), "Pawn pushes ignore the check: illegal");
    CHECK(!has(m, "e1f2"), "Kf2 attacked by the knight");
}

// --- Double check ----------------------------------------------------------------

void testDoubleCheckOnlyKingMoves() {
    // Rook e8 and knight f3 both check Ke1. The queen can take the knight but that leaves the rook check.
    Position pos = fromFEN("4r1k1/8/8/7Q/8/5n2/8/4K3 w - - 0 1");
    MoveList m = legalOf(pos);
    CHECK(isInCheck(pos, WHITE), "White is in check");
    CHECK(m.size() == 3, "Double check: Kd1 Kf1 Kf2 only, got " + str(m.size()));
    CHECK(movesFrom(m, E1) == 3, "All legal moves are king moves");
    CHECK(movesFrom(m, H5) == 0, "Queen has no legal move in double check");
    CHECK(!has(m, "h5f3"), "Qxf3 does not answer the rook check");
}

// --- Pins ---------------------------------------------------------------------------

void testPinnedRookMovesAlongPinOnly() {
    MoveList m = legalOf(fromFEN("4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1"));
    CHECK(movesFrom(m, E2) == 6, "Pinned rook: e3-e7 and Rxe8 = 6, got " + str(movesFrom(m, E2)));
    CHECK(has(m, "e2e8"), "Pinned rook may capture the pinner");
    CHECK(!has(m, "e2a2") && !has(m, "e2d2") && !has(m, "e2f2"), "Pinned rook cannot leave the file");
    CHECK(m.size() == 10, "Total 6 rook + 4 king = 10, got " + str(m.size()));
}

void testPinnedKnightHasNoMoves() {
    MoveList m = legalOf(fromFEN("4r1k1/8/8/8/8/8/4N3/4K3 w - - 0 1"));
    CHECK(movesFrom(m, E2) == 0, "Pinned knight has zero legal moves");
    CHECK(m.size() == 4, "Only the four king moves remain, got " + str(m.size()));
    CHECK(!pseudoOf(fromFEN("4r1k1/8/8/8/8/8/4N3/4K3 w - - 0 1")).empty() &&
          movesFrom(pseudoOf(fromFEN("4r1k1/8/8/8/8/8/4N3/4K3 w - - 0 1")), E2) > 0,
          "...even though pseudo-legal generation does produce knight moves");
}

void testPinnedPawnDiagonal() {
    // Bishop b4 pins pawn d2 to Ke1: pushes illegal.
    MoveList m = legalOf(fromFEN("6k1/8/8/8/1b6/8/3P4/4K3 w - - 0 1"));
    CHECK(movesFrom(m, D2) == 0, "Diagonally pinned pawn cannot push");
    CHECK(!has(m, "d2d3") && !has(m, "d2d4"), "d3/d4 illegal");
}

void testPinnedPawnCapturesPinner() {
    // Bishop c3 pins pawn d2; dxc3 captures the pinner and is legal.
    MoveList m = legalOf(fromFEN("6k1/8/8/8/8/2b5/3P4/4K3 w - - 0 1"));
    CHECK(has(m, "d2c3"), "Pawn may capture its pinner");
    CHECK(!has(m, "d2d3") && !has(m, "d2d4"), "Pushes remain illegal");
    CHECK(movesFrom(m, D2) == 1, "Exactly one pawn move, got " + str(movesFrom(m, D2)));
}

void testBlackPinned() {
    // Black to move: rook e7 pinned by Re1 against Ke8.
    MoveList m = legalOf(fromFEN("4k3/4r3/8/8/8/8/8/4R1K1 b - - 0 1"));
    CHECK(movesFrom(m, E7) == 6, "Black pinned rook: e6-e2 and Rxe1 = 6, got " + str(movesFrom(m, E7)));
    CHECK(m.size() == 10, "6 rook + 4 king = 10, got " + str(m.size()));
}

// --- En passant discovered check ---------------------------------------------------------

void testEnPassantHorizontalPinIllegal() {
    // Ka5 Pb5 | pc5 Rh5 all on rank 5. bxc6 e.p. removes BOTH pawns and exposes the king.
    Position pos = fromFEN("8/8/8/KPp4r/8/8/8/7k w - c6 0 1");
    CHECK(has(pseudoOf(pos), "b5c6"), "Pseudo-legal generator does produce b5xc6 e.p.");
    MoveList m = legalOf(pos);
    CHECK(!has(m, "b5c6"), "En passant removing both pawns from the rank is ILLEGAL");
    CHECK(has(m, "b5b6"), "Ordinary push b6 is still legal");
}

void testEnPassantLegalWhenSafe() {
    MoveList m = legalOf(fromFEN("4k3/8/8/KPp5/8/8/8/8 w - c6 0 1"));
    CHECK(has(m, "b5c6"), "En passant legal when no rook lines up");
}

void testEnPassantCapturesChecker() {
    // Black pawn d5 (just pushed d7-d5) checks Ke4. cxd6 e.p. removes the checker.
    MoveList m = legalOf(fromFEN("4k3/8/8/2Pp4/4K3/8/8/8 w - d6 0 1"));
    CHECK(has(m, "c5d6"), "En passant resolves a pawn check");
    CHECK(!has(m, "c5c6"), "c6 does not resolve the check");
    CHECK(has(m, "e4d5"), "Kxd5 also resolves it");
    CHECK(m.size() == 9, "8 king moves + e.p. = 9, got " + str(m.size()));
}

// --- Terminal positions ----------------------------------------------------------------

void testCheckmateAndStalemateHaveNoMoves() {
    Position mate = fromFEN("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    CHECK(isInCheck(mate, WHITE), "Fool's mate: white in check");
    CHECK(legalOf(mate).empty(), "Fool's mate: zero legal moves");
    CHECK(!pseudoOf(mate).empty(), "...but pseudo-legal moves exist");

    Position stale = fromFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(!isInCheck(stale, BLACK), "Stalemate: black not in check");
    CHECK(legalOf(stale).empty(), "Stalemate: zero legal moves");
}

// --- King safety generally -----------------------------------------------------------------

void testKingCannotApproachEnemyKing() {
    // Kings on e1 / e3: Ke2 would be adjacent to the enemy king.
    MoveList m = legalOf(fromFEN("8/8/8/8/8/4k3/8/4K3 w - - 0 1"));
    CHECK(!has(m, "e1e2") && !has(m, "e1d2") && !has(m, "e1f2"), "Cannot step next to the enemy king");
    CHECK(has(m, "e1d1") && has(m, "e1f1"), "Kd1, Kf1 fine");
}

void testDiscoveredAttackBlockerCannotLeave() {
    // Bishop e4 shields Ke1 from rook e8. It may not leave the e-file (bishops can't stay on it).
    MoveList m = legalOf(fromFEN("4r1k1/8/8/8/4B3/8/8/4K3 w - - 0 1"));
    CHECK(movesFrom(m, E4) == 0, "Bishop shielding the king has no legal moves");
}

// --- Castling boundary with Stage 6 -------------------------------------------------------------

void testCastlingLandingSquareFiltered() {
    // Rook g2 covers g1: kingside castle lands on an attacked square and is filtered now.
    MoveList m = legalOf(fromFEN("4k3/8/8/8/8/8/6r1/4K2R w K - 0 1"));
    CHECK(!has(m, "e1g1"), "Castling onto an attacked square is illegal");
}

void testStage6GapDocumented() {
    // Was a documented Stage 5 gap; fixed in Stage 6: castling out of check is rejected
    // even though the landing square (g1) is not attacked.
    MoveList m = legalOf(fromFEN("4r1k1/8/8/8/8/8/8/R3K2R w KQ - 0 1"));
    CHECK(!has(m, "e1g1") && !has(m, "e1c1"), "castling out of check is illegal (Stage 6)");
}

// --- Early depth-1 perft cross-check (standard perft positions) ------------------------------------

void testDepthOnePerftCounts() {
    struct Case { const char* name; const char* fen; size_t expected; };
    const Case cases[] = {
        {"start",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 20},
        {"kiwipete","r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 48},
        {"pos3",    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 14},
        {"pos4",    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6},
        {"pos4-mirror", "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 6},
        {"pos5",    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 44},
        {"pos6",    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 46},
    };
    for (const Case& c : cases) {
        size_t got = legalCount(c.fen);
        CHECK(got == c.expected, std::string("depth-1 count ") + c.name + ": expected " +
              str(c.expected) + ", got " + str(got));
    }
}

// --- Property checks across many positions ------------------------------------------------------------

void testPropertyLegalSubsetOfPseudo() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "4r1k1/8/8/7Q/8/5n2/8/4K3 w - - 0 1",
        "8/8/8/KPp4r/8/8/8/7k w - c6 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    };
    for (const char* f : fens) {
        Position pos = fromFEN(f);
        MoveList legal = legalOf(pos), pseudo = pseudoOf(pos);
        CHECK(legal.size() <= pseudo.size(), std::string("legal <= pseudo for ") + f);
        bool allSafe = true, allListed = true;
        for (size_t i = 0; i < legal.size(); ++i) {
            if (!leavesKingSafe(pos, legal[i])) allSafe = false;
            if (!has(pseudo, legal[i].toString())) allListed = false;
        }
        CHECK(allSafe, std::string("every legal move leaves king safe: ") + f);
        CHECK(allListed, std::string("every legal move is also pseudo-legal: ") + f);
    }
}

int main() {
    initAttackTables();

    testStartingPosition();
    testDoesNotMutatePosition();
    testKingCapturesUnprotectedChecker();
    testKingCannotCaptureProtectedChecker();
    testKingCannotRetreatAlongCheckingRay();
    testBlockOrMoveKing();
    testCaptureKnightChecker();
    testDoubleCheckOnlyKingMoves();
    testPinnedRookMovesAlongPinOnly();
    testPinnedKnightHasNoMoves();
    testPinnedPawnDiagonal();
    testPinnedPawnCapturesPinner();
    testBlackPinned();
    testEnPassantHorizontalPinIllegal();
    testEnPassantLegalWhenSafe();
    testEnPassantCapturesChecker();
    testCheckmateAndStalemateHaveNoMoves();
    testKingCannotApproachEnemyKing();
    testDiscoveredAttackBlockerCannotLeave();
    testCastlingLandingSquareFiltered();
    testStage6GapDocumented();
    testDepthOnePerftCounts();
    testPropertyLegalSubsetOfPseudo();

    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << testsFailed << " TESTS FAILED\n";
    return 1;
}