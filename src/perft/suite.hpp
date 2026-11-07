#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

// ---------------------------------------------------------------------
// The perft test suite (Stage 9): established positions with the node count for each depth.
//
// Provenance: the first eight are the standard positions from the Chess Programming Wiki
// "Perft Results" page (start position, "Kiwipete", positions 3-6, a mirrored position 4, and a
// castling-only position); the rest are well-known tricky positions in the style of Peter Jones'
// perft suite (en passant discovered checks, pins, promotions, castling through check, ...).
// Every count was recomputed independently with python-chess 1.11.2, and the standard positions
// also agree with the published tables.
// ---------------------------------------------------------------------

struct PerftCase {
    std::string name;
    std::string fen;
    std::vector<uint64_t> nodes;  // nodes[d - 1] = perft(d)
};

const std::vector<PerftCase>& standardPerftSuite();

struct PerftSuiteResult {
    int positions = 0;
    int checks = 0;        // number of (position, depth) counts compared
    int mismatches = 0;
    uint64_t nodes = 0;    // total leaf nodes visited
    double seconds = 0.0;
    bool passed() const { return mismatches == 0 && checks > 0; }
};

// Runs every case up to maxDepth (or its last listed depth). If `out` is not null, prints one line
// per case and, on a mismatch, the expected and actual counts plus a hint to use `divide`.
PerftSuiteResult runPerftSuite(int maxDepth, std::ostream* out);