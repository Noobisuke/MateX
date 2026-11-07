// Stage 10 tests: basic evaluation.
#include "mirror.hpp"
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/eval/evaluate.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/suite.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace evalparams;

static int testsRun = 0, testsFailed = 0;
#define CHECK(cond, msg) do { ++testsRun; if (!(cond)) { ++testsFailed; std::cout << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; } } while (0)

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) { std::cout << "SETUP FAILED: " << fen << ": " << err << "\n"; ++testsFailed; }
    return pos;
}
static EvalTerms terms(const std::string& fen) { return evaluateTerms(fromFEN(fen)); }

static std::vector<std::string> samplePositions() {
    std::vector<std::string> v;
    for (const auto& c : standardPerftSuite()) v.push_back(c.fen);
    uint64_t rng = 0x9E3779B97F4A7C15ULL;
    for (int game = 0; game < 40; ++game) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 80; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            makeMove(pos, legal[(rng >> 33) % legal.size()]);
            if (ply % 7 == 0) v.push_back(toFEN(pos));
        }
    }
    return v;
}

void testBasics() {
    Position s; s.setStartingPosition();
    CHECK(evaluate(s) == 0, "start position evaluates to 0");
    const EvalTerms t = evaluateTerms(s);
    CHECK(t.material == 0 && t.pst == 0 && t.pawns == 0 && t.mobility == 0 && t.kingSafety == 0, "start terms all 0");
    CHECK(pieceValue(PAWN) == 100 && pieceValue(QUEEN) == 900 && pieceValue(KING) == 0, "piece values");
    CHECK(!isEndgame(s), "start is not endgame");
}

void testMaterial() {
    CHECK(terms("4k3/8/8/8/8/8/8/3QK3 w - - 0 1").material == QUEEN_VALUE, "white queen up");
    CHECK(terms("3qk3/8/8/8/8/8/8/4K3 w - - 0 1").material == -QUEEN_VALUE, "black queen up");
    CHECK(terms("4k3/8/8/8/8/8/PP6/1N2K3 w - - 0 1").material == 2 * PAWN_VALUE + KNIGHT_VALUE, "mixed material");
}

void testPerspective() {
    const std::string w = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    const std::string b = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 2 3";
    CHECK(evaluate(fromFEN(w)) == -evaluate(fromFEN(b)), "side-to-move flips sign");
    CHECK(evaluate(fromFEN("4k3/8/8/8/8/8/8/3QK3 b - - 0 1")) < 0, "black to move, white queen up: negative");
    CHECK(evaluate(fromFEN("4k3/8/8/8/8/8/8/3QK3 w - - 0 1")) > 0, "white to move, white queen up: positive");
}

void testPst() {
    CHECK(pstValue(KNIGHT, WHITE, 4, false) == pstValue(KNIGHT, BLACK, 60, false), "black mirrored via sq^56");
    CHECK(pstValue(KNIGHT, WHITE, 0, false) == -50, "knight a1 = -50");
    CHECK(pstValue(KNIGHT, WHITE, 27, false) == 20, "knight d4 = +20");
    CHECK(pstValue(KNIGHT, WHITE, 28, false) == 20, "knight e4 = +20 (mirrored)");
    CHECK(pstValue(KING, WHITE, 6, false) == 30, "king g1 middlegame = 30");
    CHECK(pstValue(KING, WHITE, 27, true) == 40, "king d4 endgame = 40");
    for (int sq = 0; sq < 64; ++sq)
        for (int pt = PAWN; pt <= KING; ++pt)
            CHECK(pstValue(static_cast<PieceType>(pt), WHITE, sq, false) ==
                  pstValue(static_cast<PieceType>(pt), WHITE, (sq & ~7) | (7 - (sq & 7)), false), "pst left-right symmetric");
}

void testPawnStructure() {
    // Isolated + doubled: white pawns a2,a3 (doubled, isolated), black king/white king only.
    const EvalTerms t = terms("4k3/8/8/8/8/P7/P7/4K3 w - - 0 1");
    // a2: isolated, not passed? black has no pawns -> both passed. a2 rel rank 1, a3 rel rank 2.
    const int expected = -DOUBLED_PAWN - 2 * ISOLATED_PAWN + PASSED_PAWN[1] + PASSED_PAWN[2];
    CHECK(t.pawns == expected, "doubled isolated passed pawns exact: got " + std::to_string(t.pawns));
    // Non-isolated, blocked by enemy pawn on same file: no passed bonus.
    const EvalTerms u = terms("4k3/p7/8/8/8/8/P7/4K3 w - - 0 1");
    CHECK(u.pawns == 0, "opposed lone pawns: isolated penalties cancel, none passed");
    // Passed pawn on rank 6 for white, relative rank 5.
    CHECK(terms("4k3/8/3P4/8/8/8/8/4K3 w - - 0 1").pawns == PASSED_PAWN[5] - ISOLATED_PAWN, "passed d6");
    // Black passed pawn on rank 3 has relative rank 5 as well.
    CHECK(terms("4k3/8/8/8/8/3p4/8/4K3 w - - 0 1").pawns == -(PASSED_PAWN[5] - ISOLATED_PAWN), "black passed d3");
    // Neighbour pawn on adjacent file removes isolation; enemy pawn on adjacent file ahead stops passing.
    CHECK(terms("4k3/2p5/8/8/8/8/3P4/4K3 w - - 0 1").pawns == 0, "adjacent enemy pawn ahead: not passed (both isolated)");
}

void testBishopPair() {
    CHECK(terms("4k3/8/8/8/8/8/8/2B1KB2 w - - 0 1").bishopPair == BISHOP_PAIR, "white bishop pair");
    CHECK(terms("2b1kb2/8/8/8/8/8/8/4K3 w - - 0 1").bishopPair == -BISHOP_PAIR, "black bishop pair");
    CHECK(terms("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1").bishopPair == 0, "single bishop");
}

void testMobility() {
    // Lone white knight d4: 8 squares, no pawns, own king far away.
    CHECK(terms("7k/8/8/8/3N4/8/8/K7 w - - 0 1").mobility == 8 * MOBILITY_KNIGHT, "knight d4 mobility 8");
    // Knight a1: 2 squares (b3, c2). King on h8 not interfering.
    CHECK(terms("7k/8/8/8/8/8/8/N6K w - - 0 1").mobility == 2 * MOBILITY_KNIGHT, "knight a1 mobility 2");
    // Rook a1 alone on an otherwise empty board with kings at e1? use kings out of lines: Ke3, kh8... rook a1: 7 + 7.
    CHECK(terms("7k/8/8/8/8/4K3/8/R7 w - - 0 1").mobility == 14 * MOBILITY_ROOK, "rook a1 mobility 14");
    // Squares attacked by enemy pawns are excluded: knight d4, black pawns on c6 and e6 hit b5,d5,f5... c6 hits b5,d5; e6 hits d5,f5.
    CHECK(terms("7k/8/2p1p3/8/3N4/8/8/K7 w - - 0 1").mobility ==
          (6 * MOBILITY_KNIGHT) - /*black pawns have no mobility*/ 0, "pawn-attacked squares excluded");
}

void testKingSafety() {
    // White king g1, pawns f2 g2 h2: 3 near. Black has king alone on e8 (shield: no pawns -> 3 missing).
    const EvalTerms a = terms("rnbqkbnr/8/8/8/8/8/5PPP/6K1 w - - 0 1");
    CHECK(a.kingSafety == 3 * SHIELD_NEAR - 3 * SHIELD_MISSING, "shield near vs missing (middlegame)");
    // Pawn two ranks ahead counts as far.
    CHECK(terms("rnbqkbnr/8/8/8/8/5P2/6P1/6K1 w - - 0 1").kingSafety >= -1000, "sanity");
    // Endgame: king safety off.
    const std::string eg = "4k3/8/8/8/8/8/5PPP/6K1 w - - 0 1";
    CHECK(isEndgame(fromFEN(eg)) && terms(eg).kingSafety == 0, "no king safety in endgame");
}

void testPhase() {
    CHECK(isEndgame(fromFEN("4k3/pppp4/8/8/8/8/PPPP4/4K3 w - - 0 1")), "no queens: endgame");
    CHECK(!isEndgame(fromFEN("r2qk3/8/8/8/8/8/8/4K3 w - - 0 1")), "queen + rook side: middlegame");
    CHECK(isEndgame(fromFEN("3qk3/8/8/8/8/8/8/3QK3 w - - 0 1")), "queens only: endgame");
    CHECK(isEndgame(fromFEN("2bqk3/8/8/8/8/8/8/3QK3 w - - 0 1")), "queen + one minor: endgame");
    CHECK(!isEndgame(fromFEN("1nbqk3/8/8/8/8/8/8/3QK3 w - - 0 1")), "queen + two minors: middlegame");
}

void testSymmetry(const std::vector<std::string>& fens) {
    int bad = 0, badLR = 0;
    for (const auto& fen : fens) {
        const int e = evaluate(fromFEN(fen));
        if (evaluate(fromFEN(mirrorFEN(fen))) != e) { ++bad; if (bad < 4) std::cout << "colour asym: " << fen << "\n"; }
        if (evaluate(fromFEN(flipFilesFEN(fen))) != e) { ++badLR; if (badLR < 4) std::cout << "LR asym: " << fen << "\n"; }
    }
    CHECK(bad == 0, "colour symmetry on " + std::to_string(fens.size()) + " positions, bad=" + std::to_string(bad));
    CHECK(badLR == 0, "left-right symmetry, bad=" + std::to_string(badLR));
}

void testIndependence(const std::vector<std::string>& fens) {
    int bad = 0, outOfRange = 0;
    for (const auto& fen : fens) {
        Position p = fromFEN(fen);
        const int e = evaluate(p);
        Position q = fromFEN(fen);
        q.setCastlingRights(static_cast<CastlingRights>(0));
        q.setEnPassantSquare(NO_SQUARE);
        q.setHalfmoveClock(0);
        if (evaluate(q) != e) ++bad;
        if (e > 20000 || e < -20000) ++outOfRange;
    }
    CHECK(bad == 0, "eval ignores castling/ep/clocks");
    CHECK(outOfRange == 0, "eval bounded, no mate-score magnitudes");
}

void testKinglessGuard() {
    Position p; std::string err;
    p.clear();
    CHECK(evaluate(p) == 0, "empty board evaluates to 0, no crash");
}

void testSpeed(const std::vector<std::string>& fens) {
    std::vector<Position> ps;
    for (const auto& f : fens) ps.push_back(fromFEN(f));
    volatile int sink = 0;
    const int reps = 2000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) for (const auto& p : ps) sink = sink + evaluate(p);
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / (double(reps) * ps.size());
    std::cout << "Eval cost: " << ns << " ns/call over " << ps.size() << " positions\n";
    (void)sink;
}

int main() {
    initAttackTables();
    const auto fens = samplePositions();
    testBasics(); testMaterial(); testPerspective(); testPst(); testPawnStructure();
    testBishopPair(); testMobility(); testKingSafety(); testPhase();
    testSymmetry(fens); testIndependence(fens); testKinglessGuard(); testSpeed(fens);
    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed\n";
    return testsFailed == 0 ? 0 : 1;
}