// Stage 9 tests: perft and complete rule verification
//
// Reference data was produced independently with python-chess 1.11.2 (and, for the standard
// positions, agrees with the published perft tables). Nothing here was derived from this engine.

#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/perft.hpp"
#include "../src/perft/suite.hpp"
#include "../src/rules/validate.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do {     ++testsRun;     if (!(cond)) {         ++testsFailed;         std::cout << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";     } } while (0)

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) {
        std::cout << "TEST SETUP FAILED to load FEN '" << fen << "': " << err << "\n";
    }
    return pos;
}

static std::string str(uint64_t n) { return std::to_string(n); }

// --- The suite ---------------------------------------------------------------------------------------

void testStandardSuite() {
    const PerftSuiteResult r = runPerftSuite(99, &std::cout);
    CHECK(r.mismatches == 0, "perft suite: no mismatches (" + str(r.mismatches) + ")");
    CHECK(r.positions == static_cast<int>(standardPerftSuite().size()), "every suite position loaded and validated");
    CHECK(r.checks >= 150, "the suite compared at least 150 counts (" + str(r.checks) + ")");
    CHECK(r.passed(), "suite reports passed");
    for (const PerftCase& c : standardPerftSuite()) {
        Position pos = fromFEN(c.fen);
        std::string why;
        CHECK(validatePosition(pos, &why), "suite position is valid: " + c.name + " " + why);
    }
}

void testSuiteCoversTheRequiredAreas() {
    // The stage requires: start position, castling, en passant, promotions, checks, pins,
    // king movement, captures, complex tactical positions. Check the suite really contains them.
    bool start = false, castling = false, ep = false, promo = false, pins = false, check = false, tactical = false;
    for (const PerftCase& c : standardPerftSuite()) {
        const std::string& n = c.name;
        start |= (n == "start");
        castling |= n.find("castling") != std::string::npos || n.find("castle") != std::string::npos;
        ep |= n.find("ep") != std::string::npos;
        promo |= n.find("promotion") != std::string::npos;
        pins |= n.find("pin") != std::string::npos;
        check |= n.find("check") != std::string::npos;
        tactical |= (n == "kiwipete");
    }
    CHECK(start && castling && ep && promo && pins && check && tactical, "suite covers every required area");
}

// --- Random positions (python-chess reference) ---------------------------------------------------------

void testRandomPositionsMatchReference() {
    struct Case { const char* fen; std::vector<uint64_t> nodes; };
    const Case cases[] = {
        {"rn2k2r/pp6/8/3p1NBp/6b1/PP6/6PP/b3KN2 b - - 2 23", {27, 704, 19477, 471696}},
        {"6k1/1p5p/3p2p1/1Nr1P3/8/8/1P3K1P/4R3 b - - 0 28", {20, 542, 10906, 280754}},
        {"3r4/2p3pk/1pP5/4pQ1B/4p3/P2P3P/2P2P1K/R3R3 b - - 0 24", {4, 181, 2884, 125633}},
        {"r4k1r/1p1b1Bpp/2N2b2/8/8/4B1P1/PPP3P1/RN2K3 w Q - 0 16", {42, 1370, 51523, 1611924}},
        {"6n1/1np4r/p2p1k1p/8/5p2/N1b2K1P/8/8 w - - 2 31", {11, 327, 3440, 99709}},
        {"1rq4r/pp4pp/5k2/2P3b1/5N2/6n1/PP1N3P/R1BQK3 b - - 0 19", {37, 1057, 37616, 1123477}},
        {"8/8/b4k2/8/3B1K2/8/8/8 b - - 19 46", {4, 80, 1021, 16160, 214688}},
        {"6rr/1R6/2p1b1kp/p7/8/6P1/P4K1P/2b5 w - - 1 26", {25, 760, 15992, 504310}},
        {"8/8/5b2/2p1k3/5p2/R4q2/2P5/4K3 w - - 0 28", {15, 430, 4817, 140014}},
        {"r5k1/2p4p/3p4/1n2N3/pP2Pr2/P4RP1/7P/R5K1 b - - 1 26", {29, 819, 22747, 638492}},
        {"5k2/5p2/8/2p5/2n3p1/6Pn/7r/2RK2N1 b - - 2 29", {26, 207, 5317, 62339}},
        {"6rr/1p1p1kpp/1b2Nnb1/1q6/B1P4P/3P4/P5P1/R2Q1R1K b - - 0 13", {42, 1576, 61355}},
        {"1n2k2B/2pbpp1n/8/7p/1P5P/4K1PR/4PP2/rN3B2 b - - 0 20", {29, 619, 17844, 387236}},
        {"4k3/3p4/1p2pr2/pN5R/P7/3K4/R5qP/8 b - - 1 32", {39, 885, 27757, 638219}},
        {"4qb2/2pk4/8/1P1pn3/6P1/1P2P3/5P1N/2B1K3 b - - 2 27", {31, 437, 13955, 210608}},
        {"4k3/rp1p1ppp/2p3b1/1P2P3/2P4Q/Pq4PP/1R3K2/R7 b - - 2 14", {35, 1087, 36257, 1134983}},
        {"7k/1P4pp/8/1b6/1PR4P/3P4/6P1/5K2 b - - 2 35", {11, 246, 2605, 56634, 623560}},
        {"8/5k2/2p5/4R1Pp/3P4/P4rP1/8/4K3 b - d3 0 35", {17, 284, 4535, 75421}},
        {"2bk4/7p/p6P/2p5/1p2R3/8/7K/2b5 w - - 0 37", {17, 277, 4295, 77361}},
        {"r2k4/5pp1/p1b2n2/4p3/1p6/3r4/2K5/8 b - - 1 31", {44, 177, 7011, 29674, 1155973}},
        {"3b2k1/5p2/5r2/7p/6p1/6n1/8/K7 w - - 14 38", {3, 78, 425, 12071, 59758}},
        {"3r4/pk1q1pb1/b3p1p1/3nP3/1P1p4/2N2BPQ/P4P1P/3bK2R w K - 0 12", {32, 1289, 40014, 1651086}},
        {"5B2/p7/8/k7/8/1K5p/r6r/8 b - - 0 44", {20, 163, 3246, 31775, 687623}},
        {"rnbqkbnr/p2p1p2/p3p1pp/8/2pP2P1/1QP1P3/PP3P1P/RNB1K1NR b KQkq - 1 7", {28, 795, 22529, 656269}},
        {"r4k1r/Rp1n4/6p1/7p/2B5/5N2/P1PK4/RN6 b - - 0 24", {20, 692, 14684, 478791}},
        {"1nq5/4kpr1/8/6Np/2BQ1nP1/r1p2P1b/PB5P/4K2R w K - 5 22", {42, 1524, 58946, 2179158}},
        {"5k1r/r4b1p/3P4/1P1P2P1/3N2P1/3P4/2n5/5K2 b - - 0 26", {29, 385, 10109, 148790}},
        {"4nr2/1pp1q1pp/r1np1k2/3bp3/4P3/8/1B1N1PPP/4N1K1 w - - 0 21", {23, 829, 18523, 673512}},
        {"5r2/p7/2p1k2p/1p5K/P7/8/1P6/1q6 b - - 1 31", {42, 259, 10428, 59332, 2340334}},
        {"1n2rbnr/1Qp2kp1/3p4/p3p2p/5P2/1P6/P1PB3P/qN1K1B1R w - - 0 17", {40, 1099, 41608, 1216592}},
        {"6r1/p4pp1/2p1k3/6Pp/8/P2K2b1/2P5/8 b - - 4 27", {29, 298, 8383, 76207}},
        {"8/N5k1/6p1/6P1/1P6/7p/P7/1n3K2 b - - 0 30", {9, 81, 790, 7840, 84498}},
        {"8/5k2/3p4/2p1r1pp/8/R2P4/2P2NKP/1r6 w - - 0 29", {22, 726, 15629, 473308}},
        {"r1k3n1/5p2/8/p1rR4/P7/5KPp/7P/7R w - - 1 26", {26, 499, 12153, 263742}},
        {"1N2k3/2Bp4/1p6/1P6/B3p3/P7/b4K2/8 w - - 1 20", {20, 252, 4915, 62077}},
        {"1R3k2/8/7p/2pp3P/3P4/8/4r3/6K1 b - - 0 35", {4, 68, 1138, 16548, 278710}},
        {"8/8/5k2/8/4R3/1P6/P7/4K3 b - - 2 37", {5, 105, 634, 13118, 69402}},
        {"8/3Q4/8/8/3P4/2P4k/8/1N3K2 b - - 6 39", {3, 80, 259, 7220, 27135}},
        {"8/6k1/8/r2p4/7p/R6P/6P1/2N3K1 w - - 3 37", {20, 315, 6180, 100042}},
        {"2rk4/n1p2p2/p4n2/3p2p1/PP1p4/3R4/8/4K3 w - g6 0 36", {17, 315, 5293, 103974}},
    };
    int compared = 0;
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        for (size_t d = 1; d <= c.nodes.size(); ++d) {
            const uint64_t got = perft(pos, static_cast<int>(d));
            ++compared;
            CHECK(got == c.nodes[d - 1], std::string("random position depth ") + str(d) + ": " + c.fen +
                  " expected " + str(c.nodes[d - 1]) + ", got " + str(got));
        }
    }
    CHECK(compared >= 100, "compared at least 100 random-position counts");
}

// --- Leaf breakdown (captures, ep, castles, promotions, checks, mates) ---------------------------------

void testDetailedStatsMatchReference() {
    struct Case { const char* name; const char* fen; int depth; uint64_t v[7]; };
    const Case cases[] = {
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, {20, 0, 0, 0, 0, 0, 0}},
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2, {400, 0, 0, 0, 0, 0, 0}},
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, {8902, 34, 0, 0, 0, 12, 0}},
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, {197281, 1576, 0, 0, 0, 469, 8}},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, {48, 8, 0, 2, 0, 0, 0}},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, {2039, 351, 1, 91, 0, 3, 0}},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, {97862, 17102, 45, 3162, 0, 993, 1}},
        {"pos3 (ep pins, endgame)", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 1, {14, 1, 0, 0, 0, 2, 0}},
        {"pos3 (ep pins, endgame)", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 2, {191, 14, 0, 0, 0, 10, 0}},
        {"pos3 (ep pins, endgame)", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3, {2812, 209, 2, 0, 0, 267, 0}},
        {"pos3 (ep pins, endgame)", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, {43238, 3348, 123, 0, 0, 1680, 17}},
        {"pos4 (promotions, checks)", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 1, {6, 0, 0, 0, 0, 0, 0}},
        {"pos4 (promotions, checks)", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 2, {264, 87, 0, 6, 48, 10, 0}},
        {"pos4 (promotions, checks)", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, {9467, 1021, 4, 0, 120, 38, 22}},
        {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 1, {44, 6, 0, 1, 4, 0, 0}},
        {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 2, {1486, 222, 0, 0, 0, 117, 0}},
        {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, {62379, 8517, 0, 1081, 5068, 1201, 44}},
        {"pos6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 1, {46, 4, 0, 0, 0, 1, 0}},
        {"pos6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 2, {2079, 203, 0, 0, 0, 40, 0}},
    };
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        const PerftStats s = perftDetailed(pos, c.depth);
        const std::string tag = std::string(c.name) + " depth " + str(c.depth) + ": ";
        CHECK(s.nodes == c.v[0], tag + "nodes " + str(s.nodes) + " vs " + str(c.v[0]));
        CHECK(s.captures == c.v[1], tag + "captures " + str(s.captures) + " vs " + str(c.v[1]));
        CHECK(s.enPassant == c.v[2], tag + "en passant " + str(s.enPassant) + " vs " + str(c.v[2]));
        CHECK(s.castles == c.v[3], tag + "castles " + str(s.castles) + " vs " + str(c.v[3]));
        CHECK(s.promotions == c.v[4], tag + "promotions " + str(s.promotions) + " vs " + str(c.v[4]));
        CHECK(s.checks == c.v[5], tag + "checks " + str(s.checks) + " vs " + str(c.v[5]));
        CHECK(s.checkmates == c.v[6], tag + "checkmates " + str(s.checkmates) + " vs " + str(c.v[6]));
    }
}

void testDetailedDepthZero() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const PerftStats s = perftDetailed(pos, 0);
    CHECK(s.nodes == 1 && s.captures == 0 && s.checks == 0 && s.checkmates == 0, "perftDetailed(0): one node, nothing else");
}

// --- divide (python-chess reference) -----------------------------------------------------------------------

void testDivideMatchesReference() {
    struct Entry { const char* uci; uint64_t nodes; };
    struct Case { const char* name; const char* fen; int depth; std::vector<Entry> lines; };
    const Case cases[] = {
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, {{"a2a3", 380}, {"a2a4", 420}, {"b1a3", 400}, {"b1c3", 440}, {"b2b3", 420}, {"b2b4", 421}, {"c2c3", 420}, {"c2c4", 441}, {"d2d3", 539}, {"d2d4", 560}, {"e2e3", 599}, {"e2e4", 600}, {"f2f3", 380}, {"f2f4", 401}, {"g1f3", 440}, {"g1h3", 400}, {"g2g3", 420}, {"g2g4", 421}, {"h2h3", 380}, {"h2h4", 420}}},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, {{"a1b1", 43}, {"a1c1", 43}, {"a1d1", 43}, {"a2a3", 44}, {"a2a4", 44}, {"b2b3", 42}, {"c3a4", 42}, {"c3b1", 42}, {"c3b5", 39}, {"c3d1", 42}, {"d2c1", 43}, {"d2e3", 43}, {"d2f4", 43}, {"d2g5", 42}, {"d2h6", 41}, {"d5d6", 41}, {"d5e6", 46}, {"e1c1", 43}, {"e1d1", 43}, {"e1f1", 43}, {"e1g1", 43}, {"e2a6", 36}, {"e2b5", 39}, {"e2c4", 41}, {"e2d1", 44}, {"e2d3", 42}, {"e2f1", 44}, {"e5c4", 42}, {"e5c6", 41}, {"e5d3", 43}, {"e5d7", 45}, {"e5f7", 44}, {"e5g4", 44}, {"e5g6", 42}, {"f3d3", 42}, {"f3e3", 43}, {"f3f4", 43}, {"f3f5", 45}, {"f3f6", 39}, {"f3g3", 43}, {"f3g4", 43}, {"f3h3", 43}, {"f3h5", 43}, {"g2g3", 42}, {"g2g4", 42}, {"g2h3", 43}, {"h1f1", 43}, {"h1g1", 43}}},
        {"pos3 (ep pins, endgame)", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3, {{"a5a4", 224}, {"a5a6", 240}, {"b4a4", 202}, {"b4b1", 265}, {"b4b2", 205}, {"b4b3", 248}, {"b4c4", 254}, {"b4d4", 243}, {"b4e4", 228}, {"b4f4", 41}, {"e2e3", 205}, {"e2e4", 177}, {"g2g3", 54}, {"g2g4", 226}}},
        {"pos4 (promotions, checks)", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 2, {{"b4c5", 42}, {"c4c5", 43}, {"d2d4", 43}, {"f1f2", 45}, {"f3d4", 45}, {"g1h1", 46}}},
    };
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        const std::vector<DivideEntry> got = divide(pos, c.depth);
        std::map<std::string, uint64_t> mine;
        uint64_t total = 0;
        for (const DivideEntry& e : got) { mine[e.move.toString()] = e.nodes; total += e.nodes; }
        CHECK(got.size() == c.lines.size(), std::string(c.name) + ": same number of root moves (" + str(got.size()) + ")");
        for (const Entry& e : c.lines) {
            auto it = mine.find(e.uci);
            CHECK(it != mine.end() && it->second == e.nodes,
                  std::string(c.name) + " divide " + e.uci + ": expected " + str(e.nodes) +
                  ", got " + (it == mine.end() ? "missing" : str(it->second)));
        }
        CHECK(total == perft(pos, c.depth), std::string(c.name) + ": divide sums to perft");
    }
}

void testDivideEdgeCases() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(divide(pos, 0).empty(), "divide(0) is empty");
    const std::vector<DivideEntry> d1 = divide(pos, 1);
    CHECK(d1.size() == 20, "divide(1): one entry per legal move");
    bool allOne = true;
    for (const DivideEntry& e : d1) allOne &= (e.nodes == 1);
    CHECK(allOne, "divide(1): every entry is exactly 1");
}

// --- Basic perft properties -----------------------------------------------------------------------------

void testPerftBasics() {
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(perft(pos, 0) == 1, "perft(0) == 1");
    CHECK(perft(pos, 1) == 20 && perft(pos, 2) == 400 && perft(pos, 3) == 8902, "start 20 / 400 / 8902");
    Position mate = fromFEN("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    CHECK(perft(mate, 1) == 0 && perft(mate, 3) == 0, "checkmate: no legal moves, perft 0 at every depth");
    Position stale = fromFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(perft(stale, 1) == 0, "stalemate: perft 0");
}

void testPerftRestoresPosition() {
    for (const PerftCase& c : standardPerftSuite()) {
        Position pos = fromFEN(c.fen);
        const Position before = pos;
        perft(pos, 2);
        perftBulk(pos, 2);
        divide(pos, 2);
        perftDetailed(pos, 2);
        CHECK(pos == before, "position unchanged by perft/bulk/divide/detailed: " + c.name);
    }
}

void testBulkEqualsPlain() {
    for (const PerftCase& c : standardPerftSuite()) {
        Position pos = fromFEN(c.fen);
        const int depth = static_cast<int>(std::min<size_t>(c.nodes.size(), 3));
        for (int d = 1; d <= depth; ++d) {
            CHECK(perftBulk(pos, d) == c.nodes[d - 1], "bulk counting matches the reference: " + c.name + " depth " + str(d));
        }
    }
    Position pos = fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(perftBulk(pos, 0) == 1, "perftBulk(0) == 1");
}

// --- Colour symmetry via an independent code path ------------------------------------------------------------

// Mirrors a FEN vertically and swaps colours: the mirrored position must have identical perft.
static std::string mirrorFEN(const std::string& fen) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t i = 0; i <= fen.size(); ++i) {
        if (i == fen.size() || fen[i] == ' ') { fields.push_back(fen.substr(start, i - start)); start = i + 1; }
    }
    auto swapCase = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32)
                                      : (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
    std::vector<std::string> ranks;
    size_t s = 0;
    for (size_t i = 0; i <= fields[0].size(); ++i) {
        if (i == fields[0].size() || fields[0][i] == '/') { ranks.push_back(fields[0].substr(s, i - s)); s = i + 1; }
    }
    std::string placement;
    for (int i = static_cast<int>(ranks.size()) - 1; i >= 0; --i) {
        for (char c : ranks[i]) placement += swapCase(c);
        if (i > 0) placement += '/';
    }
    const std::string side = fields[1] == "w" ? "b" : "w";
    std::string castling = "-";
    if (fields[2] != "-") { castling.clear(); for (char c : fields[2]) castling += swapCase(c); }
    std::string ep = "-";
    if (fields[3] != "-") ep = std::string(1, fields[3][0]) + (fields[3][1] == '3' ? '6' : '3');
    return placement + " " + side + " " + castling + " " + ep + " " + fields[4] + " " + fields[5];
}

void testColourSymmetry() {
    int checked = 0;
    for (const PerftCase& c : standardPerftSuite()) {
        Position a = fromFEN(c.fen);
        Position b = fromFEN(mirrorFEN(c.fen));
        const int depth = static_cast<int>(std::min<size_t>(c.nodes.size(), 3));
        const uint64_t na = perft(a, depth), nb = perft(b, depth);
        ++checked;
        CHECK(na == nb, "mirrored position has the same perft: " + c.name + " (" + str(na) + " vs " + str(nb) + ")");
        CHECK(nb == c.nodes[depth - 1], "mirrored position matches the reference count: " + c.name);
    }
    CHECK(checked == static_cast<int>(standardPerftSuite().size()), "every suite position mirrored");
}

// --- Rule properties checked over whole move trees -------------------------------------------------------------

static void walkRules(Position& pos, int depth, bool& kingCaptured, bool& kingLeftInCheck, bool& inconsistent) {
    if (depth == 0) return;
    MoveList moves;
    generateLegalMoves(pos, moves);
    const Color mover = pos.sideToMove();
    for (size_t i = 0; i < moves.size(); ++i) {
        if (moves[i].captured != NO_PIECE && typeOf(moves[i].captured) == KING) kingCaptured = true;
        const UndoInfo undo = makeMove(pos, moves[i]);
        if (isInCheck(pos, mover)) kingLeftInCheck = true;
        if (!pos.isConsistent()) inconsistent = true;
        walkRules(pos, depth - 1, kingCaptured, kingLeftInCheck, inconsistent);
        unmakeMove(pos, moves[i], undo);
    }
}

void testNoIllegalMovesInTrees() {
    for (const PerftCase& c : standardPerftSuite()) {
        Position pos = fromFEN(c.fen);
        bool kingCaptured = false, kingLeftInCheck = false, inconsistent = false;
        walkRules(pos, 3, kingCaptured, kingLeftInCheck, inconsistent);
        CHECK(!kingCaptured, "no legal move ever captures a king: " + c.name);
        CHECK(!kingLeftInCheck, "no legal move leaves the mover's own king in check: " + c.name);
        CHECK(!inconsistent, "every position in the tree stays consistent: " + c.name);
    }
}

int main() {
    initAttackTables();

    testStandardSuite();
    testSuiteCoversTheRequiredAreas();
    testRandomPositionsMatchReference();
    testDetailedStatsMatchReference();
    testDetailedDepthZero();
    testDivideMatchesReference();
    testDivideEdgeCases();
    testPerftBasics();
    testPerftRestoresPosition();
    testBulkEqualsPlain();
    testColourSymmetry();
    testNoIllegalMovesInTrees();

    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << testsFailed << " TESTS FAILED\n";
    return 1;
}