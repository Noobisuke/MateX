// Stage 12 tests: alpha-beta must agree exactly with plain minimax, and visit fewer nodes.
#include "mirror.hpp"
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/suite.hpp"
#include "../src/search/search.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int testsRun = 0, testsFailed = 0;
#define CHECK(cond, msg) do { ++testsRun; if (!(cond)) { ++testsFailed; std::cout << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; } } while (0)

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) { std::cout << "SETUP FAILED: " << fen << ": " << err << "\n"; ++testsFailed; }
    return pos;
}

static bool samePV(const SearchResult& a, const SearchResult& b) {
    if (a.pv.size() != b.pv.size()) return false;
    for (size_t i = 0; i < a.pv.size(); ++i) if (!(a.pv[i] == b.pv[i])) return false;
    return true;
}

// Returns true when everything matches; prints the first few differences.
static bool agree(const std::string& fen, int depth, uint64_t* abNodes = nullptr, uint64_t* mmNodes = nullptr) {
    Position a = fromFEN(fen), b = fromFEN(fen);
    const Position before = a;
    const SearchResult ab = searchAlphaBeta(a, depth);
    const SearchResult mm = searchMinimax(b, depth);
    if (abNodes) *abNodes += ab.nodes;
    if (mmNodes) *mmNodes += mm.nodes;
    const bool ok = ab.score == mm.score && ab.hasMove == mm.hasMove &&
                    (!ab.hasMove || ab.bestMove == mm.bestMove) && samePV(ab, mm) && a == before &&
                    ab.nodes <= mm.nodes;
    if (!ok) std::cout << "  differs at depth " << depth << ": " << fen << "  ab=" << ab.score << " mm=" << mm.score << "\n";
    return ok;
}

static std::vector<std::string> positions() {
    std::vector<std::string> v;
    for (const auto& c : standardPerftSuite()) v.push_back(c.fen);
    uint64_t rng = 0x1234567887654321ULL;
    for (int game = 0; game < 25; ++game) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 120; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            makeMove(pos, legal[(rng >> 33) % legal.size()]);
            if (ply % 9 == 4) v.push_back(toFEN(pos));
        }
    }
    return v;
}

void testAgreesWithMinimax() {
    const auto fens = positions();
    for (int depth = 1; depth <= 3; ++depth) {
        int bad = 0;
        const size_t step = depth == 3 ? 2 : 1;  // depth 3 on every second position keeps the run short
        for (size_t i = 0; i < fens.size(); i += step) if (!agree(fens[i], depth)) ++bad;
        CHECK(bad == 0, "alpha-beta == minimax (score, best move, PV) at depth " + std::to_string(depth) + " on " +
                         std::to_string(fens.size()) + " positions, bad=" + std::to_string(bad));
    }
    // Deeper, on fewer positions (minimax is slow).
    int bad = 0, n = 0;
    for (size_t i = standardPerftSuite().size(); i < fens.size(); i += 70, ++n) if (!agree(fens[i], 4)) ++bad;
    CHECK(bad == 0, "alpha-beta == minimax at depth 4 on " + std::to_string(n) + " positions, bad=" + std::to_string(bad));
}

void testMatesAndDraws() {
    const char* fens[] = {"r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1",
                          "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", "7k/8/5K2/8/8/8/8/6Q1 b - - 0 1",
                          "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",
                          "4k3/8/8/8/8/8/8/3QK3 w - - 99 80", "6k1/5ppp/8/8/8/8/8/R3K3 w - - 100 80"};
    int bad = 0;
    for (const char* f : fens) {
        const int maxDepth = f == fens[0] ? 3 : 5;  // the full-board position is too big for plain minimax at depth 5
        for (int d = 1; d <= maxDepth; ++d) if (!agree(f, d)) ++bad;
    }
    CHECK(bad == 0, "mate / stalemate / draw positions agree at all tested depths");
    Position p = fromFEN("7k/8/5K2/8/8/8/8/5Q2 w - - 0 1");
    CHECK(searchAlphaBeta(p, 5).score == MATE_SCORE - 3, "alpha-beta finds shortest mate in 2");
}

void testColourSymmetry() {
    const char* fens[] = {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"};
    for (const char* f : fens) {
        Position a = fromFEN(f), b = fromFEN(mirrorFEN(f));
        CHECK(searchAlphaBeta(a, 4).score == searchAlphaBeta(b, 4).score, std::string("mirrored position, same alpha-beta score: ") + f);
    }
}

void testRootClaimStillMoves() {
    Position p = fromFEN("4k3/8/8/8/8/8/8/3QK3 w - - 100 80");
    CHECK(searchAlphaBeta(p, 3).hasMove, "root with a fifty-move claim still returns a move (alpha-beta)");
}

void testNodeSavings() {
    const char* fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"};
    std::cout << "position                depth   minimax nodes   alpha-beta nodes   ratio\n";
    uint64_t totalAB = 0, totalMM = 0;
    for (const char* f : fens) {
        for (int d = 2; d <= 4; ++d) {
            Position a = fromFEN(f), b = fromFEN(f);
            const SearchResult ab = searchAlphaBeta(a, d), mm = searchMinimax(b, d);
            totalAB += ab.nodes; totalMM += mm.nodes;
            std::cout << "  " << std::string(f).substr(0, 18) << "...  " << d << "   " << mm.nodes << "   " << ab.nodes
                      << "   " << static_cast<double>(mm.nodes) / static_cast<double>(ab.nodes) << "x\n";
        }
    }
    CHECK(totalAB < totalMM, "alpha-beta visits fewer nodes overall");
    CHECK(totalMM > 3 * totalAB, "alpha-beta cuts the node count by more than 3x on this set (no move ordering yet)");
}

int main() {
    initAttackTables();
    auto timed = [](const char* name, void (*fn)()) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        std::cout << "[" << name << ": " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s]\n";
    };
    timed("agrees", testAgreesWithMinimax); timed("mates", testMatesAndDraws); timed("symmetry", testColourSymmetry);
    timed("root claim", testRootClaimStillMoves); timed("savings", testNodeSavings);
    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed\n";
    return testsFailed == 0 ? 0 : 1;
}