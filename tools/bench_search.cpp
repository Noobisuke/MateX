// bench_search [depth] : nodes and time for each move-ordering configuration on a fixed position set.
// The score must be identical in every configuration (ordering never changes it); a mismatch is reported.
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/search/search.hpp"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    initAttackTables();
    const int depth = argc > 1 ? std::atoi(argv[1]) : 4;
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
    struct Config { const char* name; unsigned flags; };
    const Config configs[] = {
        {"none", ORDER_NONE},
        {"+captures", ORDER_CAPTURES},
        {"+killers", ORDER_CAPTURES | ORDER_KILLERS},
        {"+history", ORDER_CAPTURES | ORDER_HISTORY},
        {"all three", ORDER_DEFAULT},
    };
    std::cout << "depth " << depth << " (alpha-beta + quiescence); nodes (ms)\n";
    std::cout << std::left << std::setw(13) << "position";
    for (const auto& c : configs) std::cout << std::setw(24) << c.name;
    std::cout << "\n";
    uint64_t total[5] = {};
    for (const auto& [name, fen] : positions) {
        std::cout << std::left << std::setw(13) << name;
        int refScore = 0;
        for (size_t k = 0; k < 5; ++k) {
            Position p;
            loadFEN(p, fen, nullptr);
            const auto t0 = std::chrono::steady_clock::now();
            const SearchResult r = searchOrdered(p, depth, configs[k].flags);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (k == 0) refScore = r.score;
            total[k] += r.nodes;
            std::string cell = std::to_string(r.nodes) + " (" + std::to_string(static_cast<long>(ms)) + ")";
            if (r.score != refScore) cell += " SCORE MISMATCH";
            std::cout << std::setw(24) << cell;
        }
        std::cout << "\n";
    }
    std::cout << std::left << std::setw(13) << "TOTAL";
    for (size_t k = 0; k < 5; ++k)
        std::cout << std::setw(24) << (std::to_string(total[k]) + " (" + std::to_string(100.0 * total[k] / total[0]).substr(0, 5) + "%)");
    std::cout << "\n";
    return 0;
}