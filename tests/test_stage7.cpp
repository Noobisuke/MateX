// Stage 7 tests: make / unmake

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/move/movegen.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

struct Played { Move move; UndoInfo undo; };

// Finds the legal move with this UCI string and makes it. Returns what is needed to unmake.
static Played play(Position& pos, const std::string& uci) {
    MoveList legal;
    generateLegalMoves(pos, legal);
    for (size_t i = 0; i < legal.size(); ++i) {
        if (legal[i].toString() == uci) {
            Move m = legal[i];
            return {m, makeMove(pos, m)};
        }
    }
    std::cout << "TEST SETUP FAILED: no legal move " << uci << " in " << toFEN(pos) << "\n";
    return {};
}

static bool occupancyOk(const Position& pos) {
    Bitboard w = 0, b = 0;
    for (int p = WHITE_PAWN; p <= WHITE_KING; ++p) w |= pos.pieces(static_cast<Piece>(p));
    for (int p = BLACK_PAWN; p <= BLACK_KING; ++p) b |= pos.pieces(static_cast<Piece>(p));
    return pos.occupancy(WHITE) == w && pos.occupancy(BLACK) == b && pos.occupancyAll() == (w | b);
}

// Plays `uci`, checks the resulting FEN, unmakes, checks exact restoration.
static void checkMoveAndUndo(const std::string& startFen, const std::string& uci,
                             const std::string& expectedFen) {
    Position pos = fromFEN(startFen);
    const Position before = pos;
    Played p = play(pos, uci);
    CHECK(toFEN(pos) == expectedFen,
          "after " + uci + " from " + startFen + ": expected " + expectedFen + ", got " + toFEN(pos));
    CHECK(pos.isConsistent() && occupancyOk(pos), "consistent after " + uci);
    unmakeMove(pos, p.move, p.undo);
    CHECK(pos == before, "unmake restores exact position after " + uci + " (" + startFen + ")");
    CHECK(toFEN(pos) == startFen, "unmake restores the FEN after " + uci);
}

// --- Position equality ---------------------------------------------------------

void testPositionEquality() {
    Position a = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Position b = a;
    CHECK(a == b, "copy equals original");
    Played p = play(b, "e2e4");
    CHECK(!(a == b), "position differs after a move");
    unmakeMove(b, p.move, p.undo);
    CHECK(a == b, "equal again after unmake");
    Position c = a;
    c.setHalfmoveClock(5);
    CHECK(!(a == c), "operator== sees the halfmove clock");
}

// --- Individual move kinds ----------------------------------------------------------

void testQuietPawnAndDoublePush() {
    checkMoveAndUndo("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4",
                     "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    checkMoveAndUndo("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e3",
                     "rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    checkMoveAndUndo("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "d7d5",
                     "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
}

void testQuietPieceMoveClocks() {
    // Knight move: halfmove clock increments; White's move does not bump the fullmove number.
    checkMoveAndUndo("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 3 7", "g1f3",
                     "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 4 7");
    // Black's move increments the fullmove number.
    checkMoveAndUndo("4k3/8/8/8/8/8/8/4K3 b - - 3 20", "e8e7", "8/4k3/8/8/8/8/8/4K3 w - - 4 21");
}

void testCapture() {
    // exd5 resets the halfmove clock and removes the black pawn; unmake brings it back.
    checkMoveAndUndo("4k3/8/8/3p4/4P3/8/8/4K3 w - - 7 20", "e4d5", "4k3/8/8/3P4/8/8/8/4K3 b - - 0 20");
}

void testCastling() {
    const std::string f = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    checkMoveAndUndo(f, "e1g1", "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
    checkMoveAndUndo(f, "e1c1", "r3k2r/8/8/8/8/8/8/2KR3R b kq - 1 1");
    const std::string g = "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1";
    checkMoveAndUndo(g, "e8g8", "r4rk1/8/8/8/8/8/8/R3K2R w KQ - 1 2");
    checkMoveAndUndo(g, "e8c8", "2kr3r/8/8/8/8/8/8/R3K2R w KQ - 1 2");
}

void testCastlingRightsChanges() {
    const std::string f = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    checkMoveAndUndo(f, "h1h2", "r3k2r/8/8/8/8/8/7R/R3K3 b Qkq - 1 1");
    checkMoveAndUndo(f, "a1a2", "r3k2r/8/8/8/8/8/R7/4K2R b Kkq - 1 1");
    checkMoveAndUndo(f, "e1e2", "r3k2r/8/8/8/8/8/4K3/R6R b kq - 1 1");
    // Rook captures the h8 rook: white loses K (rook left h1), black loses k (rook captured).
    checkMoveAndUndo(f, "h1h8", "r3k2R/8/8/8/8/8/8/R3K3 b Qq - 0 1");
}

void testEnPassant() {
    const std::string f = "4k3/8/8/KPp5/8/8/8/8 w - c6 0 1";
    checkMoveAndUndo(f, "b5c6", "4k3/8/2P5/K7/8/8/8/8 b - - 0 1");  // black pawn c5 removed
    // A move that is not the capture clears the ep square; unmake brings it back.
    checkMoveAndUndo(f, "b5b6", "4k3/8/1P6/K1p5/8/8/8/8 b - - 0 1");
    // Black captures en passant.
    checkMoveAndUndo("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1", "d4e3", "4k3/8/8/8/8/4p3/8/4K3 w - - 0 2");
}

void testPromotion() {
    const std::string f = "4k3/P7/8/8/8/8/8/4K3 w - - 0 1";
    checkMoveAndUndo(f, "a7a8q", "Q3k3/8/8/8/8/8/8/4K3 b - - 0 1");
    checkMoveAndUndo(f, "a7a8r", "R3k3/8/8/8/8/8/8/4K3 b - - 0 1");
    checkMoveAndUndo(f, "a7a8b", "B3k3/8/8/8/8/8/8/4K3 b - - 0 1");
    checkMoveAndUndo(f, "a7a8n", "N3k3/8/8/8/8/8/8/4K3 b - - 0 1");
    // Promotion capture: the knight comes back on unmake, and the pawn returns to a7.
    checkMoveAndUndo("1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8r", "1R2k3/8/8/8/8/8/8/4K3 b - - 0 1");
    // Black promotion.
    checkMoveAndUndo("4k3/8/8/8/8/8/p7/4K3 b - - 0 30", "a2a1q", "4k3/8/8/8/8/8/8/q3K3 w - - 0 31");
    // Promotion capture of a corner rook clears that castling right.
    checkMoveAndUndo("r3k3/1P6/8/8/8/8/8/4K3 w q - 0 1", "b7a8q", "Q3k3/8/8/8/8/8/8/4K3 b - - 0 1");
}

void testPromotedPieceType() {
    Position pos = fromFEN("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    Played p = play(pos, "a7a8n");
    CHECK(pos.pieceOn(A8) == WHITE_KNIGHT && pos.pieceOn(A7) == NO_PIECE, "Promotion to knight placed on a8");
    unmakeMove(pos, p.move, p.undo);
    CHECK(pos.pieceOn(A7) == WHITE_PAWN && pos.pieceOn(A8) == NO_PIECE, "Unmake restores the pawn, empties a8");
    CHECK(pos.pieces(WHITE, KNIGHT) == 0, "No phantom knight bitboard left behind");
}

// --- Sequences (LIFO) -----------------------------------------------------------------

void testSequenceAndFullUnwind() {
    Position pos;
    pos.setStartingPosition();
    const Position start = pos;
    std::vector<Played> stack;

    stack.push_back(play(pos, "e2e4"));
    CHECK(toFEN(pos) == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "after 1.e4");
    stack.push_back(play(pos, "c7c5"));
    CHECK(toFEN(pos) == "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", "after 1...c5");
    stack.push_back(play(pos, "g1f3"));
    CHECK(toFEN(pos) == "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2", "after 2.Nf3");

    while (!stack.empty()) {
        unmakeMove(pos, stack.back().move, stack.back().undo);
        stack.pop_back();
        CHECK(pos.isConsistent() && occupancyOk(pos), "consistent during unwind");
    }
    CHECK(pos == start, "unwinding three moves restores the start position exactly");
}

void testRepeatedMakeUnmakeSameMove() {
    Position pos = fromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const Position before = pos;
    for (int i = 0; i < 1000; ++i) {
        Played p = play(pos, "e1g1");
        unmakeMove(pos, p.move, p.undo);
    }
    CHECK(pos == before, "1000 make/unmake cycles leave no drift");
}

// --- Round trip over EVERY legal move, recursively -------------------------------------------

// Walks the full legal move tree to `depth`, checking at every node that (a) the position stays
// consistent after make, and (b) unmake restores the exact previous position. Returns leaf count.
// This is a smoke check of make/unmake only; the real perft system (with divide) is Stage 9.
static uint64_t walk(Position& pos, int depth, bool& ok) {
    if (depth == 0) return 1;
    MoveList moves;
    generateLegalMoves(pos, moves);
    uint64_t nodes = 0;
    for (size_t i = 0; i < moves.size(); ++i) {
        const Position before = pos;
        const UndoInfo undo = makeMove(pos, moves[i]);
        if (!pos.isConsistent() || !occupancyOk(pos)) ok = false;
        nodes += walk(pos, depth - 1, ok);
        unmakeMove(pos, moves[i], undo);
        if (!(pos == before)) ok = false;
    }
    return nodes;
}

void testRoundTripTree() {
    struct Case { const char* name; const char* fen; uint64_t d1, d2, d3; };
    const Case cases[] = {
        {"start",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 20, 400, 8902},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 48, 2039, 97862},
        {"pos3",     "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 14, 191, 2812},
        {"pos4",     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, 264, 9467},
        {"pos5",     "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 44, 1486, 62379},
        {"pos6",     "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 46, 2079, 89890},
    };
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        const Position original = pos;
        bool ok = true;
        const uint64_t n1 = walk(pos, 1, ok);
        const uint64_t n2 = walk(pos, 2, ok);
        const uint64_t n3 = walk(pos, 3, ok);
        CHECK(ok, std::string(c.name) + ": make/unmake invariants hold at every node (depth 3)");
        CHECK(pos == original, std::string(c.name) + ": position unchanged after the whole walk");
        CHECK(n1 == c.d1, std::string(c.name) + " depth 1: expected " + std::to_string(c.d1) +
                          ", got " + std::to_string(n1));
        CHECK(n2 == c.d2, std::string(c.name) + " depth 2: expected " + std::to_string(c.d2) +
                          ", got " + std::to_string(n2));
        CHECK(n3 == c.d3, std::string(c.name) + " depth 3: expected " + std::to_string(c.d3) +
                          ", got " + std::to_string(n3));
    }
}

int main() {
    initAttackTables();

    testPositionEquality();
    testQuietPawnAndDoublePush();
    testQuietPieceMoveClocks();
    testCapture();
    testCastling();
    testCastlingRightsChanges();
    testEnPassant();
    testPromotion();
    testPromotedPieceType();
    testSequenceAndFullUnwind();
    testRepeatedMakeUnmakeSameMove();
    testRoundTripTree();

    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << testsFailed << " TESTS FAILED\n";
    return 1;
}