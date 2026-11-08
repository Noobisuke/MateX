// Stage 13 tests: quiescence search.
#include "mirror.hpp"
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/eval/evaluate.hpp"
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

static std::vector<std::string> positions() {
    std::vector<std::string> v;
    for (const auto& c : standardPerftSuite()) v.push_back(c.fen);
    uint64_t rng = 0xABCDEF0123456789ULL;
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

// Alpha-beta + quiescence must equal plain minimax + unpruned quiescence exactly.
static bool agree(const std::string& fen, int depth) {
    Position a = fromFEN(fen), b = fromFEN(fen);
    const Position before = a;
    const SearchResult q = searchOrdered(a, depth, ORDER_NONE);
    const SearchResult ref = searchMinimaxQuiescence(b, depth);
    const bool ok = q.score == ref.score && q.hasMove == ref.hasMove && (!q.hasMove || q.bestMove == ref.bestMove) &&
                    samePV(q, ref) && a == before;
    if (!ok) std::cout << "  differs at depth " << depth << ": " << fen << "  q=" << q.score << " ref=" << ref.score << "\n";
    return ok;
}

// The unpruned reference quiescence is exponential, so it is only feasible on positions with few pieces.
static std::vector<std::string> smallPositions(int maxPieces) {
    std::vector<std::string> out;
    for (const auto& f : positions()) {
        Position p = fromFEN(f);
        if (popCount(p.occupancyAll()) <= maxPieces) out.push_back(f);
    }
    return out;
}

void testAgreesWithReference() {
    const auto fens = smallPositions(14);
    CHECK(fens.size() >= 30, "enough small positions for the comparison: " + std::to_string(fens.size()));
    for (int depth = 0; depth <= 2; ++depth) {
        int bad = 0;
        for (const auto& f : fens) if (!agree(f, depth)) ++bad;
        CHECK(bad == 0, "alpha-beta+quiescence == minimax+quiescence at depth " + std::to_string(depth) + ", bad=" +
                         std::to_string(bad) + " of " + std::to_string(fens.size()));
    }
    int bad = 0, n = 0;
    for (size_t i = 0; i < fens.size(); i += 4, ++n) if (!agree(fens[i], 3)) ++bad;
    CHECK(bad == 0, "agreement at depth 3 on " + std::to_string(n) + " small positions, bad=" + std::to_string(bad));
}

void testHorizonEffect() {
    // White queen can grab the d5 pawn, but c6 recaptures. A static eval after 1 ply sees only "+1 pawn".
    const std::string fen = "4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1";
    Position a = fromFEN(fen), b = fromFEN(fen);
    const SearchResult plain = searchAlphaBeta(a, 1);
    const SearchResult quiet = search(b, 1);
    CHECK(plain.bestMove.toString() == "d1d5", "without quiescence depth 1 falls for the poisoned pawn");
    CHECK(quiet.bestMove.toString() != "d1d5", "with quiescence depth 1 sees cxd5 and declines");
    CHECK(quiet.score > 0, "still winning (queen vs two pawns)");

    // A free pawn that really is free must still be taken.
    Position c = fromFEN("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1");
    CHECK(search(c, 1).bestMove.toString() == "d1d5", "an undefended pawn is still captured");
}

void testStandPatAndLeafValue() {
    // Quiet position: depth 0 quiescence equals static eval (nothing to capture).
    Position s; s.setStartingPosition();
    CHECK(search(s, 0).score == evaluate(s), "quiet position: quiescence returns the static eval");
    // Hanging queen at depth 0: stand pat is bad, capture wins it back.
    Position h = fromFEN("4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1");
    const int stat = evaluate(h);
    CHECK(search(h, 0).score > stat + 400, "depth 0 + quiescence resolves a capture that static eval ignores");
}

void testChecksAreNotStoodPatThrough() {
    // Side to move is in check at a quiescence node and may not stand pat; mate in the evasion tree is found.
    // Black king h8, white rook captures give check chain. Use a back-rank mate delivered by capture at the horizon.
    Position p = fromFEN("6k1/5ppp/8/8/8/8/r7/R3K2R w KQ - 0 1");
    for (int d = 0; d <= 3; ++d) CHECK(agree(toFEN(p), d), "check handling agrees with the reference at depth " + std::to_string(d));
    // Fixed expected values, produced by an independent python-chess quiescence implementation. In each position the
    // side to move is in check at depth 0, so it may not stand pat; a version that stood pat scores them differently.
    struct InCheckCase { const char* fen; int score; };
    const InCheckCase inCheck[] = {
        {"r6r/1P3k2/b3B2p/pN1Pp1p1/P1p1PpP1/6P1/5K1P/3R2NR b - - 2 30", -1144},
        {"r1b3n1/1p2p1b1/p1n1r1k1/3P4/6p1/2K4p/q5BP/RN3R2 w - - 2 34", -2388},
        {"1n1k1Qnr/1b5p/r1p3p1/pp1pppN1/P1BP2P1/R3P1P1/1PP5/1NBK2R1 b - - 0 18", -1310},
        {"r1b1kbn1/pq4pn/1p5r/2pp2K1/P1P3PQ/7P/1PNPP2R/R1B2BN1 w - - 3 24", -738},
    };
    for (const auto& c : inCheck) {
        Position q = fromFEN(c.fen);
        CHECK(search(q, 0).score == c.score, std::string("in check at depth 0, no stand pat: ") + c.fen);
    }
    // Already in check at depth 0: no stand-pat, evasions are searched, mate is reported.
    Position m = fromFEN("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(search(m, 0).score == -MATE_SCORE, "checkmated at depth 0");
}

void testDeadPositionAfterCapture() {
    // Bxd5 leaves K+B v K, a dead draw. Quiescence must score it 0, not as bishop-up.
    Position p = fromFEN("4k3/8/8/3r4/8/5B2/8/4K3 w - - 0 1");
    CHECK(search(p, 0).score == 0, "capturing into a dead position scores 0");
}

void testMatesStillFound() {
    struct Case { const char* fen; int depth; int score; const char* move; };
    const Case cases[] = {
        {"r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1", 1, MATE_SCORE - 1, "h5f7"},
        {"6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", 2, MATE_SCORE - 1, "a1a8"},
        {"rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2", 3, MATE_SCORE - 1, "d8h4"},
        {"7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", 3, MATE_SCORE - 3, nullptr},
        {"7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", 5, MATE_SCORE - 3, nullptr},
    };
    for (const Case& c : cases) {
        Position p = fromFEN(c.fen);
        const SearchResult r = search(p, c.depth);
        CHECK(r.score == c.score, std::string("mate score preserved: ") + c.fen);
        if (c.move) CHECK(r.bestMove.toString() == c.move, std::string("mate move found: ") + c.move);
        else CHECK(r.bestMove.toString() != "f1f7", "no stalemate blunder");
    }
    CHECK(isMateScore(MATE_SCORE - MAX_PLY) && !isMateScore(MATE_SCORE - MAX_PLY - 1), "mate bound covers quiescence plies");
}

void testTermination() {
    // Promotion-heavy and capture-heavy positions: must terminate and stay inside the ply bound.
    const char* fens[] = {"4k3/P1P5/8/8/8/8/p1p5/4K3 w - - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "1r2k3/PP6/8/8/8/8/pp6/1R2K3 b - - 0 1"};
    for (const char* f : fens) {
        Position p = fromFEN(f);
        const Position before = p;
        const SearchResult r = search(p, 2);
        CHECK(r.nodes > 0 && r.nodes < 200000000ULL && p == before, std::string("terminates, position restored: ") + f);
        CHECK(std::abs(r.score) <= INFINITE_SCORE, "score within bounds");
    }
}

void testColourSymmetry() {
    const char* fens[] = {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
                          "4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1"};
    for (const char* f : fens) {
        Position a = fromFEN(f), b = fromFEN(mirrorFEN(f));
        CHECK(search(a, 2).score == search(b, 2).score, std::string("mirrored position, same score: ") + f);
    }
}

void testNodeCost() {
    const char* fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"};
    std::cout << "position            depth  alpha-beta nodes  +quiescence nodes  ratio    best move (without -> with)\n";
    for (const char* f : fens) {
        const int maxD = f == fens[1] ? 3 : 4;  // Kiwipete at depth 4 takes ~9 s with quiescence; depth 3 shows the same effect
        for (int d = 2; d <= maxD; ++d) {
            Position a = fromFEN(f), b = fromFEN(f);
            const auto t0 = std::chrono::steady_clock::now();
            const SearchResult ab = searchAlphaBeta(a, d);
            const auto t1 = std::chrono::steady_clock::now();
            const SearchResult q = searchOrdered(b, d, ORDER_NONE);
            const auto t2 = std::chrono::steady_clock::now();
            std::cout << "  " << std::string(f).substr(0, 14) << "...  " << d << "     " << ab.nodes << "   " << q.nodes << "   "
                      << static_cast<double>(q.nodes) / static_cast<double>(ab.nodes) << "x   " << ab.bestMove.toString() << " -> "
                      << q.bestMove.toString() << "   (" << std::chrono::duration<double>(t1 - t0).count() << " s, "
                      << std::chrono::duration<double>(t2 - t1).count() << " s)\n";
        }
    }
}

int main() {
    initAttackTables();
    auto timed = [](const char* name, void (*fn)()) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        std::cout << "[" << name << ": " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s]\n";
        std::cout.flush();
    };
    timed("horizon", testHorizonEffect); timed("leaf value", testStandPatAndLeafValue);
    timed("checks", testChecksAreNotStoodPatThrough); timed("dead position", testDeadPositionAfterCapture);
    timed("mates", testMatesStillFound); timed("termination", testTermination); timed("symmetry", testColourSymmetry);
    timed("agreement", testAgreesWithReference); timed("node cost", testNodeCost);
    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed\n";
    return testsFailed == 0 ? 0 : 1;
}