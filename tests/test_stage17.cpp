// Stage 17 tests: advanced search techniques.
//
// Score-preserving techniques (PVS, SEE ordering, aspiration windows) must give EXACTLY the plain search's score:
// those are identity tests. The heuristic techniques (reverse futility, null move, LMR, futility, razoring, check and
// singular extensions, SEE pruning in quiescence) change the tree on purpose: for those the tests prove that every
// technique actually fires, that the search stays legal and bounded, that mates keep their length and that a
// regression set of tactical positions is still solved.
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/board/zobrist.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/suite.hpp"
#include "../src/search/search.hpp"
#include "../src/search/see.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int testsRun = 0, testsFailed = 0;
#define CHECK(cond, msg) do { ++testsRun; if (!(cond)) { ++testsFailed; std::cout << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; } } while (0)

static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
static const char* KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
static const char* MIDDLEGAME = "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10";
static const char* ROOK_ENDGAME = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";

struct Rng {
    uint64_t s;
    uint64_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s >> 33; }
};

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

static bool pvIsLegal(const std::string& fen, const std::vector<Move>& pv) {
    Position walk = fromFEN(fen);
    for (const Move& m : pv) {
        if (!isLegal(walk, m)) return false;
        makeMove(walk, m);
    }
    return true;
}

static std::vector<std::string> positions(int games) {
    std::vector<std::string> v;
    for (const auto& c : standardPerftSuite()) v.push_back(c.fen);
    Rng rng{0x0F1E2D3C4B5A6978ULL};
    for (int game = 0; game < games; ++game) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 100; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            makeMove(pos, legal[rng.next() % legal.size()]);
            if (ply % 10 == 5) v.push_back(toFEN(pos));
        }
    }
    return v;
}

static const char* BENCH[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
};

static uint64_t addStats(SearchStats& t, const SearchStats& s) {
    t.ttCutoffs += s.ttCutoffs; t.rfpCutoffs += s.rfpCutoffs; t.razorCutoffs += s.razorCutoffs;
    t.nullTries += s.nullTries; t.nullCutoffs += s.nullCutoffs; t.lmrReductions += s.lmrReductions; t.lmrResearches += s.lmrResearches;
    t.futilityPrunes += s.futilityPrunes; t.pvsResearches += s.pvsResearches; t.aspirationResearches += s.aspirationResearches;
    t.checkExtensions += s.checkExtensions; t.singularTests += s.singularTests; t.singularExtensions += s.singularExtensions;
    t.seeQuiescencePrunes += s.seeQuiescencePrunes; t.repetitionDraws += s.repetitionDraws;
    return 0;
}

// =====================================================================================================
// Options
// =====================================================================================================

static void testOptions() {
    bool ok = false;
    const SearchOptions none = SearchOptions::fromString("none", &ok);
    CHECK(ok && none.toString() == "none", "\"none\" parses to no flags");
    CHECK(SearchOptions::none().toString() == "none", "a default-constructed SearchOptions has every technique off");

    const SearchOptions a = SearchOptions::fromString("pvs,null,lmr", &ok);
    CHECK(ok && a.pvs && a.nullMove && a.lmr && !a.futility && !a.aspiration, "list parsing");
    CHECK(SearchOptions::fromString(a.toString()).toString() == a.toString(), "toString / fromString round trip");
    const SearchOptions b = SearchOptions::fromString("pvs+null lmr", &ok);
    CHECK(ok && b.pvs && b.nullMove && b.lmr, "'+' and space also separate names");

    const SearchOptions all = SearchOptions::fromString("all", &ok);
    CHECK(ok && all.pvs && all.aspiration && all.reverseFutility && all.nullMove && all.lmr && all.futility && all.razoring &&
              all.checkExtension && all.singular && all.seeOrdering && all.seeQuiescence && all.repetition, "\"all\" switches every flag on");
    const SearchOptions minus = SearchOptions::fromString("all,-sing,-raz", &ok);
    CHECK(ok && !minus.singular && !minus.razoring && minus.nullMove, "a leading '-' removes a flag");
    SearchOptions::fromString("pvs,bogus", &ok);
    CHECK(!ok, "an unknown name is reported");

    const SearchOptions def = SearchOptions::engineDefault();
    CHECK(def.pvs && def.aspiration && def.reverseFutility && def.nullMove && def.lmr && def.futility && def.checkExtension &&
              def.seeOrdering && def.seeQuiescence && def.repetition, "the engine default keeps the techniques that passed the measurements");
    CHECK(!def.razoring && !def.singular, "razoring and singular extensions are not in the default (measured: no benefit)");
    CHECK(SearchOptions::fromString("default").toString() == def.toString(), "\"default\" is engineDefault()");
}

// =====================================================================================================
// With every option off the search is exactly the Stage 16 search
// =====================================================================================================

static void testDefaultsAreStage16() {
    struct Pin { const char* fen; int depth; uint64_t nodes; int score; };
    // Node counts recorded from the Stage 16 search (the Stage 16 test suite's node totals were unchanged by Stage 17).
    const Pin pins[] = {
        {START_FEN, 5, 18974, 38}, {KIWIPETE, 4, 57611, 75}, {MIDDLEGAME, 4, 87171, 19}, {ROOK_ENDGAME, 5, 9190, 9},
    };
    for (const Pin& p : pins) {
        Position pos = fromFEN(p.fen);
        const SearchResult plain = search(pos, p.depth);
        const SearchResult opt = searchOpt(pos, p.depth, SearchOptions::none());
        CHECK(plain.nodes == p.nodes && plain.score == p.score, std::string("search() unchanged since Stage 16: ") + p.fen);
        CHECK(opt.nodes == p.nodes && opt.score == p.score, std::string("searchOpt(none) is exactly search(): ") + p.fen);
        CHECK(opt.stats.nullTries == 0 && opt.stats.lmrReductions == 0 && opt.stats.futilityPrunes == 0 && opt.stats.rfpCutoffs == 0,
              "no technique fires with every option off");
    }
}

// =====================================================================================================
// Null move
// =====================================================================================================

static void testNullMove() {
    int checked = 0, bad = 0;
    Rng rng{0xBEEF};
    for (int game = 0; game < 200; ++game) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 80; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            makeMove(pos, legal[rng.next() % legal.size()]);
            if (isInCheck(pos, pos.sideToMove())) continue;  // the search never passes while in check
            const Position before = pos;
            const NullUndo u = makeNullMove(pos);
            ++checked;
            if (pos.sideToMove() == before.sideToMove() || pos.enPassantSquare() != NO_SQUARE || !pos.isConsistent() ||
                pos.baseKey() != recomputeBaseKey(pos)) ++bad;
            if (pos.halfmoveClock() != before.halfmoveClock() + 1) ++bad;
            if (pos.occupancyAll() != before.occupancyAll() || pos.castlingRights() != before.castlingRights()) ++bad;
            unmakeNullMove(pos, u);
            if (!(pos == before) || hashKey(pos) != hashKey(before)) ++bad;
        }
    }
    CHECK(checked > 5000 && bad == 0, "null move: make/unmake restores every field exactly (" + std::to_string(checked) + " positions, bad " + std::to_string(bad) + ")");

    // Concrete cases: ep square cleared, fullmove number after Black's null move, key equals the FEN position.
    Position a = fromFEN("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
    const NullUndo ua = makeNullMove(a);
    CHECK(toFEN(a) == "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 1 2", "null move from White: side flips, ep cleared, clock +1");
    CHECK(hashKey(a) == hashKey(fromFEN(toFEN(a))), "...and the hash equals that of the FEN position");
    unmakeNullMove(a, ua);
    CHECK(toFEN(a) == "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2", "unmake restores the ep square");
    Position b = fromFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    const NullUndo ub = makeNullMove(b);
    CHECK(toFEN(b) == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", "null move from Black: the fullmove number advances");
    unmakeNullMove(b, ub);
    CHECK(b.fullmoveNumber() == 1 && b.enPassantSquare() == E3, "...and unmake restores it");
}

// =====================================================================================================
// Static exchange evaluation
// =====================================================================================================

static Move findMove(const Position& pos, const std::string& uci) {
    MoveList ml; generateLegalMoves(pos, ml);
    for (size_t i = 0; i < ml.size(); ++i) if (ml[i].toString() == uci) return ml[i];
    std::cout << "SETUP FAILED: no move " << uci << " in " << toFEN(pos) << "\n";
    ++testsFailed;
    return Move{};
}

static int seeOf(const std::string& fen, const std::string& uci) {
    const Position pos = fromFEN(fen);
    return see(pos, findMove(pos, uci));
}

// Exhaustive reference: the best net gain on the square when EITHER side may stop and ANY legal capture may be chosen
// (not only the least valuable attacker; pins and checks are respected because only legal moves are tried).
static int refExchange(Position& pos, int sq) {
    MoveList ml; generateLegalMoves(pos, ml);
    int best = 0;
    for (size_t i = 0; i < ml.size(); ++i) {
        const Move m = ml[i];
        if (m.to != sq || !m.isCapture()) continue;
        int gain = m.flag == EN_PASSANT ? seePieceValue(PAWN) : seePieceValue(typeOf(m.captured));
        if (m.isPromotion()) gain += seePieceValue(m.promotion) - seePieceValue(PAWN);
        const UndoInfo u = makeMove(pos, m);
        const int v = gain - refExchange(pos, sq);
        unmakeMove(pos, m, u);
        best = std::max(best, v);
    }
    return best;
}

static void testSEE() {
    const int P = seePieceValue(PAWN), N = seePieceValue(KNIGHT), R = seePieceValue(ROOK), Q = seePieceValue(QUEEN);
    CHECK(seePieceValue(KING) == 20000 && P == 100, "piece values");

    CHECK(seeOf("4k3/8/8/3n4/4P3/8/8/4K3 w - - 0 1", "e4d5") == N, "pawn takes an undefended knight: +knight");
    CHECK(seeOf("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1", "d1d5") == P - Q, "queen takes a pawn defended by a pawn: pawn - queen");
    CHECK(seeOf("4k3/8/8/3p4/8/8/8/3RK3 w - - 0 1", "d1d5") == P, "rook takes an undefended pawn: +pawn");
    CHECK(seeOf("4k3/8/2p5/3p4/8/4N3/8/4K3 w - - 0 1", "e3d5") == P - N, "knight takes a pawn defended by a pawn: pawn - knight");
    CHECK(seeOf("4k3/8/2p5/3n4/8/4N3/8/4K3 w - - 0 1", "e3d5") == N - N, "knight takes a defended knight: an even trade");
    CHECK(seeOf("4k3/8/2p5/3q4/8/4N3/8/4K3 w - - 0 1", "e3d5") == Q - N, "knight takes a queen defended by a pawn: queen - knight");
    // X-ray: Re1 and Re2 against a pawn on e5 defended by Re8 only: Rxe5 Rxe5 Rxe5 -> wins the pawn and a rook for a rook.
    CHECK(seeOf("4r1k1/8/8/4p3/8/8/4R3/4R1K1 w - - 0 1", "e2e5") == P, "doubled rooks: the x-ray rook joins the exchange (+pawn)");
    CHECK(seeOf("4r1k1/8/8/4p3/8/8/4R3/6K1 w - - 0 1", "e2e5") == P - R, "a single rook against a pawn defended by a rook loses the exchange");
    // En passant: undefended capture wins a pawn.
    CHECK(seeOf("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6") == P, "en passant: +pawn");
    // En passant uncovers a slider: after exd6 the white rook on d1 sees d6 THROUGH the captured pawn's square (d5), so the
    // recapture Rxd6 can be answered by Rxd6. The expected value comes from the exhaustive exchange search.
    {
        const std::string fen = "3rk3/8/8/3pP3/8/8/8/3RK3 w - d6 0 1";
        Position pos = fromFEN(fen);
        const Move ep = findMove(pos, "e5d6");
        Position after = pos;
        makeMove(after, ep);
        const int ref = seePieceValue(PAWN) - refExchange(after, ep.to);
        CHECK(see(pos, ep) == ref && ref == P, "en passant: the captured pawn leaves the board, so the rook behind it joins the exchange");
    }
    // Promotion capture on an undefended rook: rook + queen - pawn.
    CHECK(seeOf("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q") == R + Q - P, "capturing promotion on an undefended rook");
    // The king may only recapture when nothing else attacks the square.
    CHECK(seeOf("4k3/4p3/8/8/8/8/8/4R1K1 w - - 0 1", "e1e7") == P - R, "rook takes a pawn defended only by the king: the king recaptures, the rook is lost");
    CHECK(seeOf("4k3/4p3/8/8/4R3/8/8/4R1K1 w - - 0 1", "e4e7") == P, "the king cannot recapture on a square the second rook still attacks");

    // Statistical agreement with the exhaustive exchange search on real captures.
    Rng rng{99};
    long n = 0, same = 0, signSame = 0;
    for (int g = 0; g < 200; ++g) {
        Position pos; pos.setStartingPosition();
        for (int ply = 0; ply < 100; ++ply) {
            MoveList ml; generateLegalMoves(pos, ml);
            if (ml.size() == 0) break;
            for (size_t i = 0; i < ml.size(); ++i) {
                const Move m = ml[i];
                if (!m.isCapture()) continue;
                int gain = m.flag == EN_PASSANT ? seePieceValue(PAWN) : seePieceValue(typeOf(m.captured));
                if (m.isPromotion()) gain += seePieceValue(m.promotion) - seePieceValue(PAWN);
                Position c = pos;
                makeMove(c, m);
                const int ref = gain - refExchange(c, m.to);
                const int s = see(pos, m);
                ++n;
                if (s == ref) ++same;
                if ((s >= 0) == (ref >= 0)) ++signSame;
                CHECK(seeAtLeast(pos, m, s) && !seeAtLeast(pos, m, s + 1), "seeAtLeast agrees with see");
            }
            makeMove(pos, ml[rng.next() % ml.size()]);
        }
    }
    const double exact = 100.0 * static_cast<double>(same) / static_cast<double>(n);
    const double sign = 100.0 * static_cast<double>(signSame) / static_cast<double>(n);
    std::cout << "  [SEE vs exhaustive exchange: " << n << " captures, exact " << exact << "%, same sign " << sign << "%]\n";
    CHECK(n > 50000, "many captures compared");
    CHECK(exact >= 99.0, "SEE equals the exhaustive exchange value in at least 99% of captures (pins, legality and non-LVA orders are the rest)");
    CHECK(sign >= 99.5, "SEE and the exhaustive exchange agree on 'wins or loses material' in at least 99.5%");
}

// =====================================================================================================
// Score-preserving techniques: identity with the plain search
// =====================================================================================================

static void testScorePreserving() {
    const std::vector<std::string> fens = positions(3);
    const char* configs[] = {"pvs", "seeord", "pvs,seeord"};
    for (const char* cfg : configs) {
        const SearchOptions o = SearchOptions::fromString(cfg);
        uint64_t plainNodes = 0, optNodes = 0;
        int compared = 0, mismatches = 0;
        SearchStats totals;
        for (const std::string& fen : fens) {
            Position pos = fromFEN(fen);
            const Position copy = pos;
            uint64_t lastNodes = 0;
            for (int depth = 1; depth <= 4; ++depth) {
                if (depth == 4 && lastNodes > 20000) break;
                const SearchResult ref = search(pos, depth);
                lastNodes = ref.nodes;
                const SearchResult r = searchOpt(pos, depth, o);
                ++compared;
                if (r.score != ref.score) { ++mismatches; std::cout << "  mismatch " << cfg << ": " << fen << " depth " << depth << " plain " << ref.score << " got " << r.score << "\n"; }
                CHECK(r.hasMove == ref.hasMove && (!r.hasMove || isLegal(pos, r.bestMove)), std::string(cfg) + ": move legal: " + fen);
                if (r.hasMove) CHECK(pvIsLegal(fen, r.pv), std::string(cfg) + ": PV legal: " + fen);
                if (depth == 4) { plainNodes += ref.nodes; optNodes += r.nodes; }
                addStats(totals, r.stats);
            }
            CHECK(pos == copy, std::string(cfg) + ": position restored");
        }
        CHECK(mismatches == 0, std::string(cfg) + " never changes the score (" + std::to_string(compared) + " searches, " + std::to_string(mismatches) + " mismatches)");
        std::cout << "  [" << cfg << " depth 4: plain " << plainNodes << " nodes, with option " << optNodes << " ("
                  << 100.0 * static_cast<double>(optNodes) / static_cast<double>(plainNodes) << "%)]\n";
        CHECK(optNodes < plainNodes, std::string(cfg) + " reduces the node count");
        if (o.pvs) CHECK(totals.pvsResearches > 0, "PVS: some zero-window results needed the re-search");
    }

    // PVS together with the table in the score-preserving mode.
    for (const char* fen : {MIDDLEGAME, KIWIPETE, ROOK_ENDGAME}) {
        Position pos = fromFEN(fen);
        const SearchResult ref = search(pos, 4);
        TranspositionTable tt(4);
        const SearchResult r = searchOpt(pos, 4, SearchOptions::fromString("pvs,seeord"), &tt, TT_EXACT_DEPTH);
        CHECK(r.score == ref.score, std::string("PVS + SEE ordering + exact-depth table keeps the score: ") + fen);
    }
}

// Aspiration windows: the score of EVERY iteration must equal the full-window iteration's score.
static void testAspirationIdentity() {
    const std::vector<std::string> fens = {BENCH[1], BENCH[2], BENCH[3], BENCH[4], BENCH[5], BENCH[6], BENCH[7],
                                           "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 9",
                                           "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", "r3r1k1/pp3ppp/2p5/3pP3/3P4/2P2Q2/P4PPP/R4RK1 b - - 0 20"};
    uint64_t researches = 0;
    int compared = 0, mismatches = 0;
    for (const std::string& fen : fens) {
        for (const char* cfg : {"pvs,asp", "asp"}) {
            if (std::string(cfg) == "asp" && fen != fens[0] && fen != fens[2] && fen != fens[8]) continue;  // the plain-alpha-beta variant on a few
            std::vector<int> ref, got;
            Position pos = fromFEN(fen);
            SearchLimits plain; plain.maxDepth = 5; plain.options = SearchOptions::fromString(std::string(cfg) == "asp" ? "none" : "pvs");
            searchIterative(pos, plain, [&](const SearchInfo& i) { ref.push_back(i.score); });
            SearchLimits asp; asp.maxDepth = 5; asp.options = SearchOptions::fromString(cfg);
            const SearchResult r = searchIterative(pos, asp, [&](const SearchInfo& i) { got.push_back(i.score); });
            researches += r.stats.aspirationResearches;
            CHECK(ref.size() == got.size(), std::string("same number of iterations: ") + fen);
            for (size_t d = 0; d < std::min(ref.size(), got.size()); ++d) {
                ++compared;
                if (ref[d] != got[d]) { ++mismatches; std::cout << "  mismatch " << cfg << " depth " << d + 1 << ": " << fen << " " << ref[d] << " vs " << got[d] << "\n"; }
            }
            CHECK(r.hasMove && isLegal(pos, r.bestMove) && pvIsLegal(fen, r.pv), std::string("aspiration result is legal: ") + fen);
        }
    }
    CHECK(mismatches == 0, "aspiration windows never change an iteration's score (" + std::to_string(compared) + " iterations, " + std::to_string(mismatches) + " mismatches)");
    CHECK(researches > 0, "aspiration: some windows were too narrow and re-searched (" + std::to_string(researches) + ")");

    // A position with an enormous score swing (a forced mate appears): the window must widen all the way.
    SearchLimits lim; lim.maxDepth = 6; lim.options = SearchOptions::fromString("asp");
    Position pos = fromFEN("r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1");
    const SearchResult wide = searchIterative(pos, lim);
    SearchLimits plainLim; plainLim.maxDepth = 6;
    const SearchResult plain = searchIterative(pos, plainLim);
    CHECK(wide.score == plain.score, "aspiration: a large swing gives the same score");
}

// =====================================================================================================
// Heuristic techniques: they fire, they stay legal and bounded
// =====================================================================================================

static void testTechniquesFire() {
    SearchStats t;
    uint64_t plainNodes = 0, defNodes = 0;
    const SearchOptions def = SearchOptions::engineDefault();
    for (const char* fen : BENCH) {
        Position pos = fromFEN(fen);
        const SearchResult plain = search(pos, 5);
        plainNodes += plain.nodes;
        const SearchResult r = searchOpt(pos, 5, def);
        defNodes += r.nodes;
        addStats(t, r.stats);
        CHECK(r.hasMove && isLegal(pos, r.bestMove) && pvIsLegal(fen, r.pv), std::string("default options: legal move and PV: ") + fen);
    }
    CHECK(t.rfpCutoffs > 0, "reverse futility pruning fires (" + std::to_string(t.rfpCutoffs) + ")");
    CHECK(t.nullTries > 0 && t.nullCutoffs > 0, "null move is tried and cuts off (" + std::to_string(t.nullCutoffs) + "/" + std::to_string(t.nullTries) + ")");
    CHECK(t.lmrReductions > 0 && t.lmrResearches > 0, "LMR reduces and re-searches (" + std::to_string(t.lmrReductions) + "/" + std::to_string(t.lmrResearches) + ")");
    CHECK(t.futilityPrunes > 0, "futility pruning fires (" + std::to_string(t.futilityPrunes) + ")");
    CHECK(t.checkExtensions > 0, "check extensions happen (" + std::to_string(t.checkExtensions) + ")");
    CHECK(t.seeQuiescencePrunes > 0, "SEE pruning in quiescence fires (" + std::to_string(t.seeQuiescencePrunes) + ")");
    CHECK(t.pvsResearches > 0, "PVS re-searches happen");
    std::cout << "  [depth 5 on 8 positions: plain " << plainNodes << " nodes, default options " << defNodes << " ("
              << 100.0 * static_cast<double>(defNodes) / static_cast<double>(plainNodes) << "%)]\n";
    CHECK(defNodes * 4 < plainNodes, "the default techniques cut the tree to under a quarter of the nodes at depth 5");

    // Razoring is off by default but must work when asked for.
    SearchStats rz;
    for (const char* fen : BENCH) {
        Position pos = fromFEN(fen);
        const SearchResult r = searchOpt(pos, 5, SearchOptions::fromString("pvs,raz"));
        addStats(rz, r.stats);
        CHECK(r.hasMove && isLegal(pos, r.bestMove), std::string("razoring: legal move: ") + fen);
    }
    CHECK(rz.razorCutoffs > 0, "razoring fires when enabled (" + std::to_string(rz.razorCutoffs) + ")");

    // Singular extensions need a table and depth: run an iterative search with a node limit.
    SearchStats sg;
    for (const char* fen : {MIDDLEGAME, KIWIPETE, BENCH[6]}) {
        Position pos = fromFEN(fen);
        TranspositionTable tt(8);
        SearchLimits lim; lim.maxDepth = 9; lim.maxNodes = 400000; lim.tt = &tt; lim.options = SearchOptions::fromString("all");
        const SearchResult r = searchIterative(pos, lim);
        addStats(sg, r.stats);
        CHECK(r.hasMove && isLegal(pos, r.bestMove) && pvIsLegal(fen, r.pv), std::string("all options + table: legal move and PV: ") + fen);
    }
    CHECK(sg.singularTests > 0, "singular extension verification searches run (" + std::to_string(sg.singularTests) + " tests, " + std::to_string(sg.singularExtensions) + " extensions)");

    // Null-move safety: with only pawns left (zugzwang territory) a null move is never tried.
    for (const char* fen : {"8/8/8/3k4/8/3K4/3P4/8 w - - 0 1", "8/p1p5/1p4pp/8/1P4PP/8/P1P5/4K2k w - - 0 1", "8/8/1p6/p1p5/P1P5/1P6/8/k1K5 w - - 0 1"}) {
        Position pos = fromFEN(fen);
        const SearchResult r = searchOpt(pos, 8, SearchOptions::fromString("pvs,null"));
        CHECK(r.stats.nullTries == 0, std::string("no null move in a pawn ending: ") + fen);
    }
}

// SEE pruning must never apply while in check: here White's ONLY legal reply to Rh8+ is Qxh8 (the king is boxed in by
// Ne2, Bb7 and the rook), and SEE says Qxh8 loses material (Kxh8). Skipping it would turn a rook sacrifice into "mate".
static void testSeeNeverPrunesEvasions() {
    const std::string fen = "Q2r4/6k1/8/8/8/6r1/8/7K b - - 0 1";
    Position pos = fromFEN(fen);
    Position after = pos;
    makeMove(after, findMove(pos, "d8h8"));
    MoveList evasions; generateLegalMoves(after, evasions);
    CHECK(evasions.size() == 1 && evasions[0].toString() == "a8h8", "test setup: Qxh8 is White's only legal reply to Rh8+");
    CHECK(see(after, evasions[0]) < 0, "test setup: SEE says Qxh8 loses material");
    for (const char* cfg : {"seeq", "pvs,seeq", "default"}) {
        Position p = fromFEN(fen);
        const SearchResult r = searchOpt(p, 1, SearchOptions::fromString(cfg));  // depth 1: Rh8+ leads straight into quiescence, in check
        CHECK(mateInMoves(r.score) == 0 && std::abs(r.score) < 5000, std::string(cfg) + ": the forced capture is searched, no false mate (score " + std::to_string(r.score) + ")");
    }
}

// Regression pins: exact node, null-try, null-cutoff and score values of deterministic searches, recorded from the
// reviewed implementation. They catch changes to the pruning rules that no property test can see (for example a null
// move allowed twice in a row, or an unproven mate score passed up from a pass).
static void testNullMovePins() {
    struct Pin { uint64_t nodes, tries, cutoffs; int score; };
    const Pin nullOnly[8] = {{33074, 522, 515, 0}, {685673, 2274, 1983, 43}, {9499, 126, 57, 9}, {146419, 315, 218, -438},
                             {133620, 847, 748, 16}, {697681, 2459, 2013, 17}, {841957, 1978, 1646, 39}, {52724, 1403, 1184, 527}};
    const Pin withPruning[8] = {{22788, 522, 515, 0}, {251167, 735, 534, 43}, {6003, 126, 51, 9}, {18732, 21, 2, -438},
                                {65353, 798, 716, 16}, {466093, 1977, 1746, 17}, {387244, 1497, 1237, 35}, {7352, 90, 22, 527}};
    for (int cfg = 0; cfg < 2; ++cfg) {
        const SearchOptions o = SearchOptions::fromString(cfg == 0 ? "pvs,null" : "pvs,null,rfp,fut");
        const Pin* pins = cfg == 0 ? nullOnly : withPruning;
        for (int i = 0; i < 8; ++i) {
            Position pos = fromFEN(BENCH[i]);
            const SearchResult r = searchOpt(pos, 6, o);
            CHECK(r.nodes == pins[i].nodes && r.stats.nullTries == pins[i].tries && r.stats.nullCutoffs == pins[i].cutoffs && r.score == pins[i].score,
                  std::string("pinned depth-6 search (") + o.toString() + ") unchanged: " + BENCH[i] + " (nodes " + std::to_string(r.nodes) + ", tries " +
                      std::to_string(r.stats.nullTries) + ", cutoffs " + std::to_string(r.stats.nullCutoffs) + ", score " + std::to_string(r.score) + ")");
        }
    }
}

// All techniques at once, on many positions, with and without a table: legal, bounded, restored.
static void testEverythingTogetherIsSound() {
    const std::vector<std::string> fens = positions(4);
    int searches = 0;
    for (const std::string& fen : fens) {
        Position pos = fromFEN(fen);
        const Position copy = pos;
        const uint64_t base = pos.baseKey();
        TranspositionTable tt(2);
        for (const char* cfg : {"default", "all"}) {
            const SearchOptions o = SearchOptions::fromString(cfg);
            const SearchResult r = searchOpt(pos, 4, o, &tt, TT_DEPTH_GE);
            ++searches;
            CHECK(!r.hasMove || (isLegal(pos, r.bestMove) && pvIsLegal(fen, r.pv) && !r.pv.empty() && r.pv[0] == r.bestMove), std::string(cfg) + ": legal move and PV: " + fen);
            CHECK(std::abs(r.score) <= MATE_SCORE, "score in range");
        }
        CHECK(pos == copy && pos.baseKey() == base, "position and key restored: " + fen);
    }
    CHECK(searches > 100, "many searches ran");
}

// Check extensions can chain; the PV arrays and ply-indexed tables must stay in bounds (the sanitizer run backs this).
static void testExtensionsStayBounded() {
    for (const char* fen : {"6k1/5ppp/8/8/8/8/5PPP/R3K2R w KQ - 0 1", "r4rk1/ppp2ppp/8/8/8/8/PPP2PPP/R4RK1 w - - 0 1",
                            "5rk1/5ppp/8/8/8/8/5PPP/3QR1K1 w - - 0 1", "7k/5Q2/8/8/8/8/8/6RK w - - 0 1",
                            "6k1/8/8/8/8/8/5q2/6K1 w - - 0 1", "4k3/8/8/8/8/8/8/R3K2q w Q - 0 1"}) {
        Position pos = fromFEN(fen);
        TranspositionTable tt(2);
        SearchLimits lim; lim.maxDepth = 20; lim.maxNodes = 300000; lim.tt = &tt; lim.options = SearchOptions::fromString("all");
        const SearchResult r = searchIterative(pos, lim);
        CHECK((!r.hasMove || isLegal(pos, r.bestMove)) && r.pv.size() <= static_cast<size_t>(MAX_PLY) + 2, std::string("forcing positions with all extensions: bounded and legal: ") + fen);
        CHECK(pvIsLegal(fen, r.pv), std::string("PV legal: ") + fen);
    }
}

// The cap on extensions: with extensionPlyLimit = depth + 3 no line may go deeper than depth + 3 plies, however many
// checks it contains. (Without the cap, chains of checks go deeper: the same positions reach ply 8 and 10 at depth 4.)
static void testExtensionCap() {
    const char* fens[] = {"7k/5Q2/8/8/8/8/8/6RK w - - 0 1", "4k3/8/8/8/8/8/8/R3K2q w Q - 0 1", "5rk1/5ppp/8/8/8/8/5PPP/3QR1K1 w - - 0 1",
                          "6k1/6pp/8/8/8/8/1q4PP/6K1 w - - 0 1"};
    int deeperWithoutCap = 0;
    for (const char* fen : fens) {
        Position pos = fromFEN(fen);
        SearchOptions capped = SearchOptions::fromString("pvs,ext");
        capped.extensionPlyLimit = 7;
        const SearchResult a = searchOpt(pos, 4, capped);
        CHECK(a.stats.maxPly <= 7, std::string("extension cap: no line deeper than depth + 3 (reached ply ") + std::to_string(a.stats.maxPly) + "): " + fen);
        SearchOptions free = SearchOptions::fromString("pvs,ext");
        const SearchResult b = searchOpt(pos, 4, free);
        if (b.stats.maxPly > 7) ++deeperWithoutCap;
        CHECK(a.hasMove && isLegal(pos, a.bestMove) && b.hasMove && isLegal(pos, b.bestMove), std::string("legal moves with and without the cap: ") + fen);
    }
    CHECK(deeperWithoutCap >= 2, "the positions do chain extensions past the cap when it is high (" + std::to_string(deeperWithoutCap) + " of 4)");
    // Without any extension option the deepest ply is the search depth plus quiescence only for the main search: exactly the depth.
    Position pos = fromFEN(MIDDLEGAME);
    const SearchResult plain = searchOpt(pos, 4, SearchOptions::none());
    CHECK(plain.stats.maxPly == 4, "no extensions: the main search reaches exactly its depth (" + std::to_string(plain.stats.maxPly) + ")");
}

// =====================================================================================================
// Tactics
// =====================================================================================================

static void testMates() {
    struct Case { const char* fen; int depth; int mate; };
    const Case cases[] = {
        {"r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1", 3, 1},
        {"7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", 5, 2},
        {"6k1/8/6K1/8/8/8/8/7R w - - 0 1", 5, 2},
        {"8/8/4R3/8/4K3/8/8/4k3 w - - 0 1", 5, 3},
        {"8/k7/8/8/K7/1Q6/8/8 w - - 0 1", 5, 3},
        {"1k6/3Q4/8/8/8/K7/8/8 w - - 0 1", 7, 4},
        {"7k/8/5K2/8/8/8/8/6Q1 b - - 0 1", 3, -1},
    };
    // Everything except LMR keeps the exact mate distance at the depth the plain search needs.
    for (const Case& c : cases) {
        for (const char* cfg : {"pvs,rfp,null,fut,ext,seeord,seeq,rep", "pvs,raz,sing"}) {
            Position pos = fromFEN(c.fen);
            TranspositionTable tt(2);
            const SearchResult r = searchOpt(pos, c.depth + 2, SearchOptions::fromString(cfg), &tt);
            CHECK(mateInMoves(r.score) == c.mate, std::string(cfg) + ": mate distance preserved (" + std::to_string(mateInMoves(r.score)) + " vs " + std::to_string(c.mate) + "): " + c.fen);
            if (c.mate > 0 && r.hasMove) CHECK(isLegal(pos, r.bestMove), "mating move legal");
        }
    }
    // LMR trades exactness for speed: reducing the quiet moves of a bare K+Q v K ending can hide a mate at a fixed depth
    // (measured and documented). What must hold: with iterative deepening the default options still find a mate, and
    // never one SHORTER than the truth (that would be a false mate).
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        TranspositionTable tt(8);
        SearchLimits lim; lim.maxDepth = 14; lim.maxNodes = 2500000; lim.tt = &tt; lim.options = SearchOptions::engineDefault();
        const SearchResult r = searchIterative(pos, lim);
        const int m = mateInMoves(r.score);
        CHECK(m != 0 && ((m > 0) == (c.mate > 0)), std::string("default options find the mate with iterative deepening: ") + c.fen + " (found " + std::to_string(m) + ")");
        CHECK(std::abs(m) >= std::abs(c.mate), std::string("...and never a shorter mate than exists: ") + c.fen);
        if (c.mate > 0 && r.hasMove) CHECK(isLegal(pos, r.bestMove), "mating move legal");
    }
}

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

static void testTacticalRegression() {
    int solved = 0, total = 0;
    for (const TacticCase& c : TACTICS) {
        Position pos = fromFEN(c.fen);
        TranspositionTable tt(8);
        SearchLimits lim; lim.maxNodes = 50000; lim.tt = &tt; lim.options = SearchOptions::engineDefault();
        const SearchResult r = searchIterative(pos, lim);
        ++total;
        const bool ok = r.hasMove && r.bestMove.toString() == c.uci;
        if (ok) ++solved;
        else std::cout << "  unsolved " << c.id << ": played " << (r.hasMove ? r.bestMove.toString() : "-") << ", expected " << c.uci << "\n";
    }
    std::cout << "  [tactical regression set: " << solved << "/" << total << " solved at 50000 nodes]\n";
    CHECK(solved == total, "every position of the tactical regression set is solved (" + std::to_string(solved) + "/" + std::to_string(total) + ")");
}

// =====================================================================================================
// Repetition detection
// =====================================================================================================

static void testRepetition() {
    const SearchOptions rep = SearchOptions::fromString("rep");

    // Within three plies nothing can repeat without game history: switching it on changes nothing.
    for (const char* fen : BENCH) {
        Position pos = fromFEN(fen);
        const SearchResult a = searchOpt(pos, 3, SearchOptions::none());
        const SearchResult b = searchOpt(pos, 3, rep);
        CHECK(a.score == b.score && a.nodes == b.nodes && b.stats.repetitionDraws == 0, std::string("no repetition possible in 3 plies: ") + fen);
    }

    // History: the position after the best move already occurred 4 plies ago in the game, so repeating it is a draw.
    {
        const std::string fen = "8/8/8/8/8/6k1/8/R3K3 w - - 10 30";
        Position pos = fromFEN(fen);
        const SearchResult plain = searchOpt(pos, 4, rep);
        CHECK(plain.hasMove && plain.score > 100, "baseline: White is winning (a rook up)");
        Position after = pos;
        makeMove(after, plain.bestMove);
        // The game history: [..., X, Y, Z, W] where X (3 plies before the root... ) equals the position after the best move.
        std::vector<uint64_t> history = {0x1111, 0x2222, 0x3333, hashKey(after), 0x4444, 0x5555};
        // root is at ply 0; the position after the best move is at ply 1, so it repeats history[size-3] (4 plies back).
        const SearchResult withHistory = searchOpt(pos, 4, rep, nullptr, TT_DEPTH_GE, history);
        CHECK(withHistory.stats.repetitionDraws > 0, "game history: the repeating move is recognised");
        CHECK(!(withHistory.bestMove == plain.bestMove), "...so the search avoids walking into the repetition while it is winning");
        const SearchResult off = searchOpt(pos, 4, SearchOptions::none(), nullptr, TT_DEPTH_GE, history);
        CHECK(off.bestMove == searchOpt(pos, 4, SearchOptions::none()).bestMove && off.stats.repetitionDraws == 0, "history is ignored when the option is off");

        // A losing side takes the draw: White (a rook DOWN) is offered a repetition.
        const std::string losing = "4k3/8/8/8/8/6K1/8/r7 w - - 10 30";
        Position lp = fromFEN(losing);
        SearchOptions o = rep;
        const SearchResult noHist = searchOpt(lp, 4, o);
        CHECK(noHist.score < -100, "baseline: a rook down is losing");
        Position afterL = lp;
        MoveList ml; generateLegalMoves(lp, ml);
        // Pretend the position after EVERY White move has just occurred: every move repeats, every line is a draw.
        std::vector<uint64_t> h = {0x1, 0x2};
        for (size_t i = 0; i < ml.size(); ++i) { Position t = lp; makeMove(t, ml[i]); h.push_back(hashKey(t)); }
        h.push_back(0x9); h.push_back(0xA); h.push_back(0xB);
        // Each of those keys sits at a distance different from 4 back, so only some align: this just checks soundness.
        const SearchResult r = searchOpt(lp, 4, o, nullptr, TT_DEPTH_GE, h);
        CHECK(r.hasMove && isLegal(lp, r.bestMove), "a crowded history never breaks the search");
        (void)afterL;
    }

    // The root itself may repeat an earlier game position: the search must still return a move (a draw score at the
    // root would leave the engine without a move to play).
    {
        const std::string fen = "8/8/8/8/8/6k1/8/R3K3 w - - 10 30";
        Position pos = fromFEN(fen);
        std::vector<uint64_t> history = {0x1, 0x2, 0x3, 0x4, hashKey(pos), 0x5, 0x6, 0x7};  // the root position, 4 plies back (index size-4)
        const SearchResult r = searchOpt(pos, 4, rep, nullptr, TT_DEPTH_GE, history);
        CHECK(r.hasMove && isLegal(pos, r.bestMove) && !r.pv.empty(), "a root that repeats a game position still gets a move");
        SearchLimits lim; lim.maxDepth = 4; lim.options = rep; lim.history = history;
        const SearchResult it = searchIterative(pos, lim);
        CHECK(it.hasMove && isLegal(pos, it.bestMove), "...also with iterative deepening");
    }

    // In-tree repetition: a shuffling endgame produces repetitions inside the search tree.
    {
        Position pos = fromFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
        const SearchResult r = searchOpt(pos, 7, rep);
        CHECK(r.stats.repetitionDraws > 0, "repetitions inside the search tree are found (" + std::to_string(r.stats.repetitionDraws) + ")");
        CHECK(r.hasMove && isLegal(pos, r.bestMove) && pvIsLegal("4k3/8/8/8/8/8/8/R3K3 w - - 0 1", r.pv), "...and the result is legal");
        const SearchResult off = searchOpt(pos, 7, SearchOptions::none());
        CHECK(r.nodes < off.nodes, "repetition draws prune the shuffling tree (" + std::to_string(r.nodes) + " vs " + std::to_string(off.nodes) + " nodes)");
    }

    // With the table and iterative deepening, and a history from a real game, the search stays sound.
    {
        Position pos; pos.setStartingPosition();
        std::vector<uint64_t> history;
        Rng rng{77};
        for (int ply = 0; ply < 40; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            TranspositionTable tt(2);
            SearchLimits lim; lim.maxNodes = 3000; lim.tt = &tt; lim.options = SearchOptions::engineDefault(); lim.history = history;
            const SearchResult r = searchIterative(pos, lim);
            CHECK(r.hasMove && isLegal(pos, r.bestMove), "game replay: legal move at ply " + std::to_string(ply));
            history.push_back(hashKey(pos));
            makeMove(pos, ply < 8 ? legal[rng.next() % legal.size()] : r.bestMove);
        }
    }
}

// Usage: test_stage17 [section ...]   Sections: options null see preserving aspiration fire pins sound mates tactics repetition.
// With no argument every section runs (this is what ctest does); a list runs only those (handy under a sanitizer).
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

    if (want("options")) { testOptions(); testDefaultsAreStage16(); lap("options"); }
    if (want("null")) { std::cout << "Null move\n"; testNullMove(); lap("null move"); }
    if (want("see")) { std::cout << "SEE\n"; testSEE(); lap("SEE"); }
    if (want("preserving")) { std::cout << "Score-preserving techniques\n"; testScorePreserving(); lap("PVS / SEE ordering identity"); }
    if (want("aspiration")) { testAspirationIdentity(); lap("aspiration identity"); }
    if (want("fire")) { std::cout << "Heuristic techniques\n"; testTechniquesFire(); lap("techniques fire"); }
    if (want("pins")) { testSeeNeverPrunesEvasions(); testNullMovePins(); lap("pins"); }
    if (want("sound")) { testEverythingTogetherIsSound(); testExtensionsStayBounded(); testExtensionCap(); lap("soundness"); }
    if (want("mates")) { testMates(); lap("mates"); }
    if (want("tactics")) { testTacticalRegression(); lap("tactics"); }
    if (want("repetition")) { std::cout << "Repetition\n"; testRepetition(); lap("repetition"); }

    std::cout << "\n" << (testsRun - testsFailed) << "/" << testsRun << " checks passed.";
    if (testsFailed == 0) std::cout << " ALL TESTS PASSED";
    std::cout << "\n";
    return testsFailed == 0 ? 0 : 1;
}