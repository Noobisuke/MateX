// Perft command-line tool (Stage 9).
//
//   perft_cli <depth> [FEN]              count leaf nodes, with time and nodes/second
//   perft_cli --divide <depth> [FEN]     per-root-move counts (for debugging against a reference engine)
//   perft_cli --detailed <depth> [FEN]   leaf breakdown: captures, en passant, castles, promotions, checks, mates
//   perft_cli --bulk <depth> [FEN]       same count, using bulk counting at depth 1
//   perft_cli --suite [maxDepth]         run the whole built-in perft suite (default: all depths)
//
// The FEN defaults to the standard start position.

#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/perft/perft.hpp"
#include "../src/perft/suite.hpp"
#include "../src/rules/validate.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

static void usage() {
    std::cout << "usage:\n"
                 "  perft_cli <depth> [FEN]\n"
                 "  perft_cli --divide <depth> [FEN]\n"
                 "  perft_cli --detailed <depth> [FEN]\n"
                 "  perft_cli --bulk <depth> [FEN]\n"
                 "  perft_cli --suite [maxDepth]\n";
}

static double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

static void printRate(uint64_t nodes, double seconds) {
    std::cout << "time: " << std::fixed << std::setprecision(3) << seconds << " s";
    if (seconds > 0) std::cout << "   " << std::setprecision(2) << nodes / seconds / 1e6 << " M nodes/s";
    std::cout << "\n";
}

int main(int argc, char** argv) {
    initAttackTables();

    std::string mode = "perft";
    int argi = 1;
    if (argi < argc && std::string(argv[argi]).rfind("--", 0) == 0) mode = argv[argi++] + 2;

    if (mode == "suite") {
        const int maxDepth = (argi < argc) ? std::max(1, std::atoi(argv[argi])) : 99;
        const PerftSuiteResult r = runPerftSuite(maxDepth, &std::cout);
        std::cout << "\n" << r.positions << " positions, " << r.checks << " counts compared, " << r.mismatches
                  << " mismatches, " << r.nodes << " nodes\n";
        printRate(r.nodes, r.seconds);
        std::cout << (r.passed() ? "PERFT SUITE PASSED\n" : "PERFT SUITE FAILED\n");
        return r.passed() ? 0 : 1;
    }

    if ((mode != "perft" && mode != "divide" && mode != "detailed" && mode != "bulk") || argi >= argc) {
        usage();
        return 1;
    }
    const int depth = std::atoi(argv[argi++]);
    if (depth < 1) { usage(); return 1; }

    Position pos;
    if (argi < argc) {
        std::string fen = argv[argi++];
        while (argi < argc) { fen += ' '; fen += argv[argi++]; }
        std::string err;
        if (!loadFEN(pos, fen, &err) || !validatePosition(pos, &err)) {
            std::cout << "Bad FEN: " << err << "\n";
            return 1;
        }
    } else {
        pos.setStartingPosition();
    }

    const auto t0 = std::chrono::steady_clock::now();
    if (mode == "divide") {
        uint64_t total = 0;
        std::vector<DivideEntry> lines = divide(pos, depth);
        std::sort(lines.begin(), lines.end(),
                  [](const DivideEntry& a, const DivideEntry& b) { return a.move.toString() < b.move.toString(); });
        for (const DivideEntry& e : lines) {
            std::cout << e.move.toString() << ": " << e.nodes << "\n";
            total += e.nodes;
        }
        std::cout << "\nmoves: " << lines.size() << "\nnodes: " << total << "\n";
        printRate(total, secondsSince(t0));
    } else if (mode == "detailed") {
        const PerftStats s = perftDetailed(pos, depth);
        std::cout << "nodes:      " << s.nodes << "\ncaptures:   " << s.captures << "\nen passant: " << s.enPassant
                  << "\ncastles:    " << s.castles << "\npromotions: " << s.promotions << "\nchecks:     " << s.checks
                  << "\ncheckmates: " << s.checkmates << "\n";
        printRate(s.nodes, secondsSince(t0));
    } else {
        const uint64_t nodes = (mode == "bulk") ? perftBulk(pos, depth) : perft(pos, depth);
        std::cout << "nodes: " << nodes << "\n";
        printRate(nodes, secondsSince(t0));
    }
    return 0;
}