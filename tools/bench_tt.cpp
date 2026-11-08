// bench_tt [depth] : what does the transposition table buy, and which replacement policy is best?
//
// Table 1  (fresh table, fixed depth): nodes without a table, with the table used only for the hash move,
//          in score-preserving EXACT_DEPTH mode, and in standard mode. EXACT_DEPTH and ORDER_ONLY must give the
//          identical score; standard mode may legitimately differ (deeper entries) and is flagged when it does.
// Table 2  (iterative deepening to `depth`, the realistic use): total nodes and time per policy and table size.
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/search/search.hpp"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
const char* policyName(TTReplace p) {
    switch (p) {
        case TTReplace::ALWAYS: return "ALWAYS";
        case TTReplace::DEPTH_AGE: return "DEPTH_AGE";
        case TTReplace::TWO_TIER: return "TWO_TIER";
        case TTReplace::BUCKET4: return "BUCKET4";
    }
    return "?";
}
}  // namespace

int main(int argc, char** argv) {
    initAttackTables();
    const int depth = argc > 1 ? std::atoi(argv[1]) : 5;
    const std::vector<std::pair<std::string, std::string>> positions = {
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
        {"endgame", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
        {"tactical", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"},
        {"promo-fight", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"},
        {"open-game", "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"},
        {"middlegame", "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10"},
        {"italian", "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7"},
    };

    // ---------------- Table 1 ----------------
    std::cout << "TABLE 1: fixed depth " << depth << ", fresh 16 MB table per search; nodes (ms)\n";
    std::cout << std::left << std::setw(13) << "position" << std::setw(20) << "no table" << std::setw(20) << "hash move only"
              << std::setw(20) << "exact-depth" << std::setw(20) << "standard" << "\n";
    uint64_t total[4] = {};
    double totalMs[4] = {};
    int standardDiffers = 0, identityBroken = 0;
    for (const auto& [name, fen] : positions) {
        std::cout << std::left << std::setw(13) << name;
        int refScore = 0;
        for (int k = 0; k < 4; ++k) {
            Position p;
            loadFEN(p, fen, nullptr);
            TranspositionTable tt(16);
            const auto t0 = std::chrono::steady_clock::now();
            SearchResult r;
            if (k == 0) r = search(p, depth);
            else r = searchTT(p, depth, tt, k == 1 ? TT_ORDER_ONLY : k == 2 ? TT_EXACT_DEPTH : TT_DEPTH_GE);
            const double ms = msSince(t0);
            if (k == 0) refScore = r.score;
            total[k] += r.nodes;
            totalMs[k] += ms;
            std::string cell = std::to_string(r.nodes) + " (" + std::to_string(static_cast<long>(ms)) + ")";
            if (k > 0 && k < 3 && r.score != refScore) { cell += " MISMATCH"; ++identityBroken; }
            if (k == 3 && r.score != refScore) { cell += " *"; ++standardDiffers; }
            std::cout << std::setw(20) << cell;
        }
        std::cout << "\n";
    }
    std::cout << std::left << std::setw(13) << "TOTAL";
    for (int k = 0; k < 4; ++k)
        std::cout << std::setw(20) << (std::to_string(total[k]) + " (" + std::to_string(100.0 * static_cast<double>(total[k]) / static_cast<double>(total[0])).substr(0, 5) + "%)");
    std::cout << "\n" << std::left << std::setw(13) << "time ms";
    for (int k = 0; k < 4; ++k) std::cout << std::setw(20) << static_cast<long>(totalMs[k]);
    std::cout << "\n";
    std::cout << "score identity (hash-move-only, exact-depth): " << (identityBroken == 0 ? "OK" : "BROKEN") << "; standard mode score differs from plain in "
              << standardDiffers << " of " << positions.size() << " positions (* = deeper entries leaked in, expected)\n\n";

    // ---------------- Table 2 ----------------
    const size_t sizes[] = {1024, 16384, 262144, 4194304};  // entries (16 B each): 16 KB, 256 KB, 4 MB, 64 MB
    std::cout << "TABLE 2: iterative deepening to depth " << depth << ", all " << positions.size()
              << " positions, table cleared before each position; total nodes (total ms)\n";
    uint64_t baseNodes = 0;
    double baseMs = 0;
    for (const auto& [name, fen] : positions) {
        Position p; loadFEN(p, fen, nullptr);
        SearchLimits lim; lim.maxDepth = depth;
        const auto t0 = std::chrono::steady_clock::now();
        baseNodes += searchIterative(p, lim).nodes;
        baseMs += msSince(t0);
    }
    std::cout << "no table: " << baseNodes << " nodes (" << static_cast<long>(baseMs) << " ms)\n";
    std::cout << std::left << std::setw(12) << "policy";
    for (size_t sz : sizes) std::cout << std::setw(26) << (std::to_string(sz) + " entries");
    std::cout << "\n";
    const TTReplace policies[] = {TTReplace::ALWAYS, TTReplace::DEPTH_AGE, TTReplace::TWO_TIER, TTReplace::BUCKET4};
    for (TTReplace pol : policies) {
        std::cout << std::left << std::setw(12) << policyName(pol);
        for (size_t sz : sizes) {
            TranspositionTable tt(1, pol);
            tt.resizeEntries(sz);
            uint64_t nodes = 0;
            double ms = 0;
            for (const auto& [name, fen] : positions) {
                Position p; loadFEN(p, fen, nullptr);
                tt.clear();
                SearchLimits lim; lim.maxDepth = depth; lim.tt = &tt;
                const auto t0 = std::chrono::steady_clock::now();
                nodes += searchIterative(p, lim).nodes;
                ms += msSince(t0);
            }
            std::cout << std::setw(26) << (std::to_string(nodes) + " (" + std::to_string(static_cast<long>(ms)) + ") " +
                                           std::to_string(100.0 * static_cast<double>(nodes) / static_cast<double>(baseNodes)).substr(0, 4) + "%");
        }
        std::cout << "\n";
    }
    return 0;
}
