// Stage 14 tests: move ordering. Ordering may change node counts and tie-breaks, but never the score.
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
#include <iterator>
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

// Configurations without captures-first are as slow as no ordering, so they only run at shallow depth.
static const unsigned ALL_CONFIGS[] = {ORDER_NONE, ORDER_CAPTURES, ORDER_CAPTURES | ORDER_KILLERS,
                                       ORDER_CAPTURES | ORDER_HISTORY, ORDER_DEFAULT,
                                       ORDER_KILLERS, ORDER_HISTORY, ORDER_KILLERS | ORDER_HISTORY};
static const unsigned FAST_CONFIGS[] = {ORDER_CAPTURES, ORDER_CAPTURES | ORDER_KILLERS,
                                        ORDER_CAPTURES | ORDER_HISTORY, ORDER_DEFAULT};

static std::vector<std::string> randomPositions() {
    std::vector<std::string> v;
    uint64_t rng = 0xFEEDFACECAFEBEEFULL;
    for (int game = 0; game < 30; ++game) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 120; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            makeMove(pos, legal[(rng >> 33) % legal.size()]);
            if (ply % 8 == 3) v.push_back(toFEN(pos));
        }
    }
    return v;
}

static bool pvIsLegal(const std::string& fen, const SearchResult& r) {
    Position walk = fromFEN(fen);
    for (const Move& m : r.pv) {
        MoveList ml; generateLegalMoves(walk, ml);
        bool found = false;
        for (size_t i = 0; i < ml.size(); ++i) if (ml[i] == m) { found = true; break; }
        if (!found) return false;
        makeMove(walk, m);
    }
    return true;
}

// Every ordering configuration must return the same score as no ordering at all.
static int scoreMismatches(const std::vector<std::string>& fens, int depth, bool* anyIllegal, bool fast = false) {
    int bad = 0;
    for (const auto& f : fens) {
        Position p0 = fromFEN(f);
        const int ref = searchOrdered(p0, depth, fast ? ORDER_CAPTURES : ORDER_NONE).score;
        for (unsigned flags : (fast ? std::vector<unsigned>(std::begin(FAST_CONFIGS), std::end(FAST_CONFIGS))
                                    : std::vector<unsigned>(std::begin(ALL_CONFIGS), std::end(ALL_CONFIGS)))) {
            Position p = fromFEN(f);
            const Position before = p;
            const SearchResult r = searchOrdered(p, depth, flags);
            if (r.score != ref || !(p == before)) {
                ++bad;
                if (bad < 4) std::cout << "  mismatch flags=" << flags << " depth=" << depth << " " << f << " got " << r.score << " want " << ref << "\n";
            }
            if (r.hasMove && !pvIsLegal(f, r)) *anyIllegal = true;
        }
    }
    return bad;
}

void testScoresIndependentOfOrdering() {
    bool illegal = false;
    std::vector<std::string> suite;
    for (const auto& c : standardPerftSuite()) suite.push_back(c.fen);
    CHECK(scoreMismatches(suite, 1, &illegal) == 0, "all orderings give the same score on the perft suite, depth 1");
    CHECK(scoreMismatches(suite, 2, &illegal) == 0, "all orderings give the same score on the perft suite, depth 2");
    const auto rnd = randomPositions();
    auto every = [&](size_t step) { std::vector<std::string> v; for (size_t i = 0; i < rnd.size(); i += step) v.push_back(rnd[i]); return v; };
    CHECK(scoreMismatches(rnd, 1, &illegal) == 0, "all 8 orderings give the same score on " + std::to_string(rnd.size()) + " random positions, depth 1");
    const auto twelfth = every(40);
    CHECK(scoreMismatches(twelfth, 2, &illegal) == 0, "all 8 orderings give the same score on " + std::to_string(twelfth.size()) + " random positions, depth 2");
    const auto fast3 = every(12), fast4 = every(40), fast5 = every(110);
    CHECK(scoreMismatches(fast3, 3, &illegal, true) == 0, "captures-first orderings agree on " + std::to_string(fast3.size()) + " random positions, depth 3");
    CHECK(scoreMismatches(fast4, 4, &illegal, true) == 0, "captures-first orderings agree on " + std::to_string(fast4.size()) + " random positions, depth 4");
    CHECK(scoreMismatches(fast5, 5, &illegal, true) == 0, "captures-first orderings agree on " + std::to_string(fast5.size()) + " random positions, depth 5");
    CHECK(!illegal, "principal variation is legal for every ordering");
}

void testNoneMatchesStage13() {
    // ORDER_NONE must reproduce the Stage 13 search exactly: same score, move, PV, node count.
    const char* fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"};
    const uint64_t expected[] = {44672, 61035};  // depth 4 alpha-beta + quiescence nodes recorded in Stage 13
    for (int i = 0; i < 2; ++i) {
        Position p = fromFEN(fens[i]);
        CHECK(searchOrdered(p, 4, ORDER_NONE).nodes == expected[i], std::string("ORDER_NONE reproduces Stage 13 node count: ") + fens[i]);
    }
}

void testNodeReduction() {
    const char* fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
                          "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10",
                          "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7"};
    uint64_t none = 0, cap = 0, capKill = 0, all = 0;
    for (const char* f : fens) {
        Position a = fromFEN(f), b = fromFEN(f), c = fromFEN(f), d = fromFEN(f);
        none += searchOrdered(a, 3, ORDER_NONE).nodes;
        cap += searchOrdered(b, 3, ORDER_CAPTURES).nodes;
        capKill += searchOrdered(c, 3, ORDER_CAPTURES | ORDER_KILLERS).nodes;
        all += searchOrdered(d, 3, ORDER_DEFAULT).nodes;
    }
    std::cout << "depth 3 totals: none " << none << ", +captures " << cap << ", +killers " << capKill << ", all " << all << "\n";
    CHECK(cap < none / 4, "MVV-LVA ordering cuts nodes by more than 4x");
    CHECK(capKill < cap, "killers reduce nodes further");
    CHECK(all <= capKill + capKill / 20, "history does not make things worse by more than 5%");
}

void testMatesAndDeterminism() {
    struct Case { const char* fen; int depth; int score; const char* move; };
    const Case cases[] = {
        {"r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1", 2, MATE_SCORE - 1, "h5f7"},
        {"6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", 3, MATE_SCORE - 1, "a1a8"},
        {"rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2", 3, MATE_SCORE - 1, "d8h4"},
        {"7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", 5, MATE_SCORE - 3, nullptr},
    };
    for (const Case& c : cases) {
        Position p = fromFEN(c.fen);
        const SearchResult r = search(p, c.depth);
        CHECK(r.score == c.score, std::string("mate score preserved with ordering: ") + c.fen);
        if (c.move) CHECK(r.bestMove.toString() == c.move, std::string("mate move: ") + c.move);
        else CHECK(r.bestMove.toString() != "f1f7", "still avoids stalemate");
    }
    // Tables are reset per search: two identical searches behave identically.
    Position a = fromFEN("r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10");
    Position b = a;
    const SearchResult r1 = search(a, 4), r2 = search(b, 4);
    CHECK(r1.nodes == r2.nodes && r1.score == r2.score && r1.bestMove == r2.bestMove, "searches are deterministic (no state leaks between calls)");
}

void testColourSymmetry() {
    const char* fens[] = {"r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"};
    for (const char* f : fens) {
        Position a = fromFEN(f), b = fromFEN(mirrorFEN(f));
        CHECK(search(a, 4).score == search(b, 4).score, std::string("mirrored position, same score: ") + f);
    }
}

void testDeepSearchStability() {
    // Deeper middlegame search: legal PV, and the same score with and without killers/history.
    // (History aging was exercised separately by running this whole test file with HISTORY_MAX lowered to 300.)
    const std::string f = "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10";
    Position a = fromFEN(f), b = fromFEN(f);
    const SearchResult full = search(a, 5);
    const SearchResult cap = searchOrdered(b, 5, ORDER_CAPTURES);
    CHECK(full.hasMove && pvIsLegal(f, full), "depth-5 search returns a legal PV");
    CHECK(full.score == cap.score, "depth-5 score identical with and without killers/history");
    std::cout << "depth 5 default ordering: " << full.nodes << " nodes, score " << full.score << ", pv";
    for (const Move& m : full.pv) std::cout << ' ' << m.toString();
    std::cout << "\n";
}

int main() {
    initAttackTables();
    auto timed = [](const char* name, void (*fn)()) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        std::cout << "[" << name << ": " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s]\n";
        std::cout.flush();
    };
    timed("scores", testScoresIndependentOfOrdering); timed("none==stage13", testNoneMatchesStage13);
    timed("reduction", testNodeReduction); timed("mates", testMatesAndDeterminism);
    timed("symmetry", testColourSymmetry); timed("deep", testDeepSearchStability);
    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed\n";
    return testsFailed == 0 ? 0 : 1;
}