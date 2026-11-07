#include "suite.hpp"
#include "perft.hpp"
#include "../board/attacks.hpp"
#include "../board/fen.hpp"
#include "../rules/validate.hpp"
#include <chrono>
#include <iomanip>
#include <ostream>

const std::vector<PerftCase>& standardPerftSuite() {
    static const std::vector<PerftCase> suite = {
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {20, 400, 8902, 197281, 4865609}},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", {48, 2039, 97862, 4085603}},
        {"pos3 (ep pins, endgame)", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", {14, 191, 2812, 43238, 674624}},
        {"pos4 (promotions, checks)", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", {6, 264, 9467, 422333}},
        {"pos4 mirrored", "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", {6, 264, 9467, 422333}},
        {"pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", {44, 1486, 62379, 2103487}},
        {"pos6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", {46, 2079, 89890, 3894594}},
        {"castling both sides", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {26, 568, 13744, 314346}},
        {"ep gives discovered check", "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", {15, 126, 1928, 13931, 206379, 1440467}},
        {"ep, bishop pins (black to move)", "8/8/8/2k5/2pP4/8/B7/4K3 b - d3 0 3", {8, 72, 492, 5380, 36744, 444954}},
        {"rook pin along 5th rank", "3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", {18, 92, 1670, 10138, 185429, 1134888}},
        {"ep removes blocker of rank", "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", {13, 102, 1266, 10276, 135655, 1015133}},
        {"castling with slider pins", "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", {26, 1141, 27826, 1274206}},
        {"castling through attack (black)", "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", {44, 1494, 50509, 1720476}},
        {"kingside only", "5k2/8/8/8/8/8/8/4K2R w K - 0 1", {15, 66, 1198, 6399, 120330, 661072}},
        {"queenside only", "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", {16, 71, 1286, 7418, 141077, 803711}},
        {"rook attacked but may castle", "r3k2r/p1pp1pb1/bn2Qnp1/2qPN3/1p2P3/2N5/PPPBBPPP/R3K2R b KQkq - 3 2", {5, 259, 11766, 563603}},
        {"queenside, b8 attacked", "2kr3r/p1ppqpb1/bn2Qnp1/3PN3/1p2P3/2N5/PPPBBPPP/R3K2R b KQ - 3 2", {44, 2385, 99756, 5144430}},
        {"promotion with check", "rnb2k1r/pp1Pbppp/2p5/q7/2B5/8/PPPQNnPP/RNB1K2R w KQ - 3 9", {39, 1577, 63647, 2433142}},
        {"promotion + capture (white)", "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", {11, 133, 1442, 19174, 266199, 3821001}},
        {"underpromotion cases", "8/P1k5/K7/8/8/8/8/8 w - - 0 1", {6, 27, 273, 1329, 18135, 92683}},
        {"promotion, stalemate trick", "K1k5/8/P7/8/8/8/8/8 w - - 0 1", {2, 6, 13, 63, 382, 2217}},
        {"promotion next to kings", "8/k1P5/8/1K6/8/8/8/8 w - - 0 1", {10, 25, 268, 926, 10857, 43261}},
        {"pawn about to promote, king close", "4k3/1P6/8/8/8/8/K7/8 w - - 0 1", {9, 40, 472, 2661, 38983, 217342}},
        {"black queen + knight vs king", "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", {37, 183, 6559, 23527, 811573}},
        {"black queen, pinned pawn", "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", {29, 165, 5160, 31961, 1004658}},
        {"a-pawn knight gambit start", "r1bqkbnr/pppppppp/n7/8/8/P7/1PPPPPPP/RNBQKBNR w KQkq - 2 2", {19, 380, 8163, 182327, 4364503}},
        {"pawn endgame ep tricks", "2r5/3pk3/8/2P5/8/2K5/8/8 w - - 5 4", {9, 163, 1349, 23718, 177964, 3245821}},
        {"double check position", "4r1k1/8/8/7Q/8/5n2/8/4K3 w - - 0 1", {3, 66, 1330, 19725, 425676}},
        {"pinned knight and rook", "4r1k1/8/8/8/8/8/4N3/4K3 w - - 0 1", {4, 64, 651, 11486, 114004, 2029509}},
        {"max-moves position", "R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1", {218, 99, 19073, 85043, 13853661}},
        {"many queens, check evasion", "3k4/8/8/8/8/8/8/QQ2K2Q w - - 0 1", {51, 185, 10609, 35702, 2116018}},
        {"black to move, in check", "4k3/8/8/8/8/8/4R3/4K3 b - - 0 1", {4, 68, 320, 5734, 32302, 591477}},
        {"rook trade on h8, then castling attempt", "4k1rr/8/8/8/8/8/8/4K2R w k - 0 1", {14, 241, 3509, 79460, 1207802}},
        {"rook trade on a8, then castling attempt", "rr2k3/8/8/8/8/8/8/R3K3 w q - 0 1", {15, 276, 4202, 98754, 1543202}},
        {"rook trade on h1 (black to move)", "4k2r/8/8/8/8/8/8/4K1RR b K - 0 1", {14, 241, 3509, 79460, 1207802}},
        {"stalemate-ish endgame", "7k/5Q2/6K1/8/8/8/8/8 w - - 0 1", {26, 12, 311, 473, 12583, 22909}},
    };
    return suite;
}

PerftSuiteResult runPerftSuite(int maxDepth, std::ostream* out) {
    initAttackTables();
    PerftSuiteResult result;
    const auto start = std::chrono::steady_clock::now();

    for (const PerftCase& c : standardPerftSuite()) {
        Position pos;
        std::string err;
        if (!loadFEN(pos, c.fen, &err) || !validatePosition(pos, &err)) {
            if (out) *out << c.name << ": INVALID FEN (" << err << ")\n";
            ++result.mismatches;
            continue;
        }
        ++result.positions;
        if (out) *out << std::left << std::setw(36) << c.name;

        bool ok = true;
        for (int d = 1; d <= maxDepth && d <= static_cast<int>(c.nodes.size()); ++d) {
            const uint64_t expected = c.nodes[d - 1];
            const uint64_t got = perft(pos, d);
            ++result.checks;
            result.nodes += got;
            if (got != expected) {
                ok = false;
                ++result.mismatches;
                if (out) {
                    *out << "\n    MISMATCH at depth " << d << ": expected " << expected << ", got " << got
                         << "   (debug with: perft_cli --divide " << d << " \"" << c.fen << "\")";
                }
                break;  // deeper depths are meaningless once a depth is wrong
            }
        }
        if (out) *out << (ok ? "ok" : "\n") << "\n";
    }

    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}