// Stage 18 tests: advanced evaluation (tapered evaluation and the individual evaluation features).
//
//   test_stage18 [section ...]     sections: flags basic phase taper symmetry isolation terms bounds search tactics
//
// flags      parsing of feature names ("taper,passed", "all,-space", "default"), printing, the process-wide setting.
// basic      with no features the new code equals a frozen copy of the Stage 10 evaluation, number for number, on
//            thousands of positions (the proof that Stage 18 changed nothing until a feature is switched on).
// phase      the game phase: start position 24, bare kings 0, monotone under captures, capped at 24.
// taper      at phase 24 and phase 0 the tapered evaluation equals the basic one; in between it is the exact blend.
// symmetry   colour mirror, left-right mirror and side-to-move negation hold for every feature (the null-move guard of
//            Stage 17 relies on the last one: the evaluation has no tempo term).
// isolation  each feature changes only its own term(s); without TAPER the features are exactly additive.
// terms      hand-computed values for every feature (outposts, rooks, king zone, threats, space, passed, connected,
//            backward pawns) and the "off means zero" rule.
// bounds     no mate-sized values, empty boards and kingless positions do not crash.
// search     every feature set gives legal moves and PVs, restores the position, and finds mates.
// tactics    a tactical regression set is still solved with the default evaluation.
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/eval/evaluate.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/suite.hpp"
#include "../src/search/search.hpp"
#include "../src/search/tt.hpp"
#include "eval_stage10_reference.hpp"
#include "mirror.hpp"
#include <algorithm>
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

static bool isLegal(const Position& p, const Move& m) {
    MoveList ml; generateLegalMoves(p, ml);
    for (size_t i = 0; i < ml.size(); ++i) if (ml[i] == m) return true;
    return false;
}

// Restores the process-wide evaluation setting when a test ends.
struct FlagGuard {
    unsigned saved;
    FlagGuard() : saved(evalFlags()) {}
    ~FlagGuard() { setEvalFlags(saved); }
};

static const unsigned SINGLE_FLAGS[] = {EVAL_TAPER, EVAL_MOBILITY, EVAL_PASSED, EVAL_PAWNS, EVAL_OUTPOST, EVAL_ROOK, EVAL_KINGZONE, EVAL_THREATS, EVAL_SPACE};

// Positions: the perft suite plus random games that favour captures (so that endgames with few pieces appear).
static std::vector<std::string> samplePositions() {
    std::vector<std::string> v;
    for (const auto& c : standardPerftSuite()) v.push_back(c.fen);
    uint64_t rng = 0x9E3779B97F4A7C15ULL;
    auto next = [&]() { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return rng >> 33; };
    for (int game = 0; game < 60; ++game) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 170; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            Move pick = legal[next() % legal.size()];
            if (next() % 2 == 0) {
                for (size_t i = 0; i < legal.size(); ++i)
                    if (legal[i].isCapture()) { pick = legal[(i + next()) % legal.size()].isCapture() ? legal[(i + next()) % legal.size()] : legal[i]; break; }
            }
            makeMove(pos, pick);
            if (ply % 5 == 0) v.push_back(toFEN(pos));
        }
    }
    return v;
}

static const char* STM_SWAP_NOTE = "side to move swapped";

// The same position with the other side to move (en passant and clocks dropped: the evaluation ignores them).
static std::string swapSide(const std::string& fen) {
    auto f = fenSplit(fen, ' ');
    f[1] = f[1] == "w" ? "b" : "w";
    f[3] = "-";
    return f[0] + " " + f[1] + " " + f[2] + " " + f[3] + " " + f[4] + " " + f[5];
}

// =====================================================================================================
static void testFlags() {
    std::cout << "Flags\n";
    unsigned f = 99;
    CHECK(parseEvalFlags("none", f) && f == EVAL_BASIC, "none");
    CHECK(parseEvalFlags("all", f) && f == EVAL_ALL, "all");
    CHECK(parseEvalFlags("default", f) && f == evalDefault(), "default");
    CHECK(parseEvalFlags("taper,passed", f) && f == (EVAL_TAPER | EVAL_PASSED), "list");
    CHECK(parseEvalFlags("all,-space", f) && f == (EVAL_ALL & ~EVAL_SPACE), "all minus one");
    CHECK(parseEvalFlags("+rook,pawns", f) && f == (EVAL_ROOK | EVAL_PAWNS), "plus sign and order");
    CHECK(parseEvalFlags("", f) && f == EVAL_BASIC, "empty list is none");
    unsigned keep = EVAL_SPACE;
    f = keep;
    CHECK(!parseEvalFlags("taper,bogus", f) && f == keep, "unknown name rejected and the output left alone");
    for (unsigned flag : SINGLE_FLAGS) {
        unsigned g = 0;
        CHECK(parseEvalFlags(evalFlagsToString(flag), g) && g == flag, "name round trip for " + evalFlagsToString(flag));
    }
    CHECK(evalFlagsToString(EVAL_BASIC) == "none", "print none");
    CHECK(evalFlagsToString(EVAL_ALL).find("space") != std::string::npos && evalFlagsToString(EVAL_ALL).find("taper") != std::string::npos, "print all");
    CHECK((EVAL_ALL & EVAL_BASIC) == 0, "basic is empty");
    unsigned seen = 0;
    for (unsigned flag : SINGLE_FLAGS) { CHECK((seen & flag) == 0 && (flag & (flag - 1)) == 0, "flags are distinct single bits"); seen |= flag; }
    CHECK(seen == EVAL_ALL, "EVAL_ALL is the union of the flags");
    CHECK((evalDefault() & ~EVAL_ALL) == 0, "default is a set of known flags");
    {
        FlagGuard g;
        setEvalFlags(EVAL_TAPER | EVAL_SPACE);
        CHECK(evalFlags() == (EVAL_TAPER | EVAL_SPACE), "setEvalFlags");
        setEvalFlags(0xFFFFFFFFu);
        CHECK(evalFlags() == EVAL_ALL, "unknown bits are dropped");
    }
}

// =====================================================================================================
static void testBasicEqualsStage10(const std::vector<std::string>& fens) {
    std::cout << "Basic evaluation equals the frozen Stage 10 evaluation\n";
    CHECK(evalFlags() == EVAL_BASIC, "the library default is the basic evaluation");
    int bad = 0, badTerms = 0, badNew = 0;
    for (const auto& fen : fens) {
        const Position p = fromFEN(fen);
        const int ref = stage10ref::evaluate(p);
        if (evaluate(p, EVAL_BASIC) != ref || evaluate(p) != ref) { if (++bad < 4) std::cout << "  differs: " << fen << " ref " << ref << " new " << evaluate(p, EVAL_BASIC) << "\n"; }
        const EvalTerms a = stage10ref::evaluateTerms(p), b = evaluateTerms(p, EVAL_BASIC);
        if (a.material != b.material || a.pst != b.pst || a.pawns != b.pawns || a.bishopPair != b.bishopPair || a.mobility != b.mobility || a.kingSafety != b.kingSafety) ++badTerms;
        if (b.outposts != 0 || b.rooks != 0 || b.threats != 0 || b.space != 0) ++badNew;
        if (stage10ref::isEndgame(p) != isEndgame(p)) ++bad;
    }
    CHECK(bad == 0, "total evaluation identical on " + std::to_string(fens.size()) + " positions, differences " + std::to_string(bad));
    CHECK(badTerms == 0, "every old term identical");
    CHECK(badNew == 0, "new terms are zero without features");
}

// =====================================================================================================
static void testPhase(const std::vector<std::string>& fens) {
    std::cout << "Game phase\n";
    CHECK(gamePhase(fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")) == 24, "start position is 24");
    CHECK(gamePhase(fromFEN("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1")) == 0, "kings and pawns is 0");
    CHECK(gamePhase(fromFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1")) == 2, "a rook is 2");
    CHECK(gamePhase(fromFEN("4k3/8/8/8/8/8/8/Q3K3 w - - 0 1")) == 4, "a queen is 4");
    CHECK(gamePhase(fromFEN("4k3/8/8/8/8/8/8/1N2KB2 w - - 0 1")) == 2, "knight + bishop is 2");
    CHECK(gamePhase(fromFEN("QQQQk3/8/8/8/8/8/8/QQQQK3 w - - 0 1")) == 24, "capped at 24 with extra queens");
    CHECK(gamePhase(fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1")) == 8, "four rooks is 8");
    int bad = 0, count = 0;
    for (const auto& fen : fens) {
        const Position p = fromFEN(fen);
        const int ph = gamePhase(p);
        if (ph < 0 || ph > 24) ++bad;
        // Capturing a non-pawn piece can only lower the phase.
        MoveList ml; generateLegalMoves(p, ml);
        for (size_t i = 0; i < ml.size(); ++i) {
            Position q = p;
            const Move m = ml[i];
            if (!m.isCapture() || m.isPromotion()) continue;
            makeMove(q, m);
            if (gamePhase(q) > ph) ++bad;
            ++count;
        }
    }
    CHECK(bad == 0 && count > 1000, "phase in 0..24 and never rises on a capture (" + std::to_string(count) + " captures checked)");
}

// =====================================================================================================
static void testTaper(const std::vector<std::string>& fens) {
    std::cout << "Tapered evaluation\n";
    int full = 0, empty = 0, mid = 0, bad = 0;
    for (const auto& fen : fens) {
        const Position p = fromFEN(fen);
        const int ph = gamePhase(p);
        if (ph == 24) {
            ++full;
            // Phase 24 means every piece is on the board; that is a "middlegame" for the basic rule as well.
            if (!isEndgame(p) && evaluate(p, EVAL_TAPER) != evaluate(p, EVAL_BASIC)) ++bad;
        } else if (ph == 0) {
            ++empty;
            if (isEndgame(p) && evaluate(p, EVAL_TAPER) != evaluate(p, EVAL_BASIC)) ++bad;
        } else ++mid;
    }
    CHECK(bad == 0, "at phase 24 and phase 0 the tapered evaluation equals the basic one");
    CHECK(full > 20 && empty > 20 && mid > 500, "the sample covers all phases (" + std::to_string(full) + " / " + std::to_string(mid) + " / " + std::to_string(empty) + ")");

    // Hand case: White Kg1 + Ra1 against Ke8. Phase = 2. King table values: g1 mg 30 / eg -40, e8 mg 0 / eg -20,
    // so the difference is mg 30, eg -20 and the blend is (30*2 - 20*22) / 24 = -380 / 24 = -15 (truncated).
    const Position p = fromFEN("4k3/8/8/8/8/8/8/R5K1 w - - 0 1");
    CHECK(gamePhase(p) == 2, "hand case phase");
    CHECK(evaluateTerms(p, EVAL_BASIC).pst == -20, "basic: endgame table (no queens)");
    CHECK(evaluateTerms(p, EVAL_TAPER).pst == -15, "taper: exact blend of the two king tables");
    CHECK(evaluateTerms(fromFEN("4k3/8/8/8/8/8/8/R5K1 w - - 0 1"), EVAL_TAPER).material == 500, "material is not tapered");
    // King safety (a middlegame term) fades with the phase instead of switching off. White Kb1 behind a2 b2 c2 (+30),
    // Black Ke8 with no shield (-30): difference 60, blended with phase p as 60 * p / 24.
    const Position shield = fromFEN("4k3/8/8/8/8/8/PPP5/1K5R w - - 0 1");      // phase 2
    CHECK(evaluateTerms(shield, EVAL_BASIC).kingSafety == 0, "basic: no king safety in the endgame");
    CHECK(evaluateTerms(shield, EVAL_TAPER).kingSafety == 5, "taper at phase 2: 60 * 2 / 24 = 5");
    const Position shieldMg = fromFEN("r2qk3/8/8/8/8/8/PPP5/1K1QR3 w - - 0 1");  // phase 12
    CHECK(gamePhase(shieldMg) == 12 && evaluateTerms(shieldMg, EVAL_TAPER).kingSafety == 30, "taper at phase 12: 60 * 12 / 24 = 30");
}

// =====================================================================================================
static void testSymmetry(const std::vector<std::string>& fens) {
    std::cout << "Symmetry\n";
    std::vector<unsigned> sets(SINGLE_FLAGS, SINGLE_FLAGS + 9);
    sets.push_back(EVAL_ALL);
    sets.push_back(EVAL_ALL & ~EVAL_TAPER);
    sets.push_back(evalDefault());
    for (unsigned flags : sets) {
        int badColour = 0, badLR = 0, badStm = 0;
        for (const auto& fen : fens) {
            const Position p = fromFEN(fen);
            const int e = evaluate(p, flags);
            if (evaluate(fromFEN(mirrorFEN(fen)), flags) != e) { if (++badColour < 3) std::cout << "  colour asymmetry [" << evalFlagsToString(flags) << "]: " << fen << "\n"; }
            if (evaluate(fromFEN(flipFilesFEN(fen)), flags) != e) { if (++badLR < 3) std::cout << "  left-right asymmetry [" << evalFlagsToString(flags) << "]: " << fen << "\n"; }
            if (evaluate(fromFEN(swapSide(fen)), flags) != -e) { if (++badStm < 3) std::cout << "  " << STM_SWAP_NOTE << " [" << evalFlagsToString(flags) << "]: " << fen << "\n"; }
        }
        const std::string name = evalFlagsToString(flags);
        CHECK(badColour == 0, "colour symmetry, flags " + name + ", bad " + std::to_string(badColour));
        CHECK(badLR == 0, "left-right symmetry, flags " + name + ", bad " + std::to_string(badLR));
        CHECK(badStm == 0, "the value negates when only the side to move changes (no tempo term), flags " + name + ", bad " + std::to_string(badStm));
    }
    CHECK(evaluate(fromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"), EVAL_ALL) == 0, "start position is 0 with every feature");
}

// =====================================================================================================
static void testIsolation(const std::vector<std::string>& fens) {
    std::cout << "Each feature changes only its own terms\n";
    struct Own { unsigned flag; int EvalTerms::*term; };
    const Own own[] = {
        {EVAL_MOBILITY, &EvalTerms::mobility},
        {EVAL_PASSED, &EvalTerms::pawns},
        {EVAL_PAWNS, &EvalTerms::pawns},
        {EVAL_OUTPOST, &EvalTerms::outposts},
        {EVAL_ROOK, &EvalTerms::rooks},
        {EVAL_KINGZONE, &EvalTerms::kingSafety},
        {EVAL_THREATS, &EvalTerms::threats},
        {EVAL_SPACE, &EvalTerms::space},
    };
    int changedSomething[9] = {0};
    int bad = 0;
    int EvalTerms::*const all[] = {&EvalTerms::material, &EvalTerms::pst, &EvalTerms::pawns, &EvalTerms::bishopPair, &EvalTerms::mobility,
                                   &EvalTerms::kingSafety, &EvalTerms::outposts, &EvalTerms::rooks, &EvalTerms::threats, &EvalTerms::space};
    for (const auto& fen : fens) {
        const Position p = fromFEN(fen);
        const EvalTerms base = evaluateTerms(p, EVAL_BASIC);
        for (size_t k = 0; k < sizeof(own) / sizeof(own[0]); ++k) {
            const EvalTerms t = evaluateTerms(p, own[k].flag);
            for (int EvalTerms::*field : all) {
                const bool allowed = field == own[k].term;
                if (!allowed && t.*field != base.*field) { if (++bad < 4) std::cout << "  " << evalFlagsToString(own[k].flag) << " changed a foreign term on " << fen << "\n"; }
                if (allowed && t.*field != base.*field) ++changedSomething[k];
            }
        }
        // TAPER leaves a position of full or empty phase alone and, in between, never touches the new terms (they are zero).
        const EvalTerms tt = evaluateTerms(p, EVAL_TAPER);
        if (tt.outposts != 0 || tt.rooks != 0 || tt.threats != 0 || tt.space != 0) ++bad;
        // Additivity without TAPER: every feature adds its own change to the basic value.
        const unsigned noTaper = EVAL_ALL & ~EVAL_TAPER;
        const EvalTerms sum = evaluateTerms(p, noTaper);
        int expect = base.total();
        for (size_t k = 0; k < sizeof(own) / sizeof(own[0]); ++k) expect += evaluateTerms(p, own[k].flag).total() - base.total();
        if (sum.total() != expect) { if (++bad < 4) std::cout << "  not additive on " << fen << ": " << sum.total() << " vs " << expect << "\n"; }
    }
    CHECK(bad == 0, "features are isolated and additive on " + std::to_string(fens.size()) + " positions, violations " + std::to_string(bad));
    const char* names[] = {"mobility", "passed", "pawns", "outpost", "rook", "kingzone", "threats", "space"};
    for (int k = 0; k < 8; ++k) CHECK(changedSomething[k] > 20, std::string("the ") + names[k] + " feature actually changes the evaluation (" + std::to_string(changedSomething[k]) + " positions)");
}

// =====================================================================================================
static int term(const std::string& fen, unsigned flags, int EvalTerms::*field) { return evaluateTerms(fromFEN(fen), flags).*field; }

static void testTerms() {
    std::cout << "Hand-computed terms\n";
    // --- Outposts. Binary phase: queens + rooks on the board = middlegame (all numbers are the middlegame value).
    CHECK(term("r2qk3/8/8/3N4/4P3/8/8/R2QK3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 25, "knight on d5 supported by e4, no c/e pawns: +25");
    CHECK(term("r2qk3/8/8/3N4/4P3/8/8/R2QK3 w - - 0 1", EVAL_BASIC, &EvalTerms::outposts) == 0, "outposts are zero without the feature");
    CHECK(term("r2qk3/2p5/8/3N4/4P3/8/8/R2QK3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 0, "a black c-pawn can attack d5: no outpost");
    CHECK(term("r2qk3/4p3/8/3N4/4P3/8/8/R2QK3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 0, "a black e-pawn can attack d5: no outpost");
    CHECK(term("r2qk3/8/8/3N4/8/8/8/R2QK3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 0, "an unsupported knight is no outpost");
    CHECK(term("r2qk3/8/8/3B4/4P3/8/8/R2QK3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 12, "bishop outpost +12");
    CHECK(term("r2qk3/8/8/8/4P3/3N4/8/R2QK3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 0, "a knight on the third rank is not an outpost");
    CHECK(term("r2qk3/8/8/4p3/3n4/8/8/R2QK3 b - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == -25, "black knight d4 supported by e5 is a black outpost: -25");
    CHECK(term("4k3/8/8/3N4/4P3/8/8/4K3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 15, "endgame value of the knight outpost is 15");
    CHECK(term("4k3/8/8/3N4/4P3/8/8/4K3 w - - 0 1", EVAL_OUTPOST | EVAL_TAPER, &EvalTerms::outposts) == 15, "tapered at phase 1: (25*1 + 15*23)/24 = 15");

    // --- Rooks.
    CHECK(term("3qk3/8/8/8/8/8/8/3RK2Q w - - 0 1", EVAL_ROOK, &EvalTerms::rooks) == 18, "rook on an open file +18");
    CHECK(term("3qk3/3p4/8/8/8/8/8/3RK2Q w - - 0 1", EVAL_ROOK, &EvalTerms::rooks) == 9, "rook on a half-open file +9");
    CHECK(term("3qk3/8/8/8/8/8/3P4/3RK2Q w - - 0 1", EVAL_ROOK, &EvalTerms::rooks) == 0, "rook behind its own pawn: nothing");
    CHECK(term("3qk3/3R4/8/8/8/8/8/4K2Q w - - 0 1", EVAL_ROOK, &EvalTerms::rooks) == 30, "rook on the seventh against a king on the back rank: 18 (open) + 12");
    CHECK(term("3qk3/3R4/8/8/8/8/8/4K2Q w - - 0 1", EVAL_BASIC, &EvalTerms::rooks) == 0, "rooks are zero without the feature");
    CHECK(term("3rk2q/8/8/8/8/8/3R4/3QK3 b - - 0 1", EVAL_ROOK, &EvalTerms::rooks) == 0, "equal rook placement cancels");

    // --- King zone. Black Re8 (hits e2 and e1 = 2 squares, weight 3) and Nf2 (hits d1, weight 2) attack the white king zone:
    // units 5 + 3 = 8, two attackers, penalty 8*8/4 = 16.
    const std::string zone2 = "q3r2k/8/8/8/8/8/5n2/R2QK3 w - - 0 1";
    CHECK(term(zone2, EVAL_KINGZONE, &EvalTerms::kingSafety) - term(zone2, EVAL_BASIC, &EvalTerms::kingSafety) == -16, "two attackers: -16");
    CHECK(term("q3r2k/8/8/8/8/8/8/R2QK3 w - - 0 1", EVAL_KINGZONE, &EvalTerms::kingSafety) == term("q3r2k/8/8/8/8/8/8/R2QK3 w - - 0 1", EVAL_BASIC, &EvalTerms::kingSafety),
          "a single attacker costs nothing");
    // Two heavy attackers cost more than a rook and a knight: queen (5 + hits) + rook.
    const int heavy = term("4r2k/8/8/8/8/8/6q1/R2QK3 w - - 0 1", EVAL_KINGZONE, &EvalTerms::kingSafety) - term("4r2k/8/8/8/8/8/6q1/R2QK3 w - - 0 1", EVAL_BASIC, &EvalTerms::kingSafety);
    CHECK(heavy < -16, "queen and rook against the king cost more than rook and knight (" + std::to_string(heavy) + ")");
    CHECK(term("q3r2k/8/8/8/8/8/5n2/R2QK3 w - - 0 1", EVAL_KINGZONE, &EvalTerms::kingSafety) < 0, "the attacked side is worse");
    CHECK(term("q3r2k/8/8/8/8/8/5n2/R2QK3 b - - 0 1", EVAL_KINGZONE, &EvalTerms::kingSafety) == term(zone2, EVAL_KINGZONE, &EvalTerms::kingSafety), "the side to move does not matter");

    // --- Threats: a white pawn on e4 attacks the black knight on d5 (+28). The knight is defended by the queen and the
    // white queen's look at d5 is already counted as the pawn threat, so nothing else is added.
    CHECK(term("r2qk3/8/8/3n4/4P3/8/8/R2QK3 w - - 0 1", EVAL_THREATS, &EvalTerms::threats) == 28, "pawn attacks knight: +28");
    CHECK(term("r2qk3/8/8/3n4/4P3/8/8/R2QK3 w - - 0 1", EVAL_BASIC, &EvalTerms::threats) == 0, "threats are zero without the feature");
    CHECK(term("r2qk3/8/8/8/3n4/8/8/R2QK3 w - - 0 1", EVAL_THREATS, &EvalTerms::threats) == 0, "a knight attacked by nothing: no threat");
    CHECK(term("r2qk3/8/2b5/8/8/8/8/R2QK3 w - - 0 1", EVAL_THREATS, &EvalTerms::threats) == 0, "a bishop attacking nothing: no threat");
    CHECK(term("r2qk3/8/8/8/8/3p4/4N3/R2QK3 w - - 0 1", EVAL_THREATS, &EvalTerms::threats) == -28, "a black pawn attacks the white knight: -28 for White");

    // --- Space: 12 central squares behind the pawns, scaled by the pieces. White Ra1 Nb1 Qd1 (3 pieces): 12*3/4 = 9, black Ra8 Qd8 (2): 12*2/4 = 6.
    CHECK(term("r2qk3/8/8/8/8/8/8/RN1QK3 w - - 0 1", EVAL_SPACE, &EvalTerms::space) == 3, "space 9 - 6 = 3");
    CHECK(term("r2qk3/8/8/8/8/8/8/RN1QK3 w - - 0 1", EVAL_BASIC, &EvalTerms::space) == 0, "space is zero without the feature");
    CHECK(term("4k3/8/8/8/8/8/8/1N2K3 w - - 0 1", EVAL_SPACE, &EvalTerms::space) == 0, "no space term in the endgame (middlegame only)");
    CHECK(term("r2qk3/8/8/8/8/2p5/8/RN1QK3 w - - 0 1", EVAL_SPACE, &EvalTerms::space) == 2, "a black pawn on c3 controls b2 and d2: White has 11 safe squares, 11*3/4 = 8, minus Black's 6");

    // --- Passed pawns. K+P: White Kd1... pawn d6 passed, stop square d7. Numbers (endgame values, isolated pawn -12):
    //   free path: eg 100 - 26 (king distance: black king e8 is 1 from d7, white king e1 is 6) + 33 (free path) = 107, minus 12 = 95.
    CHECK(term("4k3/8/3P4/8/8/8/8/4K3 w - - 0 1", EVAL_PASSED, &EvalTerms::pawns) == 95, "passed pawn d6 with a close enemy king");
    CHECK(term("4k3/8/3P4/8/8/8/8/4K3 w - - 0 1", EVAL_BASIC, &EvalTerms::pawns) == 53, "basic: 65 - 12");
    //   blocked: black knight on d7: 100 - 26 - 25 = 49, minus 12 = 37.
    CHECK(term("4k3/3n4/3P4/8/8/8/8/4K3 w - - 0 1", EVAL_PASSED, &EvalTerms::pawns) == 37, "blocked passed pawn");
    //   with the white king on d5 and the black king on h8: king term ((4*5 - 2*3)*4)/2 = 28, so 100 + 28 + 33 - 12 = 149.
    CHECK(term("7k/8/3P4/3K4/8/8/8/8 w - - 0 1", EVAL_PASSED, &EvalTerms::pawns) == 149, "passed pawn escorted by its king");
    int last = -100000;
    bool rising = true;
    const char* ranks[] = {"4k3/8/8/8/8/8/3P4/4K3 w - - 0 1", "4k3/8/8/8/8/3P4/8/4K3 w - - 0 1", "4k3/8/8/8/3P4/8/8/4K3 w - - 0 1",
                           "4k3/8/8/3P4/8/8/8/4K3 w - - 0 1", "4k3/8/3P4/8/8/8/8/4K3 w - - 0 1"};
    for (const char* f : ranks) {
        const int v = term(f, EVAL_PASSED, &EvalTerms::pawns);
        if (v <= last) rising = false;
        last = v;
    }
    CHECK(rising, "a passed pawn is worth more the further it has advanced");

    // --- Connected and backward pawns (endgame values).
    CHECK(term("4k3/8/8/8/3PP3/8/8/4K3 w - - 0 1", EVAL_PAWNS, &EvalTerms::pawns) - term("4k3/8/8/8/3PP3/8/8/4K3 w - - 0 1", EVAL_BASIC, &EvalTerms::pawns) == 6, "phalanx d4+e4: +3 each");
    CHECK(term("4k3/8/8/8/8/2P5/3P4/4K3 w - - 0 1", EVAL_PAWNS, &EvalTerms::pawns) - term("4k3/8/8/8/8/2P5/3P4/4K3 w - - 0 1", EVAL_BASIC, &EvalTerms::pawns) == 2,
          "chain c3 supported by d2: +2 (rank 3 value)");
    CHECK(term("4k3/8/8/4p3/2P5/3P4/8/4K3 w - - 0 1", EVAL_PAWNS, &EvalTerms::pawns) - term("4k3/8/8/4p3/2P5/3P4/8/4K3 w - - 0 1", EVAL_BASIC, &EvalTerms::pawns) == -9,
          "c4 supported by d3 (+3) and d3 backward behind c4 facing e5 (-12): -9");
    // Doubled / isolated pawns keep their Stage 10 values.
    CHECK(term("4k3/8/8/8/8/P7/P7/4K3 w - - 0 1", EVAL_BASIC, &EvalTerms::pawns) == -15 - 12 * 2 + 10 + 15, "doubled and isolated pawns (basic): -15 - 24 + passed 10 + 15");

    // --- Cases added after mutation testing (each one pins a number the first test set left open).
    // Outpost on the fourth rank (relative rank 3 is the lowest outpost rank): Nd4 supported by e3, no black pawns.
    CHECK(term("r2qk3/8/8/8/3N4/4P3/8/R2QK3 w - - 0 1", EVAL_OUTPOST, &EvalTerms::outposts) == 25, "knight on d4 supported by e3: +25");
    // Half-open file in the endgame: Rd1 against a black pawn on d7, no queens (endgame value 6; the basic value is 0).
    CHECK(term("4k3/3p4/8/8/8/8/8/3RK3 w - - 0 1", EVAL_ROOK, &EvalTerms::rooks) == 6, "half-open rook in the endgame: +6");
    // Passed pawn on its third rank still gets the king-distance and free-path terms: eg 20 + (4*5 - 3*3)*1/2 = 5, + 20/3 = 6, minus isolated 12.
    CHECK(term("4k3/8/8/8/8/3P4/8/4K3 w - - 0 1", EVAL_PASSED, &EvalTerms::pawns) == 19, "passed pawn d3: 20 + 5 + 6 - 12");
    // King zone with a queen: Qg2 hits f1 f2 e2 d2 (5 + 4 = 9), Re8 hits e2 e1 (3 + 2 = 5): units 14, penalty 14*14/4 = 49.
    CHECK(term("4r2k/8/8/8/8/8/6q1/R2QK3 w - - 0 1", EVAL_KINGZONE, &EvalTerms::kingSafety) - term("4r2k/8/8/8/8/8/6q1/R2QK3 w - - 0 1", EVAL_BASIC, &EvalTerms::kingSafety) == -49,
          "queen + rook against the king: -49");
    // A rook attacked by a pawn and by a knight at once is counted once (the pawn threat; endgame value 18, a second
    // count for the knight would add 10): Pe4 and Nb4 both attack d5.
    CHECK(term("4k3/8/8/3r4/1N2P3/8/8/4K3 w - - 0 1", EVAL_THREATS, &EvalTerms::threats) == 18, "pawn and knight attack the same rook: only 18");
    // A rook attacking a queen: Rd1 against Qd7, both protected by their kings: +8 (endgame value) for White, nothing for the
    // queen's attack on the rook.
    CHECK(term("4k3/3q4/8/8/8/8/8/3RK3 w - - 0 1", EVAL_THREATS, &EvalTerms::threats) == 8, "rook attacks queen: +8");

    // --- Mobility relative to an average piece, endgame weights (no queens: phase 0): knight 4*(8-4), bishop 5*(13-5),
    // rook 4*(14-6), queen 2*(27-9) on d4 of an otherwise empty board. The basic values are 4*8, 3*13, 2*14 and 1*27.
    const struct { const char* fen; int mobility, basic; } mob[] = {
        {"4k3/8/8/8/3N4/8/8/4K3 w - - 0 1", 16, 32},
        {"4k3/8/8/8/3B4/8/8/4K3 w - - 0 1", 40, 39},
        {"4k3/8/8/8/3R4/8/8/4K3 w - - 0 1", 32, 28},
        {"4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1", 36, 27},
    };
    for (const auto& m : mob) {
        CHECK(term(m.fen, EVAL_MOBILITY, &EvalTerms::mobility) == m.mobility, std::string("relative mobility: ") + m.fen);
        CHECK(term(m.fen, EVAL_BASIC, &EvalTerms::mobility) == m.basic, std::string("basic mobility: ") + m.fen);
    }
}

// =====================================================================================================
static void testBounds(const std::vector<std::string>& fens) {
    std::cout << "Bounds and robustness\n";
    int outOfRange = 0;
    std::vector<unsigned> sets(SINGLE_FLAGS, SINGLE_FLAGS + 9);
    sets.push_back(EVAL_ALL);
    for (const auto& fen : fens) {
        const Position p = fromFEN(fen);
        for (unsigned f : sets) {
            const int e = evaluate(p, f);
            if (e > 20000 || e < -20000 || isMateScore(e)) ++outOfRange;
        }
    }
    CHECK(outOfRange == 0, "no mate-sized or extreme values with any feature");
    Position empty;
    empty.clear();
    for (unsigned f : sets) CHECK(evaluate(empty, f) == 0, "empty board is 0 with flags " + evalFlagsToString(f));
    Position noBlackKing;
    noBlackKing.clear();
    noBlackKing.putPiece(WHITE_KING, 4);
    noBlackKing.putPiece(WHITE_QUEEN, 3);
    noBlackKing.putPiece(BLACK_ROOK, 56);
    noBlackKing.putPiece(WHITE_PAWN, 20);
    for (unsigned f : sets) { const int e = evaluate(noBlackKing, f); CHECK(e > -20000 && e < 20000, "a position without a black king does not crash or explode, flags " + evalFlagsToString(f)); }
    // The explicit overloads do not depend on, or change, the global setting.
    FlagGuard g;
    setEvalFlags(EVAL_SPACE);
    const Position p = fromFEN("r2qk3/8/8/8/8/8/8/RN1QK3 w - - 0 1");
    CHECK(evaluate(p, EVAL_BASIC) == stage10ref::evaluate(p), "explicit flags ignore the global setting");
    CHECK(evaluate(p) == evaluate(p, EVAL_SPACE), "the global setting is used by evaluate(pos)");
    CHECK(evalFlags() == EVAL_SPACE, "explicit overloads leave the global setting alone");
}

// =====================================================================================================
static void testSearch() {
    std::cout << "Search with every evaluation\n";
    FlagGuard guard;
    std::vector<unsigned> sets(SINGLE_FLAGS, SINGLE_FLAGS + 9);
    sets.push_back(EVAL_ALL);
    sets.push_back(evalDefault());
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10",
        "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
    };
    struct Mate { const char* fen; int mate; };
    const Mate mates[] = {
        {"6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", 1},
        {"7k/5Q2/6K1/8/8/8/8/8 w - - 0 1", 1},
        {"k7/2K5/8/8/8/8/8/1R6 w - - 0 1", 1},
        {"k7/8/1K6/8/8/8/8/7R w - - 0 1", 1},
    };
    for (unsigned flags : sets) {
        setEvalFlags(flags);
        const std::string name = evalFlagsToString(flags);
        for (const char* fen : fens) {
            Position pos = fromFEN(fen);
            const std::string before = toFEN(pos);
            TranspositionTable tt(4);
            SearchLimits lim; lim.maxNodes = 4000; lim.tt = &tt; lim.options = SearchOptions::engineDefault();
            const SearchResult r = searchIterative(pos, lim);
            CHECK(toFEN(pos) == before, "position restored, flags " + name);
            CHECK(r.hasMove && isLegal(pos, r.bestMove), "legal best move, flags " + name);
            Position walk = pos;
            bool ok = true;
            for (const Move& m : r.pv) { if (!isLegal(walk, m)) { ok = false; break; } makeMove(walk, m); }
            CHECK(ok, "legal PV, flags " + name);
            CHECK(!isMateScore(r.score) || std::abs(r.score) <= MATE_SCORE, "score in range, flags " + name);
        }
        for (const Mate& m : mates) {
            Position pos = fromFEN(m.fen);
            SearchLimits lim; lim.maxNodes = 20000; lim.options = SearchOptions::engineDefault();
            const SearchResult r = searchIterative(pos, lim);
            CHECK(mateInMoves(r.score) == m.mate, std::string("mate found with flags ") + name + ": " + m.fen + " got " + std::to_string(mateInMoves(r.score)));
        }
    }
    // Fixed depth, no pruning: a different evaluation may change the move but must keep the search self-consistent
    // (alpha-beta equals minimax with the same evaluation).
    setEvalFlags(EVAL_ALL);
    int bad = 0;
    for (const char* fen : fens) {
        Position a = fromFEN(fen), b = fromFEN(fen);
        const SearchResult ra = searchAlphaBeta(a, 3), rm = searchMinimax(b, 3);
        if (ra.score != rm.score) ++bad;
    }
    CHECK(bad == 0, "alpha-beta equals minimax under the full evaluation");
}

// =====================================================================================================
struct TacticCase { const char* id; const char* fen; const char* uci; };
static const TacticCase TACTICS[] = {
    {"WAC.008", "r4q1k/p2bR1rp/2p2Q1N/5p2/5p2/2P5/PP3PPP/R5K1 w - - 0 1", "e7f7"},
    {"WAC.020", "r2qkb1r/1ppb1ppp/p7/4p3/P1Q1P3/2P5/5PPP/R1B2KNR b kq - 0 1", "d7b5"},
    {"WAC.034", "7k/1b1r2p1/p6p/1p2qN2/3bP3/3Q4/P5PP/1B1R3K b - - 0 1", "d4g1"},
    {"WAC.047", "r1b2rk1/pp2bppp/2n1pn2/q5B1/2BP4/2N2N2/PP2QPPP/2R2RK1 b - - 0 1", "c6d4"},
    {"WAC.061", "3qrbk1/ppp1r2n/3pP2p/3P4/2P4P/1P3Q2/PB6/R4R1K w - - 0 1", "f3f7"},
    {"WAC.074", "5r1k/pp4pp/2p5/2b1P3/4Pq2/1PB1p3/P3Q1PP/3N2K1 b - - 0 1", "f4f1"},
    {"WAC.089", "1r3b1k/p4rpp/4pp2/3q4/2ppbPPQ/6RK/PP5P/2B1NR2 b - - 0 1", "g7g5"},
    {"WAC.104", "b4r1k/pq2rp2/1p1bpn1p/3PN2n/2P2P2/P2B3K/1B2Q2N/3R2R1 w - - 0 1", "e2h5"},
    {"WAC.117", "3r1rk1/q4ppp/p1Rnp3/8/1p6/1N3P2/PP3QPP/3R2K1 b - - 0 1", "d6e4"},
    {"WAC.133", "r1b1k2r/1pp1q2p/p1n3p1/3QPp2/8/1BP3B1/P5PP/3R1RK1 w kq - 0 1", "g3h4"},
    {"WAC.148", "2r1k3/6pr/p1nBP3/1p3p1p/2q5/2P5/P1R4P/K2Q2R1 w - - 0 1", "g1g7"},
    {"WAC.160", "qn1kr2r/1pRbb3/pP5p/P2pP1pP/3N1pQ1/3B4/3B1PP1/R5K1 w - - 0 1", "g4d7"},
    {"WAC.173", "2r1b3/1pp1qrk1/p1n1P1p1/7R/2B1p3/4Q1P1/PP3PP1/3R2K1 w - - 0 1", "e3h6"},
    {"WAC.187", "6k1/5p2/p3p3/1p3qp1/2p1Qn2/2P1R3/PP1r1PPP/4R1K1 b - - 0 1", "f4h3"},
    {"WAC.204", "r1b1qrk1/1p3ppp/p1p5/3Nb3/5N2/P7/1P4PQ/K1R1R3 w - - 0 1", "e1e5"},
    {"WAC.219", "7k/p4q1p/1pb5/2p5/4B2Q/2P1B3/P6P/7K b - - 0 1", "f7f1"},
    {"WAC.238", "1k1r4/pp1r1pp1/4n1p1/2R5/2Pp1qP1/3P2QP/P4PB1/1R4K1 w - - 0 1", "g2b7"},
    {"WAC.255", "3r3r/p4pk1/5Rp1/3q4/1p1P2RQ/5N2/P1P4P/2b4K w - - 0 1", "f6g6"},
    {"WAC.274", "8/1p6/p5R1/k7/Prpp4/K7/1NP5/8 w - - 0 1", "g6b6"},
    {"WAC.290", "2k2r2/2p5/1pq5/p1p1n3/P1P2n1B/1R4Pp/2QR4/6K1 b - - 0 1", "f4e2"},
};

static void testTactics() {
    std::cout << "Tactical regression set\n";
    FlagGuard guard;
    for (unsigned flags : {static_cast<unsigned>(EVAL_BASIC), evalDefault()}) {
        setEvalFlags(flags);
        int solved = 0, total = 0;
        for (const TacticCase& c : TACTICS) {
            Position pos = fromFEN(c.fen);
            TranspositionTable tt(8);
            SearchLimits lim; lim.maxNodes = 50000; lim.tt = &tt; lim.options = SearchOptions::engineDefault();
            const SearchResult r = searchIterative(pos, lim);
            ++total;
            const bool ok = r.hasMove && r.bestMove.toString() == c.uci;
            if (ok) ++solved;
            else std::cout << "  unsolved " << c.id << " [" << evalFlagsToString(flags) << "]: played " << (r.hasMove ? r.bestMove.toString() : "-") << ", expected " << c.uci << "\n";
        }
        std::cout << "  [flags " << evalFlagsToString(flags) << ": " << solved << "/" << total << " solved at 50000 nodes]\n";
        CHECK(solved == total, "every position of the tactical regression set is solved with flags " + evalFlagsToString(flags));
    }
}

static void reportSpeed(const std::vector<std::string>& fens) {
    std::vector<Position> ps;
    for (const auto& f : fens) ps.push_back(fromFEN(f));
    std::cout << "Evaluation cost (informational, ns per call over " << ps.size() << " positions):\n";
    std::vector<unsigned> sets = {EVAL_BASIC};
    for (unsigned f : SINGLE_FLAGS) sets.push_back(f);
    sets.push_back(EVAL_ALL);
    for (unsigned flags : sets) {
        volatile int sink = 0;
        const int reps = 300;
        const auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < reps; ++r) for (const auto& p : ps) sink = sink + evaluate(p, flags);
        const auto t1 = std::chrono::steady_clock::now();
        std::cout << "  " << evalFlagsToString(flags) << ": " << std::chrono::duration<double, std::nano>(t1 - t0).count() / (double(reps) * ps.size()) << "\n";
        (void)sink;
    }
}

int main(int argc, char** argv) {
    initAttackTables();
    const auto t0 = std::chrono::steady_clock::now();
    auto lap = [&](const char* what) {
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "  [" << what << " done at " << s << " s]\n";
    };
    auto want = [&](const char* name) {
        if (argc <= 1) return true;
        for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == name) return true;
        return false;
    };
    const std::vector<std::string> fens = samplePositions();

    if (want("flags")) { testFlags(); lap("flags"); }
    if (want("basic")) { testBasicEqualsStage10(fens); lap("basic == Stage 10"); }
    if (want("phase")) { testPhase(fens); lap("phase"); }
    if (want("taper")) { testTaper(fens); lap("taper"); }
    if (want("symmetry")) { testSymmetry(fens); lap("symmetry"); }
    if (want("isolation")) { testIsolation(fens); lap("isolation"); }
    if (want("terms")) { testTerms(); lap("terms"); }
    if (want("bounds")) { testBounds(fens); lap("bounds"); }
    if (want("search")) { testSearch(); lap("search"); }
    if (want("tactics")) { testTactics(); lap("tactics"); }
    if (argc <= 1) reportSpeed(fens);

    std::cout << "\n" << (testsRun - testsFailed) << "/" << testsRun << " checks passed.";
    if (testsFailed == 0) std::cout << " ALL TESTS PASSED";
    std::cout << "\n";
    return testsFailed == 0 ? 0 : 1;
}