// Stage 8 tests: game state and draw rules (checkmate, stalemate, dead position,
// fifty/seventy-five-move rules, threefold/fivefold repetition)

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/rules/gamestate.hpp"
#include "../src/rules/repetition.hpp"
#include "../src/rules/special.hpp"
#include <iostream>
#include <string>
#include <vector>

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(cond, msg) do { \
    ++testsRun; \
    if (!(cond)) { \
        ++testsFailed; \
        std::cout << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while (0)

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) {
        std::cout << "TEST SETUP FAILED to load FEN '" << fen << "': " << err << "\n";
    }
    return pos;
}

// A game in progress: position + history, updated the way a real game loop must.
struct Game {
    Position pos;
    GameHistory history;

    explicit Game(const std::string& fen) : pos(fromFEN(fen)) { history.reset(pos); }

    void play(const std::string& uci) {
        MoveList legal;
        generateLegalMoves(pos, legal);
        for (size_t i = 0; i < legal.size(); ++i) {
            if (legal[i].toString() == uci) {
                makeMove(pos, legal[i]);
                history.push(pos);
                return;
            }
        }
        std::cout << "TEST SETUP FAILED: " << uci << " is not legal in " << toFEN(pos) << "\n";
    }

    void playAll(const std::vector<std::string>& moves) { for (const auto& m : moves) play(m); }

    GameStatus status() const { return evaluateGameStatus(pos, history); }
    int occurrences() const { return history.occurrences(pos); }
};

static GameStatus statusOf(const std::string& fen) {
    Game g(fen);
    return g.status();
}

static const char* START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// --- Checkmate / stalemate -----------------------------------------------------------------

void testCheckmate() {
    GameStatus fool = statusOf("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    CHECK(fool.result == GameResult::CHECKMATE && fool.inCheck, "Fool's mate is checkmate");
    CHECK(fool.isOver(), "checkmate ends the game");

    Game backRank("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1");
    CHECK(backRank.status().result == GameResult::ONGOING, "before Ra8 the game goes on");
    backRank.play("a1a8");
    CHECK(backRank.status().result == GameResult::CHECKMATE, "back-rank mate Ra8#");

    Game smothered("6rk/6pp/8/6N1/8/8/8/4K3 w - - 0 1");
    smothered.play("g5f7");
    CHECK(smothered.status().result == GameResult::CHECKMATE, "smothered mate Nf7#");
    CHECK(isCheckmate(smothered.pos) && !isStalemate(smothered.pos), "isCheckmate / isStalemate agree");
}

void testCheckIsNotMate() {
    GameStatus s = statusOf("4k3/8/8/8/8/8/4R3/4K3 b - - 0 1");
    CHECK(s.result == GameResult::ONGOING && s.inCheck, "check with escape squares: ongoing, inCheck");
    GameStatus s2 = statusOf(START);
    CHECK(s2.result == GameResult::ONGOING && !s2.inCheck, "start position: ongoing, not in check");
    // Mate can be avoided by capturing the checker.
    GameStatus s3 = statusOf("7k/6pp/8/8/8/8/8/R5K1 w - - 0 1");
    CHECK(s3.result == GameResult::ONGOING, "no mate yet");
}

void testStalemate() {
    GameStatus a = statusOf("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(a.result == GameResult::STALEMATE && !a.inCheck, "stalemate (queen f7)");
    GameStatus b = statusOf("k7/2Q5/1K6/8/8/8/8/8 b - - 0 1");
    CHECK(b.result == GameResult::STALEMATE, "stalemate (queen c7, king b6)");
    Position p = fromFEN("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(isStalemate(p) && !isCheckmate(p), "isStalemate true, isCheckmate false");
    CHECK(statusOf("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1").result == GameResult::ONGOING,
          "same placement with White to move is not stalemate");
    CHECK(std::string(toString(GameResult::STALEMATE)) == "stalemate", "toString(STALEMATE)");
}

void testHasLegalMoveMatchesGenerator() {
    const char* fens[] = {START, "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
                          "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", "4k3/8/8/8/8/8/4R3/4K3 b - - 0 1",
                          "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"};
    for (const char* f : fens) {
        Position pos = fromFEN(f);
        MoveList m;
        generateLegalMoves(pos, m);
        CHECK(hasLegalMove(pos) == !m.empty(), std::string("hasLegalMove agrees with the generator: ") + f);
    }
}

// --- Dead positions --------------------------------------------------------------------------------

static bool dead(const std::string& fen) { return isDeadPosition(fromFEN(fen)); }

void testDeadPositions() {
    CHECK(dead("4k3/8/8/8/8/8/8/4K3 w - - 0 1"), "K v K");
    CHECK(dead("4k3/8/8/8/8/8/8/3BK3 w - - 0 1"), "K+B v K");
    CHECK(dead("4k3/8/8/8/8/8/8/3NK3 w - - 0 1"), "K+N v K");
    CHECK(dead("3bk3/8/8/8/8/8/8/4K3 w - - 0 1"), "K v K+B");
    CHECK(dead("4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1"), "K+B v K+B, bishops on the same colour (c1, f8)");
    CHECK(dead("4k3/8/8/8/8/B7/8/2B1K3 w - - 0 1"), "K+B+B v K, both bishops dark (c1, a3)");
    CHECK(statusOf("4k3/8/8/8/8/8/8/4K3 w - - 0 1").result == GameResult::DEAD_POSITION,
          "K v K reported as DEAD_POSITION by evaluateGameStatus");
}

void testNotDeadPositions() {
    CHECK(!dead("2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1"), "K+B v K+B on opposite colours can mate (helpmate)");
    CHECK(!dead("4k3/8/8/8/8/8/8/2BBK3 w - - 0 1"), "K+B+B v K with bishops on both colours");
    CHECK(!dead("4k3/8/8/8/8/8/8/1NN1K3 w - - 0 1"), "K+N+N v K is NOT dead (mate is possible)");
    CHECK(!dead("4k3/8/8/8/8/8/8/2B1K1n1 w - - 0 1"), "K+B v K+N is not dead");
    CHECK(!dead("4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1"), "K+N v K+N is not dead");
    CHECK(!dead("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"), "K+R v K");
    CHECK(!dead("4k3/8/8/8/8/8/8/3QK3 w - - 0 1"), "K+Q v K");
    CHECK(!dead("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"), "K+P v K (the pawn can promote)");
    CHECK(!dead(START), "start position");
}

void testDeadPositionsMatchPythonChess() {
    // is_insufficient_material() from python-chess 1.11.2 on random sparse positions.
    struct Case { const char* fen; bool dead; };
    const Case cases[] = {
        {"8/2k5/5K2/8/8/8/8/8 w - - 0 1", true},
        {"1N6/8/1k6/8/7K/8/8/3n4 w - - 0 1", false},
        {"2r5/4K2N/8/8/8/1k6/3Q4/8 w - - 0 1", false},
        {"5k2/7n/8/8/3K4/8/2N5/8 w - - 0 1", false},
        {"8/8/8/8/2B5/7k/2B5/2K5 w - - 0 1", true},
        {"4k3/8/8/8/8/8/8/2K5 w - - 0 1", true},
        {"8/8/6K1/8/k7/8/8/8 w - - 0 1", true},
        {"3n4/b7/8/8/1K6/8/7n/7k w - - 0 1", false},
        {"8/8/8/n7/2K5/5k2/8/8 w - - 0 1", true},
        {"8/3N4/8/7K/8/8/3k4/5r2 w - - 0 1", false},
        {"8/4N3/7k/6N1/6K1/6n1/8/8 w - - 0 1", false},
        {"8/4K3/8/8/8/8/8/6k1 w - - 0 1", true},
        {"8/8/8/5K2/5b2/8/1k6/8 w - - 0 1", true},
        {"8/8/3K4/8/7n/8/3k4/5q2 w - - 0 1", false},
        {"8/5k2/8/3K4/7B/n7/8/8 w - - 0 1", false},
        {"8/8/k4B2/8/8/7K/8/8 w - - 0 1", true},
        {"5k2/8/7K/8/8/8/8/8 w - - 0 1", true},
        {"4k3/8/3K4/5b2/8/8/8/8 w - - 0 1", true},
        {"8/8/8/6k1/B7/4K3/8/8 w - - 0 1", true},
        {"8/8/5n2/8/4K1bk/2n5/8/8 w - - 0 1", false},
        {"3k2K1/8/8/8/8/8/8/8 w - - 0 1", true},
        {"8/2N5/8/8/7K/2k5/B7/2r5 w - - 0 1", false},
        {"8/8/8/5K2/7k/8/8/8 w - - 0 1", true},
        {"8/8/8/K2k4/5q2/8/8/8 w - - 0 1", false},
        {"8/1K6/8/8/1B4k1/8/8/8 w - - 0 1", true},
        {"5b2/8/8/3KN3/8/8/8/3k3B w - - 0 1", false},
        {"8/8/b7/4nK2/8/8/1k6/8 w - - 0 1", false},
        {"8/3k3N/8/8/8/1K5n/7P/8 w - - 0 1", false},
    };
    for (const Case& c : cases) {
        CHECK(dead(c.fen) == c.dead, std::string("dead-position verdict for ") + c.fen +
              " expected " + (c.dead ? "dead" : "not dead"));
    }
}

// --- Fifty / seventy-five move rules -------------------------------------------------------------------

void testFiftyMoveRule() {
    auto st = [](int clock) {
        return statusOf("4k3/8/8/8/8/8/R7/4K3 w - - " + std::to_string(clock) + " 60");
    };
    CHECK(!st(99).fiftyMoveClaimable, "clock 99: no claim yet");
    GameStatus s100 = st(100);
    CHECK(s100.fiftyMoveClaimable && s100.result == GameResult::ONGOING,
          "clock 100: draw claimable but the game is NOT over");
    CHECK(st(149).fiftyMoveClaimable && st(149).result == GameResult::ONGOING, "clock 149: still only claimable");
    CHECK(isFiftyMoveClaimable(fromFEN("4k3/8/8/8/8/8/R7/4K3 w - - 100 60")), "isFiftyMoveClaimable(100)");
    CHECK(!isFiftyMoveClaimable(fromFEN("4k3/8/8/8/8/8/R7/4K3 w - - 99 60")), "isFiftyMoveClaimable(99) is false");
}

void testSeventyFiveMoveRule() {
    GameStatus s = statusOf("4k3/8/8/8/8/8/R7/4K3 w - - 150 80");
    CHECK(s.result == GameResult::SEVENTY_FIVE_MOVE_RULE && s.isOver(), "clock 150: automatic draw");
    CHECK(isSeventyFiveMoveDraw(fromFEN("4k3/8/8/8/8/8/R7/4K3 w - - 150 80")), "isSeventyFiveMoveDraw(150)");
    CHECK(!isSeventyFiveMoveDraw(fromFEN("4k3/8/8/8/8/8/R7/4K3 w - - 149 80")), "isSeventyFiveMoveDraw(149) false");
    CHECK(statusOf("4k3/8/8/8/8/8/R7/4K3 w - - 200 80").result == GameResult::SEVENTY_FIVE_MOVE_RULE,
          "clock beyond 150 stays a draw");
}

void testCheckmateTakesPrecedenceOverClock() {
    const std::string mate = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - ";
    CHECK(statusOf(mate + "150 3").result == GameResult::CHECKMATE, "checkmate on the 150th halfmove is still checkmate");
    GameStatus s100 = statusOf(mate + "100 3");
    CHECK(s100.result == GameResult::CHECKMATE && !s100.fiftyMoveClaimable, "no fifty-move claim in a mate position");
    CHECK(!isFiftyMoveClaimable(fromFEN(mate + "100 3")), "isFiftyMoveClaimable false when mated");
    CHECK(!isSeventyFiveMoveDraw(fromFEN(mate + "150 3")), "isSeventyFiveMoveDraw false when mated");
    CHECK(statusOf("7k/5Q2/6K1/8/8/8/8/8 b - - 150 90").result == GameResult::STALEMATE,
          "stalemate is reported as stalemate at clock 150");
    CHECK(statusOf("4k3/8/8/8/8/8/8/4K3 w - - 150 90").result == GameResult::DEAD_POSITION,
          "dead position takes precedence over the 75-move rule");
}

// --- Repetition ---------------------------------------------------------------------------------------------

static const std::vector<std::string> SHUFFLE = {"g1f3", "g8f6", "f3g1", "f6g8"};

void testThreefoldAndFivefold() {
    Game g(START);
    CHECK(g.occurrences() == 1, "start position counts once");
    g.playAll(SHUFFLE);
    CHECK(g.occurrences() == 2 && !g.status().threefoldClaimable, "after one cycle: seen twice, no claim yet");
    g.playAll(SHUFFLE);
    CHECK(g.occurrences() == 3, "after two cycles: seen three times");
    CHECK(g.status().threefoldClaimable && g.status().result == GameResult::ONGOING,
          "third occurrence: draw claimable, game NOT over");
    g.playAll(SHUFFLE);
    CHECK(g.occurrences() == 4 && g.status().result == GameResult::ONGOING && g.status().threefoldClaimable,
          "fourth occurrence: still claimable only");
    g.playAll(SHUFFLE);
    CHECK(g.occurrences() == 5, "fifth occurrence counted");
    CHECK(g.status().result == GameResult::FIVEFOLD_REPETITION && g.status().isOver(),
          "fifth occurrence: automatic draw");
}

void testIntermediatePositionsCount() {
    Game g(START);
    g.play("g1f3");
    CHECK(g.occurrences() == 1, "a new position counts once");
    g.playAll({"g8f6", "f3g1", "f6g8", "g1f3"});
    CHECK(g.occurrences() == 2, "1.Nf3 position reached again after a full cycle");
}

void testHistoryPopRestoresCounts() {
    Game g(START);
    g.playAll(SHUFFLE);
    g.playAll(SHUFFLE);
    CHECK(g.history.size() == 9, "history holds the start plus eight positions");
    CHECK(g.occurrences() == 3, "three occurrences before taking back");
    g.history.pop();
    CHECK(g.history.size() == 8, "pop removes one key");
    g.history.push(g.pos);
    CHECK(g.history.size() == 9, "push adds one key back");
    GameHistory empty;
    CHECK(empty.occurrences(g.pos) == 0, "an empty history has no occurrences");
    empty.pop();  // must be harmless
    CHECK(empty.size() == 0, "pop on an empty history is a no-op");
}

void testSideToMoveMatters() {
    CHECK(!(makeRepetitionKey(fromFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1")) ==
            makeRepetitionKey(fromFEN("4k3/8/8/8/8/8/8/4K3 b - - 0 1"))),
          "same placement, different side to move: different position");
}

void testCastlingRightsMatter() {
    // Kings walk away and back: the placement repeats, but the castling rights are gone, so the
    // position is NOT the start position again (FIDE 9.2).
    Game g("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    g.playAll({"e1e2", "e8e7", "e2e1", "e7e8"});
    CHECK(g.occurrences() == 1, "placement repeated but castling rights were lost: not a repetition");
    g.playAll({"e1e2", "e8e7", "e2e1", "e7e8"});
    CHECK(g.occurrences() == 2, "the rights-less position now genuinely repeats");
}

void testStaleCastlingRightsIgnored() {
    CHECK(makeRepetitionKey(fromFEN("4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1")) ==
          makeRepetitionKey(fromFEN("4k3/8/8/8/8/8/8/4K3 w - - 0 1")),
          "rights written in a FEN but impossible (no rooks) do not make a different position");
    CHECK(effectiveCastlingRights(fromFEN("4k3/8/8/8/8/8/8/R3K3 w KQ - 0 1")) == WHITE_QUEENSIDE,
          "only the queenside right is real (no rook on h1)");
    CHECK(effectiveCastlingRights(fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")) ==
          (WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE), "all four rights real");
    CHECK(effectiveCastlingRights(fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1")) == 0, "no stored rights, none effective");
}

void testEnPassantInRepetitionKey() {
    // (a) An en passant capture is really available: the key records it.
    Game a("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1");
    a.play("e2e4");
    CHECK(a.pos.enPassantSquare() == E3, "double push stores e3 (FEN convention)");
    CHECK(effectiveEnPassantSquare(a.pos) == E3, "Black really can capture en passant: effective ep = e3");
    CHECK(!(makeRepetitionKey(a.pos) == makeRepetitionKey(fromFEN("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1"))),
          "with a capture available, the position differs from the same placement without ep");

    // (b) No adjacent pawn: the stored ep square is irrelevant for repetition.
    Game b("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    b.play("e2e4");
    CHECK(b.pos.enPassantSquare() == E3 && effectiveEnPassantSquare(b.pos) == NO_SQUARE,
          "ep stored but no capture possible: effective ep is none");
    CHECK(makeRepetitionKey(b.pos) == makeRepetitionKey(fromFEN("4k3/8/8/8/4P3/8/8/4K3 b - - 0 1")),
          "no capture possible: same position as without the ep square");

    // (c) A pawn is adjacent but the capture is illegal (horizontal pin): also ignored.
    Position pinned = fromFEN("8/8/8/8/k2pP2R/8/8/7K b - e3 0 1");
    CHECK(effectiveEnPassantSquare(pinned) == NO_SQUARE, "pinned en passant capture is not a real option");
    CHECK(makeRepetitionKey(pinned) == makeRepetitionKey(fromFEN("8/8/8/8/k2pP2R/8/8/7K b - - 0 1")),
          "illegal ep capture: key equals the position without ep");
}

void testHalfmoveClockBoundsTheLookBack() {
    // After a pawn move or capture (clock 0) nothing earlier can recur, so older keys are ignored.
    Position p = fromFEN("4k3/8/8/8/8/8/R7/4K3 w - - 0 60");
    GameHistory h;
    h.reset(p);
    h.push(p);  // artificial duplicate key sitting "before" a reset of the clock
    CHECK(h.occurrences(p) == 1, "with halfmove clock 0 only the current position is examined");
    Position q = fromFEN("4k3/8/8/8/8/8/R7/4K3 w - - 2 60");
    GameHistory h2;
    h2.reset(q);
    h2.push(q);
    h2.push(q);
    CHECK(h2.occurrences(q) == 2, "with clock 2 the position two plies back is examined (and counts)");
}

int main() {
    initAttackTables();

    testCheckmate();
    testCheckIsNotMate();
    testStalemate();
    testHasLegalMoveMatchesGenerator();
    testDeadPositions();
    testNotDeadPositions();
    testDeadPositionsMatchPythonChess();
    testFiftyMoveRule();
    testSeventyFiveMoveRule();
    testCheckmateTakesPrecedenceOverClock();
    testThreefoldAndFivefold();
    testIntermediatePositionsCount();
    testHistoryPopRestoresCounts();
    testSideToMoveMatters();
    testCastlingRightsMatter();
    testStaleCastlingRightsIgnored();
    testEnPassantInRepetitionKey();
    testHalfmoveClockBoundsTheLookBack();

    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed.\n";
    if (testsFailed == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << testsFailed << " TESTS FAILED\n";
    return 1;
}