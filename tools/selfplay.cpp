// selfplay: node-limited match between two search configurations of this engine.
//   selfplay --a "<opts>" --b "<opts>" [--ea "<evalflags>" --eb "<evalflags>"] [--nodes N] [--pairs P] [--start S] [--plies K] [--tt MB] [--maxsec T] [--seed X]
// Every opening is played twice with the colours swapped. Every move gets the same node budget, so the result does
// not depend on timing or machine load and can be reproduced exactly; --start lets a long match be run in pieces.
// A wins-draws-losses line, the score and an Elo estimate with a 95% interval are printed (the interval is wide for
// few games: 100 games can only separate differences of roughly 70 Elo or more).
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/zobrist.hpp"
#include "../src/eval/evaluate.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/rules/gamestate.hpp"
#include "../src/search/search.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

struct Rng {
    uint64_t s;
    uint64_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s >> 33; }
};

struct Player {
    SearchOptions opts;
    unsigned evalFlags = EVAL_BASIC;  // Stage 18: the evaluation features this player uses
    TranspositionTable tt;
    explicit Player(size_t mb) : tt(mb) {}
};

// Result from A's point of view: +1 win, 0 draw, -1 loss.
int playGame(const Position& opening, Player& a, Player& b, bool aIsWhite, uint64_t nodes, int maxPlies) {
    Position pos = opening;
    a.tt.clear();
    b.tt.clear();
    std::vector<uint64_t> history;
    std::map<uint64_t, int> seen;
    seen[hashKey(pos)] = 1;
    int winStreak[2] = {0, 0};
    int lastScore[2] = {0, 0};
    int flatStreak = 0;
    for (int ply = 0; ply < maxPlies; ++ply) {
        MoveList legal;
        generateLegalMoves(pos, legal);
        const bool whiteToMove = pos.sideToMove() == WHITE;
        const bool aToMove = whiteToMove == aIsWhite;
        if (legal.size() == 0) {
            if (!isInCheck(pos, pos.sideToMove())) return 0;  // stalemate
            return aToMove ? -1 : 1;                           // mated side loses
        }
        if (isDeadPosition(pos) || pos.halfmoveClock() >= 100) return 0;

        Player& p = aToMove ? a : b;
        SearchLimits lim;
        lim.maxNodes = nodes;
        lim.tt = &p.tt;
        lim.options = p.opts;
        lim.history = history;
        setEvalFlags(p.evalFlags);  // the evaluation is a process-wide setting: select this player's before it searches
        const SearchResult r = searchIterative(pos, lim);
        if (!r.hasMove) return 0;

        // Adjudication (keeps games short): a game is decided when the side to move scored itself 7+ pawns up three times
        // in a row AND the opponent's last search agreed (-3 pawns or worse); it is drawn when both searches have said
        // "equal" (within 8 centipawns) for 8 plies in a row after move 45.
        const int side = whiteToMove ? 0 : 1;
        lastScore[side] = r.score;
        winStreak[side] = r.score >= 700 ? winStreak[side] + 1 : 0;
        if (winStreak[side] >= 3 && lastScore[1 - side] <= -300) return aToMove ? 1 : -1;
        flatStreak = std::abs(r.score) <= 8 ? flatStreak + 1 : 0;
        if (ply >= 90 && flatStreak >= 8) return 0;

        history.push_back(hashKey(pos));
        makeMove(pos, r.bestMove);
        if (++seen[hashKey(pos)] >= 3) return 0;  // threefold repetition
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    initAttackTables();
    std::string optsA = "none", optsB = "none", evalA = "none", evalB = "none";
    uint64_t nodes = 10000;
    int pairs = 20, start = 0, openingPlies = 6, ttMb = 8, maxPlies = 200;
    double maxSec = 1e9;
    uint64_t seed = 12345;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--a") optsA = next();
        else if (a == "--b") optsB = next();
        else if (a == "--ea") evalA = next();
        else if (a == "--eb") evalB = next();
        else if (a == "--nodes") nodes = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--pairs") pairs = std::atoi(next().c_str());
        else if (a == "--start") start = std::atoi(next().c_str());
        else if (a == "--plies") openingPlies = std::atoi(next().c_str());
        else if (a == "--tt") ttMb = std::atoi(next().c_str());
        else if (a == "--maxsec") maxSec = std::atof(next().c_str());
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
    }
    bool okA, okB;
    Player A(static_cast<size_t>(ttMb)), B(static_cast<size_t>(ttMb));
    A.opts = SearchOptions::fromString(optsA, &okA);
    B.opts = SearchOptions::fromString(optsB, &okB);
    if (!okA || !okB) { std::cerr << "unknown option name\n"; return 1; }
    if (!parseEvalFlags(evalA, A.evalFlags) || !parseEvalFlags(evalB, B.evalFlags)) { std::cerr << "unknown eval flag\n"; return 1; }

    int w = 0, d = 0, l = 0, done = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int pair = start; pair < start + pairs; ++pair) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > maxSec) break;
        // Opening: a few random legal plies, kept roughly balanced.
        Rng rng{seed + static_cast<uint64_t>(pair) * 7919ULL};
        Position opening;
        for (int attempt = 0; attempt < 100; ++attempt) {
            Position p;
            p.setStartingPosition();
            bool ok = true;
            for (int k = 0; k < openingPlies; ++k) {
                MoveList legal;
                generateLegalMoves(p, legal);
                if (legal.size() == 0) { ok = false; break; }
                makeMove(p, legal[rng.next() % legal.size()]);
            }
            if (ok && std::abs(evaluate(p, EVAL_BASIC)) <= 120) { opening = p; break; }  // fixed evaluation: same openings for every comparison
            if (attempt == 99) opening = p;
        }
        for (int colour = 0; colour < 2; ++colour) {
            const int r = playGame(opening, A, B, colour == 0, nodes, maxPlies);
            (r > 0 ? w : r == 0 ? d : l)++;
        }
        ++done;
    }
    const int n = w + d + l;
    const double score = n ? (w + 0.5 * d) / n : 0.5;
    double elo = 0, err = 0;
    if (n > 0 && score > 0 && score < 1) {
        elo = -400.0 * std::log10(1.0 / score - 1.0);
        const double var = (w * std::pow(1 - score, 2) + d * std::pow(0.5 - score, 2) + l * std::pow(score, 2)) / n;
        const double se = std::sqrt(var / n);
        const double hi = std::min(0.999, score + 1.96 * se), lo = std::max(0.001, score - 1.96 * se);
        err = (-400.0 * std::log10(1.0 / hi - 1.0) + 400.0 * std::log10(1.0 / lo - 1.0)) / 2.0;
    }
    std::cout << "RESULT pairs " << start << ".." << start + done - 1 << " nodes " << nodes << "  A=[" << A.opts.toString() << " | eval " << evalFlagsToString(A.evalFlags) << "] vs B=[" << B.opts.toString() << " | eval " << evalFlagsToString(B.evalFlags)
              << "]  A: +" << w << " =" << d << " -" << l << "  score " << 100.0 * score << "%  Elo(A-B) " << elo << " +-" << err << "\n";
    return 0;
}