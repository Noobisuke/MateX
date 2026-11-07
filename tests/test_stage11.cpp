// Stage 11 tests: negamax search.
#include "mirror.hpp"
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/eval/evaluate.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/perft.hpp"
#include "../src/perft/suite.hpp"
#include "../src/search/search.hpp"
#include <chrono>
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

static SearchResult run(const std::string& fen, int depth) {
    Position p = fromFEN(fen);
    return searchMinimax(p, depth);  // Stage 11 tests the plain reference search
}

void testScoreHelpers() {
    CHECK(isMateScore(MATE_SCORE - 1) && isMateScore(-(MATE_SCORE - 7)), "mate scores recognised");
    CHECK(!isMateScore(900) && !isMateScore(-2000) && !isMateScore(0), "eval scores are not mate scores");
    CHECK(MATE_SCORE + 0 < INFINITE_SCORE, "mate below infinity");
    CHECK(mateInMoves(MATE_SCORE - 1) == 1, "mate in 1");
    CHECK(mateInMoves(MATE_SCORE - 3) == 2, "mate in 2");
    CHECK(mateInMoves(MATE_SCORE - 5) == 3, "mate in 3");
    CHECK(mateInMoves(-(MATE_SCORE - 2)) == -1, "mated in 1 (opponent mates on our next move)");
    CHECK(mateInMoves(-(MATE_SCORE - 4)) == -2, "mated in 2");
    CHECK(mateInMoves(123) == 0, "non-mate -> 0");
}

void testNodeCounts() {
    // With no terminal cut-offs, plain negamax visits exactly the perft tree: 1 + p1 + ... + pd.
    const char* fens[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"};
    for (const char* fen : fens) {
        for (int d = 1; d <= 3; ++d) {
            Position p = fromFEN(fen);
            uint64_t expected = 1;
            for (int i = 1; i <= d; ++i) { Position q = fromFEN(fen); expected += perft(q, i); }
            const SearchResult r = searchMinimax(p, d);
            CHECK(r.nodes == expected, std::string("node count == perft sum, depth ") + std::to_string(d));
        }
    }
}

void testDepthZeroAndClamp() {
    Position p = fromFEN("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    const SearchResult r = searchMinimax(p, 0);
    CHECK(!r.hasMove && r.score == evaluate(p) && r.nodes == 1, "depth 0 = static eval, no move");
    CHECK(searchMinimax(p, -5).depth == 0, "negative depth clamps to 0");
}

void testDepthOneBruteForce() {
    // Depth 1 must equal the best -eval(child) over legal moves (computed independently here).
    const char* fens[] = {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                          "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1"};
    for (const char* fen : fens) {
        Position p = fromFEN(fen);
        MoveList moves; generateLegalMoves(p, moves);
        int best = -100000;
        for (size_t i = 0; i < moves.size(); ++i) {
            Position c = p; makeMove(c, moves[i]);
            int s;
            if (!hasLegalMove(c)) s = isInCheck(c, c.sideToMove()) ? MATE_SCORE - 1 : 0;
            else s = -evaluate(c);
            if (s > best) best = s;
        }
        CHECK(searchMinimax(p, 1).score == best, std::string("depth-1 matches brute force: ") + fen);
    }
}

void testMates() {
    SearchResult r = run("r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1", 1);
    CHECK(r.score == MATE_SCORE - 1 && r.hasMove && r.bestMove.toString() == "h5f7", "scholar's mate Qxf7#");
    CHECK(mateInMoves(r.score) == 1, "reports mate in 1");

    r = run("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", 2);
    CHECK(r.score == MATE_SCORE - 1 && r.bestMove.toString() == "a1a8", "back-rank mate Ra8#");

    r = run("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2", 3);
    CHECK(r.score == MATE_SCORE - 1 && r.bestMove.toString() == "d8h4", "fool's mate for Black: Qh4#");

    r = run("7k/8/5K2/8/8/8/8/6Q1 b - - 0 1", 2);
    CHECK(r.score == -(MATE_SCORE - 2) && mateInMoves(r.score) == -1, "side to move is mated in 1");
}

void testMateDistanceAndStalemateAvoidance() {
    // Qf7?? would be stalemate. The engine must find the real mate in 2 and prefer the shortest line.
    const std::string fen = "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1";
    for (int d : {3, 4, 5}) {
        const SearchResult r = run(fen, d);
        CHECK(r.score == MATE_SCORE - 3, std::string("mate in 2 found and shortest, depth ") + std::to_string(d));
        CHECK(r.bestMove.toString() != "f1f7", "does not stalemate");
        CHECK(mateInMoves(r.score) == 2, "mate in 2 moves");
    }
    CHECK(run(fen, 2).score != MATE_SCORE - 3 && !isMateScore(run(fen, 2).score), "depth 2 cannot see the mate");
}

void testTerminalRoots() {
    SearchResult r = run("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1", 3);
    CHECK(r.score == -MATE_SCORE && !r.hasMove && r.pv.empty(), "root checkmated");
    r = run("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 3);
    CHECK(r.score == 0 && !r.hasMove, "root stalemate = 0");
    r = run("4k3/8/8/8/8/8/8/4KB2 w - - 0 1", 3);
    CHECK(r.score == 0, "dead position = 0");
    r = run("4k3/8/8/8/8/8/8/3QK3 w - - 100 80", 3);
    CHECK(r.score == 0 && r.hasMove, "fifty-move claim: score 0 but the root still returns a move");
    r = run("4k3/8/8/8/8/8/8/3QK3 w - - 150 80", 3);
    CHECK(r.score == 0 && !r.hasMove, "seventy-five-move rule: automatic draw, game over at the root");
    r = run("6k1/5ppp/8/8/8/8/8/R3K3 w - - 100 80", 2);
    CHECK(r.score == MATE_SCORE - 1 && r.bestMove.toString() == "a1a8", "mate is found even when a fifty-move claim exists at the root");
    r = run("4k3/8/8/8/8/8/8/3QK3 w - - 98 80", 1);
    CHECK(r.score > 0, "clock 99 after one quiet move is not yet a draw");
    r = run("4k3/8/8/8/8/8/8/3QK3 w - - 99 80", 3);
    CHECK(r.score == 0, "every quiet line reaches clock 100 within the horizon: draw");
    r = run("6k1/5ppp/8/8/8/8/8/R3K3 w - - 99 80", 2);
    CHECK(r.score == MATE_SCORE - 1, "mate on the move that makes the clock 100 is still mate");
}

void testWinningAndLosing() {
    CHECK(run("4k3/8/8/8/8/8/8/3QK3 w - - 0 1", 3).score > 500, "queen up scores as winning");
    CHECK(run("4k3/8/8/8/8/8/8/3QK3 b - - 0 1", 3).score < -500, "queen down scores as losing");
    CHECK(!isMateScore(run("4k3/8/8/8/8/8/8/3QK3 w - - 0 1", 3).score), "winning is not a mate score");
}

void testPVAndRestore() {
    const char* fens[] = {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
                          "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"};
    for (const char* fen : fens) {
        Position p = fromFEN(fen);
        const Position before = p;
        const SearchResult r = searchMinimax(p, 3);
        CHECK(p == before, "position restored after search");
        CHECK(r.pv.size() >= 1 && r.pv.size() <= 3 && r.pv[0] == r.bestMove, "pv shape");

        Position walk = fromFEN(fen);
        bool legal = true;
        for (const Move& m : r.pv) {
            MoveList ml; generateLegalMoves(walk, ml);
            bool found = false;
            for (size_t i = 0; i < ml.size(); ++i) if (ml[i] == m) { found = true; break; }
            if (!found) { legal = false; break; }
            makeMove(walk, m);
        }
        CHECK(legal, "every pv move is legal in sequence");
        if (legal && r.pv.size() == 3 && hasLegalMove(walk)) {
            const int leaf = evaluate(walk);  // side to move after 3 plies is the opposite of the root side
            CHECK(r.score == -leaf, std::string("pv leaf eval reproduces the score: ") + fen);
        }
    }
}

void testColourSymmetry() {
    const char* fens[] = {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                          "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
                          "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1"};
    for (const char* fen : fens) {
        CHECK(run(fen, 3).score == run(mirrorFEN(fen), 3).score, std::string("mirrored position, same score: ") + fen);
    }
}

void testSpeed() {
    Position p = fromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const auto t0 = std::chrono::steady_clock::now();
    const SearchResult r = searchMinimax(p, 4);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "Kiwipete depth 4: " << r.nodes << " nodes, " << sec << " s, " << static_cast<uint64_t>(r.nodes / sec)
              << " nodes/s, best " << r.bestMove.toString() << ", score " << r.score << "\n";
}

int main() {
    initAttackTables();
    testScoreHelpers(); testNodeCounts(); testDepthZeroAndClamp(); testDepthOneBruteForce();
    testMates(); testMateDistanceAndStalemateAvoidance(); testTerminalRoots(); testWinningAndLosing();
    testPVAndRestore(); testColourSymmetry(); testSpeed();
    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed\n";
    return testsFailed == 0 ? 0 : 1;
}