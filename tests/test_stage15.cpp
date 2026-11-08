// Stage 15 tests: iterative deepening, time limits, interruption.
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/eval/evaluate.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/suite.hpp"
#include "../src/search/search.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int testsRun = 0, testsFailed = 0;
#define CHECK(cond, msg) do { ++testsRun; if (!(cond)) { ++testsFailed; std::cout << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; } } while (0)

static const char* KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
static const char* MIDDLEGAME = "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10";

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) { std::cout << "SETUP FAILED: " << fen << ": " << err << "\n"; ++testsFailed; }
    return pos;
}

static bool isLegal(const Position& p, const Move& m) {
    MoveList ml; generateLegalMoves(p, ml);
    for (size_t i = 0; i < ml.size(); ++i) if (ml[i] == m) return true;
    return false;
}

static bool pvIsLegal(const std::string& fen, const std::vector<Move>& pv) {
    Position walk = fromFEN(fen);
    for (const Move& m : pv) {
        if (!isLegal(walk, m)) return false;
        makeMove(walk, m);
    }
    return true;
}

static std::vector<std::string> positions() {
    std::vector<std::string> v;
    for (const auto& c : standardPerftSuite()) v.push_back(c.fen);
    uint64_t rng = 0x0F1E2D3C4B5A6978ULL;
    for (int game = 0; game < 20; ++game) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 100; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            makeMove(pos, legal[(rng >> 33) % legal.size()]);
            if (ply % 10 == 5) v.push_back(toFEN(pos));
        }
    }
    return v;
}

void testSameScoreAsFixedDepth() {
    const auto fens = positions();
    int bad = 0, n = 0;
    for (size_t k = 0; k < fens.size(); k += 2, ++n) {
        for (int d = 1; d <= 4; ++d) {
            Position a = fromFEN(fens[k]), b = fromFEN(fens[k]);
            SearchLimits lim; lim.maxDepth = d;
            const SearchResult id = searchIterative(a, lim);
            const SearchResult fixed = search(b, d);
            // A mate found earlier stops iterating early: then the score is the same mate score at lower depth.
            const bool ok = id.score == fixed.score && id.hasMove == fixed.hasMove && (id.depth == d || isMateScore(id.score) || !id.hasMove) &&
                            (!id.hasMove || isLegal(fromFEN(fens[k]), id.bestMove));
            if (!ok) { ++bad; if (bad < 4) std::cout << "  differs depth " << d << ": " << fens[k] << " id=" << id.score << " fixed=" << fixed.score << "\n"; }
        }
    }
    CHECK(bad == 0, "iterative deepening gives the same score as fixed depth (depths 1-4, " + std::to_string(n) + " positions), bad=" + std::to_string(bad));
}

void testInfoCallback() {
    Position p = fromFEN(MIDDLEGAME);
    SearchLimits lim; lim.maxDepth = 5;
    std::vector<SearchInfo> infos;
    const SearchResult r = searchIterative(p, lim, [&](const SearchInfo& i) { infos.push_back(i); });
    CHECK(infos.size() == 5, "one info line per completed iteration");
    bool ascending = true, pvsLegal = true, nodesGrow = true;
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].depth != static_cast<int>(i) + 1) ascending = false;
        if (infos[i].pv.empty() || !pvIsLegal(MIDDLEGAME, infos[i].pv)) pvsLegal = false;
        if (i > 0 && infos[i].nodes < infos[i - 1].nodes) nodesGrow = false;
    }
    CHECK(ascending && pvsLegal && nodesGrow, "info depths ascend 1..5, PVs are legal, node counts are cumulative");
    CHECK(r.depth == 5 && !r.interrupted && r.pv.size() >= 1 && r.pv[0] == r.bestMove, "final result matches the last iteration");
    CHECK(infos.back().score == r.score, "last info score equals result score");
}

void testTimeLimit() {
    for (long ms : {1L, 20L, 100L}) {
        Position p = fromFEN(KIWIPETE);
        const Position before = p;
        SearchLimits lim; lim.movetimeMs = ms;
        const auto t0 = std::chrono::steady_clock::now();
        const SearchResult r = searchIterative(p, lim);
        const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(r.hasMove && isLegal(before, r.bestMove), "time-limited search returns a legal move, movetime " + std::to_string(ms));
        CHECK(r.depth >= 0 && r.interrupted, "movetime " + std::to_string(ms) + ": reported as interrupted");
        CHECK(wall < static_cast<double>(ms) + 100.0, "movetime " + std::to_string(ms) + " ms finished in " + std::to_string(wall) + " ms");
        CHECK(p == before, "position restored after a timed search, movetime " + std::to_string(ms));
        CHECK(pvIsLegal(KIWIPETE, r.pv), "PV legal after interruption");
        std::cout << "  movetime " << ms << " ms: wall " << wall << " ms, depth " << r.depth << ", nodes " << r.nodes << "\n";
    }
}

void testNodeLimit() {
    const uint64_t limit = 20000;
    Position a = fromFEN(KIWIPETE), b = fromFEN(KIWIPETE);
    SearchLimits lim; lim.maxNodes = limit;
    const SearchResult r1 = searchIterative(a, lim), r2 = searchIterative(b, lim);
    CHECK(r1.nodes <= limit + 1, "node limit respected: " + std::to_string(r1.nodes));
    CHECK(r1.nodes == r2.nodes && r1.bestMove == r2.bestMove && r1.score == r2.score && r1.depth == r2.depth, "node-limited search is deterministic");
    CHECK(r1.hasMove && r1.interrupted, "node-limited search interrupted with a move");

    // The reported score must be the exact score of the last COMPLETED depth: an unfinished iteration is discarded.
    int bad = 0, n = 0;
    for (const char* f : {KIWIPETE, MIDDLEGAME, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"}) {
        for (uint64_t nl : {200ULL, 1000ULL, 4000ULL, 15000ULL, 60000ULL}) {
            Position p = fromFEN(f), q = fromFEN(f);
            SearchLimits l; l.maxNodes = nl;
            const SearchResult r = searchIterative(p, l);
            const SearchResult fixed = search(q, r.depth);
            ++n;
            if (r.depth == 0) {  // fallback: legal move and the static evaluation
                Position e = fromFEN(f);
                if (!(r.hasMove && isLegal(e, r.bestMove) && r.score == evaluate(e))) { ++bad; std::cout << "  bad fallback\n"; }
                continue;
            }
            if (r.score != fixed.score) { ++bad; std::cout << "  partial result leaked: " << f << " nodes " << nl << " depth " << r.depth << "\n"; }
        }
    }
    CHECK(bad == 0, "interrupted search reports the exact score of its last completed depth (" + std::to_string(n) + " runs)");
}

void testStopFlag() {
    std::atomic<bool> stop{true};
    Position p = fromFEN(KIWIPETE);
    const Position before = p;
    SearchLimits lim; lim.stop = &stop;
    const SearchResult r = searchIterative(p, lim);
    CHECK(r.hasMove && r.depth <= 1 && r.interrupted && isLegal(before, r.bestMove), "stop flag already set: still returns a legal move (depth 0 fallback or depth 1)");
    CHECK(p == before, "position restored");

    // Stop requested from the info callback after depth 3 (deterministic stand-in for another thread).
    std::atomic<bool> stop2{false};
    Position q = fromFEN(MIDDLEGAME);
    SearchLimits l2; l2.stop = &stop2;
    const SearchResult r2 = searchIterative(q, l2, [&](const SearchInfo& i) { if (i.depth == 3) stop2 = true; });
    Position ref = fromFEN(MIDDLEGAME);
    CHECK(r2.depth == 3 && r2.interrupted && r2.score == search(ref, 3).score, "stop after depth 3 returns the depth-3 result");
}

void testMateStopsEarly() {
    Position a = fromFEN("7k/8/5K2/8/8/8/8/5Q2 w - - 0 1");
    SearchLimits lim; lim.maxDepth = 20;
    const SearchResult r = searchIterative(a, lim);
    CHECK(r.score == MATE_SCORE - 3 && r.depth == 3 && !r.interrupted, "mate in 2 found at depth 3, search stops there");
    CHECK(r.bestMove.toString() != "f1f7", "mate line avoids the stalemate");

    Position b = fromFEN("r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1");
    const SearchResult r1 = searchIterative(b, lim);
    CHECK(r1.score == MATE_SCORE - 1 && r1.depth == 1 && r1.bestMove.toString() == "h5f7", "mate in 1 found at depth 1");

    Position c = fromFEN("7k/8/5K2/8/8/8/8/6Q1 b - - 0 1");
    const SearchResult r2 = searchIterative(c, lim);
    CHECK(r2.score == -(MATE_SCORE - 2) && r2.depth == 2, "being mated in 1 is reported and the search stops");
}

void testTerminalRoots() {
    Position m = fromFEN("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    SearchLimits lim; lim.maxDepth = 6;
    SearchResult r = searchIterative(m, lim);
    CHECK(!r.hasMove && r.score == -MATE_SCORE, "checkmated root: no move, mate score");
    Position s = fromFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    r = searchIterative(s, lim);
    CHECK(!r.hasMove && r.score == 0, "stalemate root: no move, score 0");
    Position c = fromFEN("4k3/8/8/8/8/8/8/3QK3 w - - 120 80");
    r = searchIterative(c, lim);
    CHECK(r.hasMove, "fifty-move claim at the root still returns a move");
}

void testFallbackMove() {
    // A pathological position (many promotions) with a 1 ms budget: whatever happens, a legal move comes back fast.
    const char* fens[] = {"4k3/P1P1P3/8/8/8/8/p1p1p3/4K3 w - - 0 1", KIWIPETE, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"};
    for (const char* f : fens) {
        Position p = fromFEN(f);
        const Position before = p;
        SearchLimits lim; lim.movetimeMs = 1;
        const auto t0 = std::chrono::steady_clock::now();
        const SearchResult r = searchIterative(p, lim);
        const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(r.hasMove && isLegal(before, r.bestMove), std::string("1 ms: legal move returned for ") + f);
        CHECK(wall < 100.0, std::string("1 ms budget: returned in ") + std::to_string(wall) + " ms");
        CHECK(p == before, "position restored");
    }
    // Zero-time style limits via an already-set stop flag on a large tree.
    std::atomic<bool> stop{true};
    Position p = fromFEN(fens[0]);
    SearchLimits lim; lim.stop = &stop;
    const SearchResult r = searchIterative(p, lim);
    CHECK(r.hasMove && r.interrupted, "stop flag on a promotion-heavy position still yields a move");
}

void testAllocateTime() {
    CHECK(allocateTimeMs(60000, 0) == 2000, "60 s left, no increment: 2 s");
    CHECK(allocateTimeMs(60000, 1000) == 2750, "increment adds 3/4 of itself");
    CHECK(allocateTimeMs(60000, 0, 10) == 5454, "10 moves to go: 1/11 of the clock");
    CHECK(allocateTimeMs(100, 0) >= 1 && allocateTimeMs(100, 0) < 100, "never the whole clock");
    CHECK(allocateTimeMs(1000, 5000) <= 980, "huge increment is capped below the remaining time");
    CHECK(allocateTimeMs(0, 0) >= 1 && allocateTimeMs(-5, 0) >= 1, "always at least 1 ms");
}

void testDepthReached() {
    // Reports (no assertion on speed): what a fixed time buys, and iterative deepening vs fixed depth cost.
    for (long ms : {50L, 200L, 1000L}) {
        Position p = fromFEN(MIDDLEGAME);
        SearchLimits lim; lim.movetimeMs = ms;
        const SearchResult r = searchIterative(p, lim);
        std::cout << "  middlegame, " << ms << " ms: depth " << r.depth << ", " << r.nodes << " nodes, best " << r.bestMove.toString() << "\n";
    }
    Position a = fromFEN(MIDDLEGAME), b = fromFEN(MIDDLEGAME);
    SearchLimits lim; lim.maxDepth = 5;
    const SearchResult id = searchIterative(a, lim);
    const SearchResult fixed = search(b, 5);
    std::cout << "  depth 5 middlegame: iterative " << id.nodes << " nodes (" << id.timeMs << " ms), fixed " << fixed.nodes << " nodes\n";
    CHECK(id.score == fixed.score, "depth 5 scores agree");
}

int main() {
    initAttackTables();
    testSameScoreAsFixedDepth(); testInfoCallback(); testTimeLimit(); testNodeLimit(); testStopFlag(); testFallbackMove();
    testMateStopsEarly(); testTerminalRoots(); testAllocateTime(); testDepthReached();
    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed\n";
    return testsFailed == 0 ? 0 : 1;
}