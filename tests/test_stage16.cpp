// Stage 16 tests: Zobrist hashing, transposition table, search integration.
//
// Part A  hashing:  key tables, determinism, incremental key == full recompute (whole trees + random games),
//                   transpositions, side/castling/en passant components, FEN round trips.
// Part B  table:    packed moves, mate-score conversion, store/probe, bounds, generations, all four replacement
//                   policies.
// Part C  search:   with the table in EXACT_DEPTH mode the score must equal the search without a table (also with a
//                   tiny table under heavy replacement), standard mode gives legal moves/PVs and fewer nodes,
//                   mates keep their distance, a poisoned hash move is harmless, the root is never cut off.
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/position.hpp"
#include "../src/board/zobrist.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/suite.hpp"
#include "../src/rules/repetition.hpp"
#include "../src/search/search.hpp"
#include "../src/search/tt.hpp"
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
static const uint64_t START_HASH = 0xd4ac4930b83b3e3bULL;  // pinned: changing the keys or the key rules breaks this on purpose

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

// Plays the legal move whose UCI string is `uci`. Returns false if there is none.
static bool play(Position& pos, const std::string& uci) {
    MoveList ml; generateLegalMoves(pos, ml);
    for (size_t i = 0; i < ml.size(); ++i)
        if (ml[i].toString() == uci) { makeMove(pos, ml[i]); return true; }
    std::cout << "SETUP FAILED: no legal move " << uci << " in " << toFEN(pos) << "\n";
    ++testsFailed;
    return false;
}

static bool playAll(Position& pos, const std::vector<std::string>& moves) {
    for (const auto& m : moves) if (!play(pos, m)) return false;
    return true;
}

// Suite positions plus positions sampled from random games (every 10th ply starting at ply 5).
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

// =====================================================================================================
// Part A: hashing
// =====================================================================================================

static void testKeyTables() {
    std::vector<uint64_t> all;
    for (int p = 0; p < 12; ++p)
        for (int sq = 0; sq < 64; ++sq) all.push_back(zobrist::KEYS.piece[p][sq]);
    all.push_back(zobrist::KEYS.side);
    for (int i = 1; i < 16; ++i) all.push_back(zobrist::KEYS.castling[i]);
    for (int f = 0; f < 8; ++f) all.push_back(zobrist::KEYS.epFile[f]);
    CHECK(all.size() == 768 + 1 + 15 + 8, "792 independent keys");
    CHECK(std::count(all.begin(), all.end(), 0ULL) == 0, "no key is zero");
    std::sort(all.begin(), all.end());
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end(), "all keys are distinct");
    CHECK(zobrist::KEYS.castling[0] == 0, "empty castling mask has key 0 (so the empty board hashes to 0)");

    // The keys should look random: about 32 set bits on average.
    long bits = 0;
    for (uint64_t k : all) bits += __builtin_popcountll(k);
    const double avg = static_cast<double>(bits) / static_cast<double>(all.size());
    CHECK(avg > 30.0 && avg < 34.0, "keys are balanced (average popcount " + std::to_string(avg) + ")");
}

static void testDeterminism() {
    Position a; a.setStartingPosition();
    Position b = fromFEN(START_FEN);
    CHECK(hashKey(a) == START_HASH, "start position hash is the pinned constant");
    CHECK(hashKey(b) == START_HASH, "FEN start position gives the same hash");
    CHECK(a.baseKey() == recomputeBaseKey(a), "start: incremental == recomputed");
    Position empty;
    CHECK(empty.baseKey() == 0 && hashKey(empty) == 0, "empty board hashes to 0");
    Position c = a;
    CHECK(c.baseKey() == a.baseKey(), "copy keeps the key");
    Position d; d.setStartingPosition(); d.setStartingPosition();  // resetting must not accumulate
    CHECK(hashKey(d) == START_HASH, "setStartingPosition twice: same hash");
    d.clear();
    CHECK(d.baseKey() == 0, "clear() resets the key");
    CHECK(a.isConsistent(), "isConsistent includes the key check");

    // Pieces placed in a different ORDER give the same hash.
    Position x, y;
    x.putPiece(WHITE_KING, E1); x.putPiece(BLACK_KING, E8); x.putPiece(WHITE_ROOK, A1);
    y.putPiece(WHITE_ROOK, A1); y.putPiece(BLACK_KING, E8); y.putPiece(WHITE_KING, E1);
    CHECK(hashKey(x) == hashKey(y), "placement order does not matter");
    // putPiece on an occupied square replaces: the old piece's key must go.
    Position z = x;
    z.putPiece(WHITE_QUEEN, A1);
    z.putPiece(WHITE_ROOK, A1);
    CHECK(hashKey(z) == hashKey(x) && z.isConsistent(), "replace-then-restore returns to the same hash");
    z.putPiece(NO_PIECE, A1);
    z.removePiece(A1);
    CHECK(z.isConsistent(), "putPiece(NO_PIECE) / removing an empty square keeps the key consistent");
}

// Walks the full tree; after every make and every unmake the incremental key must equal the recompute.
static uint64_t walkNodes = 0;
static int walkBad = 0;
static void walk(Position& pos, int depth) {
    if (depth == 0) return;
    const uint64_t baseBefore = pos.baseKey();
    const uint64_t fullBefore = hashKey(pos);
    MoveList ml; generateLegalMoves(pos, ml);
    for (size_t i = 0; i < ml.size(); ++i) {
        const Move m = ml[i];
        const UndoInfo u = makeMove(pos, m);
        ++walkNodes;
        if (pos.baseKey() != recomputeBaseKey(pos) || !pos.isConsistent()) ++walkBad;
        walk(pos, depth - 1);
        unmakeMove(pos, m, u);
        if (pos.baseKey() != baseBefore || hashKey(pos) != fullBefore) ++walkBad;
    }
}

static void testIncrementalOnPerftSuite() {
    int positions = 0;
    for (const auto& c : standardPerftSuite()) {
        Position pos = fromFEN(c.fen);
        const int depth = (c.nodes.size() >= 3 && c.nodes[2] <= 120000) ? 3 : 2;
        walk(pos, depth);
        ++positions;
    }
    CHECK(walkBad == 0, "incremental key == recompute at every node of the suite trees (bad: " + std::to_string(walkBad) + ")");
    CHECK(walkNodes > 300000, "the walk visited many nodes (" + std::to_string(walkNodes) + ")");
    CHECK(positions >= 37, "all suite positions walked");
}

static void testRandomPlayouts() {
    Rng rng{0xC0FFEE1234ULL};
    long plies = 0, bad = 0, fenBad = 0;
    long ep = 0, castles = 0, promos = 0, captures = 0, epCapturable = 0;
    int games = 1500;
    for (int g = 0; g < games; ++g) {
        Position pos; pos.setStartingPosition();
        std::vector<Move> moves; std::vector<UndoInfo> undos;
        for (int ply = 0; ply < 160; ++ply) {
            MoveList legal; generateLegalMoves(pos, legal);
            if (legal.size() == 0) break;
            const Move m = legal[rng.next() % legal.size()];
            if (m.flag == EN_PASSANT) ++ep;
            if (m.isCastle()) ++castles;
            if (m.isPromotion()) ++promos;
            if (m.isCapture()) ++captures;
            undos.push_back(makeMove(pos, m));
            moves.push_back(m);
            ++plies;
            if (pos.baseKey() != recomputeBaseKey(pos)) ++bad;
            if (pos.enPassantSquare() != NO_SQUARE && hashKey(pos) != pos.baseKey()) ++epCapturable;
            if (ply % 7 == 0) {  // a position rebuilt from its FEN has the same hash
                Position again = fromFEN(toFEN(pos));
                if (hashKey(again) != hashKey(pos)) ++fenBad;
            }
        }
        // Take the whole game back: the key must return to the start value exactly.
        for (size_t i = moves.size(); i-- > 0;) unmakeMove(pos, moves[i], undos[i]);
        if (hashKey(pos) != START_HASH || !pos.isConsistent()) ++bad;
    }
    CHECK(plies > 100000, "random playouts: many plies (" + std::to_string(plies) + ")");
    CHECK(bad == 0, "incremental == recompute after every ply and after unwinding every game (bad: " + std::to_string(bad) + ")");
    CHECK(fenBad == 0, "FEN round trip preserves the hash (bad: " + std::to_string(fenBad) + ")");
    // The playouts must actually have exercised the special moves, otherwise the test proves little.
    CHECK(ep > 20, "playouts contained en passant captures (" + std::to_string(ep) + ")");
    CHECK(castles > 100, "playouts contained castling (" + std::to_string(castles) + ")");
    CHECK(promos > 100, "playouts contained promotions (" + std::to_string(promos) + ")");
    CHECK(captures > 10000, "playouts contained captures");
    CHECK(epCapturable > 20, "playouts reached positions where the ep key applies (" + std::to_string(epCapturable) + ")");
}

static void testTranspositions() {
    // Same position by different move orders.
    Position a = fromFEN(START_FEN), b = fromFEN(START_FEN);
    playAll(a, {"g1f3", "g8f6", "b1c3", "b8c6"});
    playAll(b, {"b1c3", "b8c6", "g1f3", "g8f6"});
    CHECK(hashKey(a) == hashKey(b), "1.Nf3 Nf6 2.Nc3 Nc6 == 1.Nc3 Nc6 2.Nf3 Nf6");
    CHECK(a == b, "...and the Positions are fully equal here (same clocks)");

    // Returning to the start: same hash, but the clocks differ so the Positions are not equal.
    Position c = fromFEN(START_FEN);
    playAll(c, {"g1f3", "g8f6", "f3g1", "f6g8"});
    CHECK(hashKey(c) == START_HASH, "Nf3 Nf6 Ng1 Ng8 returns to the start hash");
    CHECK(!(c == fromFEN(START_FEN)), "...although the clocks differ (the hash ignores them)");
    CHECK(c.halfmoveClock() == 4, "halfmove clock is not part of the key");

    // But the start position after the same moves with different castling rights is a different position.
    Position d = fromFEN(START_FEN);
    playAll(d, {"g1f3", "g8f6", "h1g1", "h8g8", "g1h1", "g8h8"});
    CHECK(d.castlingRights() == (WHITE_QUEENSIDE | BLACK_QUEENSIDE), "rook moves cost kingside rights");
    CHECK(hashKey(d) != START_HASH, "lost castling rights change the hash");

    // Side to move.
    Position w = fromFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    Position bl = fromFEN("4k3/8/8/8/8/8/4P3/4K3 b - - 0 1");
    CHECK(hashKey(w) != hashKey(bl), "side to move changes the hash");
    CHECK((hashKey(w) ^ hashKey(bl)) == zobrist::KEYS.side, "...by exactly the side key");

    // Castling rights: every one of the 16 masks gives a distinct hash for the same placement.
    std::vector<uint64_t> hs;
    for (int mask = 0; mask < 16; ++mask) {
        Position p = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1");
        p.setCastlingRights(static_cast<CastlingRights>(mask));
        hs.push_back(hashKey(p));
        CHECK(p.isConsistent(), "mask " + std::to_string(mask) + " consistent");
    }
    std::sort(hs.begin(), hs.end());
    CHECK(std::adjacent_find(hs.begin(), hs.end()) == hs.end(), "16 castling masks -> 16 distinct hashes");
    {
        Position p1 = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        Position p0 = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1");
        CHECK((hashKey(p1) ^ hashKey(p0)) == zobrist::KEYS.castling[15], "rights KQkq vs none differ by the castling key");
    }

    // En passant: counts only when a capture is possible.
    Position e1 = fromFEN("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1");
    playAll(e1, {"e2e4"});
    CHECK(e1.enPassantSquare() == E3, "double push sets the ep square");
    Position e1plain = fromFEN("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1");
    CHECK(hashKey(e1) != hashKey(e1plain), "capturable ep square changes the hash");
    CHECK((hashKey(e1) ^ hashKey(e1plain)) == zobrist::KEYS.epFile[4], "...by exactly the e-file key");
    CHECK(e1.baseKey() == e1plain.baseKey(), "the incremental part ignores en passant");

    Position e2 = fromFEN("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    playAll(e2, {"e2e4"});
    Position e2plain = fromFEN("4k3/8/8/8/4P3/8/8/4K3 b - - 0 1");
    CHECK(e2.enPassantSquare() == E3, "ep square is stored after the push");
    CHECK(hashKey(e2) == hashKey(e2plain), "uncapturable ep square does NOT change the hash (transpositions are found)");

    // Black side, other file.
    Position e3 = fromFEN("4k3/3p4/8/4P3/8/8/8/4K3 b - - 0 1");
    playAll(e3, {"d7d5"});
    Position e3plain = fromFEN("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1");
    CHECK(e3.enPassantSquare() == D6, "black double push ep square");
    CHECK((hashKey(e3) ^ hashKey(e3plain)) == zobrist::KEYS.epFile[3], "black double push: d-file key applies");

    // Documented limitation: the ONLY ep capture is illegal (the capturing pawn is pinned along the rank), so the
    // Stage 8 'effective' ep square is none, but the cheap pseudo-legal test still sees a capturer.
    Position pin = fromFEN("8/8/8/8/k2pP2Q/8/8/4K3 b - e3 0 1");
    Position pinPlain = fromFEN("8/8/8/8/k2pP2Q/8/8/4K3 b - - 0 1");
    CHECK(effectiveEnPassantSquare(pin) == NO_SQUARE, "pinned ep capture is not legal (Stage 8 rule)");
    CHECK(hashKey(pin) != hashKey(pinPlain), "known limitation: such positions may hash differently (only a missed match, never a wrong one)");

    // Capturing removes the right key: after exd6 style captures the incremental key still agrees.
    Position cap = fromFEN("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1");
    playAll(cap, {"e2e4", "d4e3"});
    CHECK(cap.baseKey() == recomputeBaseKey(cap) && cap.isConsistent(), "after an en passant capture the key is consistent");
    CHECK(hashKey(cap) == hashKey(fromFEN("4k3/8/8/8/8/4p3/8/4K3 w - - 0 2")), "ep capture result equals the FEN position");

    // Promotion, underpromotion and promotion capture.
    for (const char* mv : {"a7a8q", "a7a8n", "a7b8q", "a7b8r"}) {
        Position pr = fromFEN("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        const uint64_t before = hashKey(pr);
        const Position copy = pr;
        MoveList ml; generateLegalMoves(pr, ml);
        for (size_t i = 0; i < ml.size(); ++i) {
            if (ml[i].toString() != mv) continue;
            const UndoInfo u = makeMove(pr, ml[i]);
            CHECK(pr.baseKey() == recomputeBaseKey(pr) && pr.isConsistent(), std::string("promotion ") + mv + " consistent");
            CHECK(hashKey(pr) != before, std::string("promotion ") + mv + " changes the hash");
            unmakeMove(pr, ml[i], u);
            CHECK(hashKey(pr) == before && pr == copy, std::string("promotion ") + mv + " unmake restores the hash");
        }
    }

    // Castling moves both king and rook.
    {
        Position cs = fromFEN("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        playAll(cs, {"e1g1", "e8c8"});
        CHECK(cs.baseKey() == recomputeBaseKey(cs) && cs.isConsistent(), "both castling moves keep the key consistent");
        CHECK(hashKey(cs) == hashKey(fromFEN("2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2")), "castled position equals its FEN position");
    }
}

// =====================================================================================================
// Part B: transposition table
// =====================================================================================================

static void testPackedMoves() {
    long moves = 0, bad = 0;
    for (const std::string& fen : positions(5)) {
        Position pos = fromFEN(fen);
        MoveList ml; generateLegalMoves(pos, ml);
        for (size_t i = 0; i < ml.size(); ++i) {
            const uint16_t pk = packMove(ml[i]);
            ++moves;
            if (pk == 0) { ++bad; continue; }
            int matches = 0;
            for (size_t j = 0; j < ml.size(); ++j) if (packedMoveMatches(pk, ml[j])) ++matches;
            if (matches != 1) ++bad;  // the packed form must identify exactly one legal move, underpromotions included
        }
    }
    CHECK(moves > 2000 && bad == 0, "packed moves are non-zero and unique within each position (" + std::to_string(moves) + " moves, bad " + std::to_string(bad) + ")");
    CHECK(!packedMoveMatches(0, Move{}), "0 means no move and matches nothing");
    Move m; m.from = E2; m.to = E4;
    Move n = m; n.promotion = QUEEN;
    CHECK(packMove(m) != packMove(n), "promotion piece is part of the packed move");
}

static void testMateScoreConversion() {
    // A mate delivered at ply 7 (score MATE-7), found by a node at ply 4: 3 plies from that node.
    const int s = MATE_SCORE - 7;
    const int stored = scoreToTT(s, 4);
    CHECK(stored == MATE_SCORE - 3, "mate score is stored relative to the node");
    CHECK(scoreFromTT(stored, 4) == s, "round trip at the same ply");
    CHECK(scoreFromTT(stored, 2) == MATE_SCORE - 5, "probed from ply 2 the same mate is 5 plies from the root");
    CHECK(scoreFromTT(stored, 9) == MATE_SCORE - 12, "probed from ply 9 it is 12 plies from the root");
    const int neg = -(MATE_SCORE - 6);
    CHECK(scoreFromTT(scoreToTT(neg, 3), 3) == neg, "negative mate round trip");
    CHECK(scoreToTT(neg, 3) == -(MATE_SCORE - 3), "being mated: stored relative to the node");
    CHECK(scoreFromTT(scoreToTT(neg, 3), 1) == -(MATE_SCORE - 4), "...and rebased at another ply");
    for (int sc : {0, 1, -1, 150, -2500, 29000, -29000, MATE_BOUND - 1, -(MATE_BOUND - 1)})
        CHECK(scoreToTT(sc, 17) == sc && scoreFromTT(sc, 17) == sc, "non-mate score unchanged: " + std::to_string(sc));
    bool fits = true;
    for (int ply = 0; ply <= MAX_PLY; ++ply) {
        const int hi = scoreToTT(MATE_SCORE - ply, ply), lo = scoreToTT(-(MATE_SCORE - ply), ply);
        if (hi > 32767 || lo < -32768) fits = false;
    }
    CHECK(fits, "stored mate scores fit in int16");
}

static const TTReplace POLICIES[] = {TTReplace::ALWAYS, TTReplace::DEPTH_AGE, TTReplace::TWO_TIER, TTReplace::BUCKET4};
static const char* policyName(TTReplace p) {
    switch (p) {
        case TTReplace::ALWAYS: return "ALWAYS";
        case TTReplace::DEPTH_AGE: return "DEPTH_AGE";
        case TTReplace::TWO_TIER: return "TWO_TIER";
        case TTReplace::BUCKET4: return "BUCKET4";
    }
    return "?";
}

// Keys that all land in the same cluster of a table: they differ only in high bits (the index uses the low bits).
static uint64_t collide(int i) { return 0x1234ULL | (static_cast<uint64_t>(i + 1) << 40); }

static void testTableBasics() {
    for (TTReplace pol : POLICIES) {
        const std::string name = policyName(pol);
        TranspositionTable tt(1, pol);
        CHECK(tt.entryCount() == 65536, name + ": 1 MB = 65536 entries");
        CHECK(tt.clusterSize() == (pol == TTReplace::BUCKET4 ? 4u : pol == TTReplace::TWO_TIER ? 2u : 1u), name + ": cluster size");

        TTEntry e;
        CHECK(!tt.probe(0xABCDEF, e), name + ": empty table misses");

        const uint16_t mv = 0x1ABC;
        tt.store(0xABCDEF, 7, 123, BOUND_EXACT, mv, 3);
        CHECK(tt.probe(0xABCDEF, e), name + ": stored entry is found");
        CHECK(e.key == 0xABCDEF && e.depth == 7 && e.score == 123 && e.move == mv && e.bound() == BOUND_EXACT, name + ": all fields round trip");
        CHECK(!tt.probe(0xABCDEE, e), name + ": a different key misses");

        tt.store(0x5555, 4, -50, BOUND_LOWER, 0, 0);
        CHECK(tt.probe(0x5555, e) && e.bound() == BOUND_LOWER && e.score == -50 && e.move == 0, name + ": lower bound");
        tt.store(0x6666, 4, 60, BOUND_UPPER, 0, 0);
        CHECK(tt.probe(0x6666, e) && e.bound() == BOUND_UPPER && e.score == 60, name + ": upper bound");

        // Mate scores are stored node-relative and come back rebased on the probing ply.
        tt.store(0x7777, 5, MATE_SCORE - 7, BOUND_EXACT, 0, 4);
        CHECK(tt.probe(0x7777, e) && e.score == MATE_SCORE - 3, name + ": mate score stored relative to its node");
        CHECK(scoreFromTT(e.score, 2) == MATE_SCORE - 5, name + ": ...and rebased when probed");

        CHECK(tt.stores() == 4 && tt.probes() == 6 && tt.hits() == 4, name + ": statistics (4 stores, 6 probes, 4 hits)");
        CHECK(tt.hashfull() >= 0 && tt.hashfull() <= 1000, name + ": hashfull range");

        // Same key stored again: updated; a move survives a store without one.
        tt.store(0xABCDEF, 9, 200, BOUND_EXACT, 0, 0);
        CHECK(tt.probe(0xABCDEF, e) && e.depth == 9 && e.score == 200 && e.move == mv, name + ": update keeps the old move when the new store has none");

        tt.clear();
        CHECK(!tt.probe(0xABCDEF, e) && tt.stores() == 0 && tt.hashfull() == 0, name + ": clear() empties the table and statistics");
    }
    TranspositionTable big(2);
    CHECK(big.entryCount() == 131072, "2 MB = 131072 entries");
    big.resize(3);
    CHECK(big.entryCount() == 131072, "3 MB rounds down to a power of two");
    big.resize(1);
    CHECK(big.entryCount() == 65536, "resize down");
    big.resizeEntries(1000);
    CHECK(big.entryCount() == 512, "resizeEntries rounds down to a power of two");
    big.resizeEntries(1);
    CHECK(big.entryCount() == big.clusterSize(), "never smaller than one cluster");
    big.setPolicy(TTReplace::ALWAYS);
    CHECK(big.clusterSize() == 1 && big.policy() == TTReplace::ALWAYS, "setPolicy");
}

static bool has(const TranspositionTable& tt, uint64_t key) { TTEntry e; return tt.probe(key, e); }

static void testReplacementPolicies() {
    {   // ALWAYS: the newcomer wins, regardless of depth.
        TranspositionTable tt(1, TTReplace::ALWAYS);
        tt.store(collide(0), 10, 1, BOUND_EXACT, 0, 0);
        tt.store(collide(1), 1, 2, BOUND_EXACT, 0, 0);
        CHECK(!has(tt, collide(0)) && has(tt, collide(1)), "ALWAYS: shallow newcomer evicts the deep entry");
    }
    {   // DEPTH_AGE: a deeper entry of the current search is protected; an older one is not.
        TranspositionTable tt(1, TTReplace::DEPTH_AGE);
        tt.store(collide(0), 10, 1, BOUND_EXACT, 0, 0);
        tt.store(collide(1), 1, 2, BOUND_EXACT, 0, 0);
        CHECK(has(tt, collide(0)) && !has(tt, collide(1)), "DEPTH_AGE: shallow newcomer is dropped");
        tt.store(collide(2), 10, 3, BOUND_EXACT, 0, 0);
        CHECK(!has(tt, collide(0)) && has(tt, collide(2)), "DEPTH_AGE: an equally deep newcomer replaces");
        tt.newSearch();
        tt.store(collide(3), 1, 4, BOUND_EXACT, 0, 0);
        CHECK(!has(tt, collide(2)) && has(tt, collide(3)), "DEPTH_AGE: an entry from an older search is replaced by anything");
    }
    {   // TWO_TIER: slot 0 keeps the deep entry, slot 1 always takes the newest.
        TranspositionTable tt(1, TTReplace::TWO_TIER);
        tt.store(collide(0), 10, 1, BOUND_EXACT, 0, 0);
        tt.store(collide(1), 1, 2, BOUND_EXACT, 0, 0);
        CHECK(has(tt, collide(0)) && has(tt, collide(1)), "TWO_TIER: both coexist");
        tt.store(collide(2), 2, 3, BOUND_EXACT, 0, 0);
        CHECK(has(tt, collide(0)) && !has(tt, collide(1)) && has(tt, collide(2)), "TWO_TIER: the always-replace slot is recycled, the deep one stays");
        tt.store(collide(3), 12, 4, BOUND_EXACT, 0, 0);
        CHECK(!has(tt, collide(0)) && has(tt, collide(3)) && has(tt, collide(2)), "TWO_TIER: a deeper newcomer takes the depth slot");
    }
    {   // BUCKET4: the least valuable (shallowest / oldest) of four is evicted.
        TranspositionTable tt(1, TTReplace::BUCKET4);
        const int depths[4] = {5, 6, 7, 8};
        for (int i = 0; i < 4; ++i) tt.store(collide(i), depths[i], i, BOUND_EXACT, 0, 0);
        tt.store(collide(4), 3, 9, BOUND_EXACT, 0, 0);
        CHECK(!has(tt, collide(0)) && has(tt, collide(1)) && has(tt, collide(2)) && has(tt, collide(3)) && has(tt, collide(4)),
              "BUCKET4: the shallowest of the four is evicted");
        tt.newSearch();
        tt.store(collide(5), 1, 10, BOUND_EXACT, 0, 0);
        CHECK(!has(tt, collide(4)) && has(tt, collide(1)) && has(tt, collide(5)), "BUCKET4: after ageing, the lowest depth-minus-age is evicted");
        // An old deep entry loses to a fresh shallow one once it is old enough (8 per generation).
        TranspositionTable t2(1, TTReplace::BUCKET4);
        for (int i = 0; i < 4; ++i) t2.store(collide(i), 10, i, BOUND_EXACT, 0, 0);
        for (int g = 0; g < 3; ++g) t2.newSearch();  // value of the old entries: 10 - 24 = -14
        t2.store(collide(4), 4, 9, BOUND_EXACT, 0, 0);
        int survivors = 0;
        for (int i = 0; i < 4; ++i) survivors += has(t2, collide(i)) ? 1 : 0;
        CHECK(survivors == 3 && has(t2, collide(4)), "BUCKET4: stale deep entries are not immortal");
    }
    for (TTReplace pol : POLICIES) {  // same-key update rules
        TranspositionTable tt(1, pol);
        TTEntry e;
        tt.store(collide(0), 8, 100, BOUND_EXACT, 0x111, 0);
        tt.store(collide(0), 2, -7, BOUND_UPPER, 0, 0);  // much shallower, not exact
        CHECK(tt.probe(collide(0), e), std::string(policyName(pol)) + ": same key still present");
        if (pol == TTReplace::ALWAYS) CHECK(e.depth == 2 && e.score == -7 && e.move == 0x111, "ALWAYS: overwritten, move kept");
        else CHECK(e.depth == 8 && e.score == 100, std::string(policyName(pol)) + ": a much shallower non-exact result does not overwrite");
        tt.store(collide(0), 7, 55, BOUND_LOWER, 0x222, 0);
        CHECK(tt.probe(collide(0), e) && e.depth == 7 && e.score == 55 && e.move == 0x222, std::string(policyName(pol)) + ": a nearly-as-deep result overwrites");
        tt.store(collide(0), 1, 0, BOUND_EXACT, 0, 0);
        CHECK(tt.probe(collide(0), e) && e.depth == 1 && e.bound() == BOUND_EXACT && e.move == 0x222, std::string(policyName(pol)) + ": an exact result always overwrites and keeps the move");
    }
}

// =====================================================================================================
// Part C: search integration
// =====================================================================================================

static void testStrictModeKeepsScores() {
    const std::vector<std::string> fens = positions(4);
    uint64_t plainNodes = 0, ttNodes = 0;
    int compared = 0, mismatches = 0;
    for (const std::string& fen : fens) {
        Position pos = fromFEN(fen);
        const Position copy = pos;
        TranspositionTable tt(4);
        uint64_t lastNodes = 0;
        for (int depth = 1; depth <= 4; ++depth) {
            if (depth == 4 && lastNodes > 20000) break;  // keep the suite quick: depth 4 only where depth 3 was cheap
            const SearchResult ref = search(pos, depth);
            lastNodes = ref.nodes;
            const SearchResult r = searchTT(pos, depth, tt, TT_EXACT_DEPTH);  // the table is reused across depths
            ++compared;
            if (r.score != ref.score) {
                ++mismatches;
                std::cout << "  mismatch: " << fen << " depth " << depth << " plain " << ref.score << " tt " << r.score << "\n";
            }
            CHECK(r.hasMove == ref.hasMove, "hasMove agrees: " + fen);
            if (r.hasMove) CHECK(isLegal(pos, r.bestMove), "best move legal: " + fen);
            if (depth == 4) { plainNodes += ref.nodes; ttNodes += r.nodes; }
        }
        CHECK(pos == copy, "position restored after searchTT: " + fen);
    }
    CHECK(mismatches == 0, "EXACT_DEPTH table never changes the score (" + std::to_string(compared) + " searches, " + std::to_string(mismatches) + " mismatches)");
    std::cout << "  [strict mode depth 4: plain " << plainNodes << " nodes, with table " << ttNodes << " nodes ("
              << (100.0 * static_cast<double>(ttNodes) / static_cast<double>(plainNodes)) << "%)]\n";
    CHECK(ttNodes < plainNodes, "the table reduces the node count even in the score-preserving mode");
}

static void testOrderOnlyMode() {
    // The table used purely for the hash move (no cutoffs) is a pure reordering: scores must be identical.
    int compared = 0, mismatches = 0;
    for (const std::string& fen : positions(2)) {
        Position pos = fromFEN(fen);
        TranspositionTable tt(4);
        uint64_t lastNodes = 0;
        for (int depth = 1; depth <= 4; ++depth) {
            if (depth == 4 && lastNodes > 20000) break;
            const SearchResult ref = search(pos, depth);
            lastNodes = ref.nodes;
            const SearchResult r = searchTT(pos, depth, tt, TT_ORDER_ONLY);
            ++compared;
            if (r.score != ref.score) { ++mismatches; std::cout << "  mismatch: " << fen << " depth " << depth << "\n"; }
            if (r.hasMove) CHECK(isLegal(pos, r.bestMove), "order-only best move legal: " + fen);
        }
    }
    CHECK(mismatches == 0, "ORDER_ONLY table never changes the score (" + std::to_string(compared) + " searches)");
}

// Every entry the search leaves in the table must be TRUE: compare it with an independent plain search of that
// position to the entry's own depth. EXACT -> equal, LOWER -> value >= stored, UPPER -> value <= stored. Strict mode
// is used because there the stored values are exactly the table-less values. Mate scores are compared node-relative.
static void testStoredEntriesAreTrue() {
    const std::vector<std::string> fens = {MIDDLEGAME, KIWIPETE, START_FEN, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                                           "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", "8/8/4R3/8/4K3/8/8/4k3 w - - 0 1",
                                           "8/k7/8/8/K7/1Q6/8/8 w - - 0 1"};
    int checked = 0, wrong = 0, exact = 0, lower = 0, upper = 0, mate = 0;
    for (const std::string& fen : fens) {
        Position pos = fromFEN(fen);
        const int depth = (fen == fens[5] || fen == fens[6]) ? 5 : 4;
        TranspositionTable tt(4);
        searchTT(pos, depth, tt, TT_EXACT_DEPTH);
        Rng rng{0x1234567ULL};
        std::vector<Position> nodes;  // children, plus a deterministic sample of grandchildren
        MoveList l1; generateLegalMoves(pos, l1);
        for (size_t i = 0; i < l1.size(); ++i) {
            Position c = pos; makeMove(c, l1[i]);
            nodes.push_back(c);
            MoveList l2; generateLegalMoves(c, l2);
            for (size_t j = 0; j < l2.size(); ++j)
                if (rng.next() % 5 == 0) { Position g = c; makeMove(g, l2[j]); nodes.push_back(g); }
        }
        for (const Position& n : nodes) {
            TTEntry e;
            if (!tt.probe(hashKey(n), e) || e.depth < 1) continue;
            Position copy = n;
            const int truth = search(copy, e.depth).score;
            const int stored = scoreFromTT(e.score, 0);  // node-relative: as if the node were the root
            bool ok = true;
            switch (e.bound()) {
                case BOUND_EXACT: ok = truth == stored; ++exact; break;
                case BOUND_LOWER: ok = truth >= stored; ++lower; break;
                case BOUND_UPPER: ok = truth <= stored; ++upper; break;
                case BOUND_NONE: ok = false; break;
            }
            if (isMateScore(stored)) ++mate;
            ++checked;
            if (!ok) {
                ++wrong;
                if (wrong <= 5) std::cout << "  wrong entry: " << toFEN(n) << " depth " << int(e.depth) << " bound " << int(e.bound()) << " stored " << stored << " truth " << truth << "\n";
            }
        }
    }
    CHECK(wrong == 0, "every stored entry agrees with an independent search (" + std::to_string(checked) + " entries, " + std::to_string(wrong) + " wrong)");
    CHECK(checked > 200 && exact > 20 && lower > 20 && upper > 20, "all three bound types were exercised (exact " + std::to_string(exact) + ", lower " +
                                                                        std::to_string(lower) + ", upper " + std::to_string(upper) + ")");
    CHECK(mate > 5, "mate-score entries were among them (" + std::to_string(mate) + ")");
}

// A bogus entry that is much DEEPER than the search: the standard mode must trust it (that is what depth >= means),
// the exact-depth and order-only modes must ignore it. This pins down the three modes against each other.
static void testModesTreatDeepEntriesDifferently() {
    for (const char* fen : {MIDDLEGAME, KIWIPETE}) {
        Position pos = fromFEN(fen);
        const SearchResult plain = search(pos, 3);
        MoveList ml; generateLegalMoves(pos, ml);
        Position child = pos; makeMove(child, ml[ml.size() - 1]);
        // "After the last move the side to move is lost by 5000": the root would love to play it.
        auto poisoned = [&](TTMode mode) {
            TranspositionTable tt(2);
            tt.store(hashKey(child), 9, -5000, BOUND_EXACT, 0, 1);
            return searchTT(pos, 3, tt, mode);
        };
        const SearchResult standard = poisoned(TT_DEPTH_GE);
        const SearchResult strict = poisoned(TT_EXACT_DEPTH);
        const SearchResult orderOnly = poisoned(TT_ORDER_ONLY);
        CHECK(standard.score == 5000, std::string("standard mode trusts a deeper entry (test has power): ") + fen);
        CHECK(strict.score == plain.score, std::string("exact-depth mode ignores an entry of a different depth: ") + fen);
        CHECK(orderOnly.score == plain.score, std::string("order-only mode never cuts off: ") + fen);
        // And an entry that is too shallow is ignored by every mode.
        TranspositionTable tt(2);
        tt.store(hashKey(child), 1, -5000, BOUND_EXACT, 0, 1);
        CHECK(searchTT(pos, 3, tt, TT_DEPTH_GE).score == plain.score, std::string("a too-shallow entry is ignored: ") + fen);
    }
}

static void testStrictModeUnderHeavyReplacement() {
    // Tiny tables (256 entries) against trees that store several times as many nodes: replacement happens
    // constantly, for every policy. The score must still be exactly the table-less score.
    struct Case { std::string fen; int depth; };
    const std::vector<Case> cases = {{MIDDLEGAME, 4},
                                     {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", 4},
                                     {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5},
                                     {KIWIPETE, 4}};
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        const SearchResult ref = search(pos, c.depth);
        for (TTReplace pol : POLICIES) {
            for (size_t entries : {size_t{256}, size_t{16384}}) {
                if (entries == 16384 && pol != TTReplace::BUCKET4 && pol != TTReplace::ALWAYS) continue;
                TranspositionTable tt(1, pol);
                tt.resizeEntries(entries);
                const SearchResult r = searchTT(pos, c.depth, tt, TT_EXACT_DEPTH);
                CHECK(r.score == ref.score, std::string("score unchanged with a ") + std::to_string(entries) + "-entry " + policyName(pol) +
                                                " table, depth " + std::to_string(c.depth) + ": " + c.fen);
                if (entries == 256)
                    CHECK(tt.stores() > 2 * tt.entryCount(), std::string(policyName(pol)) + ": the table was really under pressure (stores " +
                                                                 std::to_string(tt.stores()) + ", entries " + std::to_string(tt.entryCount()) + ")");
            }
        }
    }
}

static void testStandardMode() {
    const std::vector<std::string> fens = positions(4);
    uint64_t plainNodes = 0, ttNodes = 0;
    for (const std::string& fen : fens) {
        Position pos = fromFEN(fen);
        const Position copy = pos;
        TranspositionTable tt(8);
        const SearchResult ref = search(pos, 4);
        const SearchResult r = searchTT(pos, 4, tt, TT_DEPTH_GE);
        plainNodes += ref.nodes; ttNodes += r.nodes;
        CHECK(r.hasMove == ref.hasMove, "hasMove agrees (standard mode): " + fen);
        if (r.hasMove) {
            CHECK(isLegal(pos, r.bestMove), "standard mode best move legal: " + fen);
            CHECK(!r.pv.empty() && r.pv[0] == r.bestMove, "PV starts with the best move: " + fen);
            CHECK(pvIsLegal(fen, r.pv), "PV (extended from the table) is legal: " + fen);
            CHECK(r.pv.size() >= ref.pv.size() || r.pv.size() >= 1, "PV not empty");
            CHECK(std::abs(r.score) <= MATE_SCORE, "score in range");
        }
        CHECK(pos == copy, "position restored (standard mode): " + fen);
    }
    std::cout << "  [standard mode depth 4: plain " << plainNodes << " nodes, with table " << ttNodes << " nodes ("
              << (100.0 * static_cast<double>(ttNodes) / static_cast<double>(plainNodes)) << "%)]\n";
    CHECK(ttNodes < plainNodes, "standard mode searches fewer nodes in total");
}

static void testMatesKeepTheirDistance() {
    // Mate scores pass through the table at different plies. If the ply conversion were wrong, mates would be
    // reported too short or too long. Reference: the table-less search at the same depth.
    struct Case { const char* fen; int depth; int expectMate; };
    const Case cases[] = {
        {"r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1", 3, 1},
        {"7k/8/5K2/8/8/8/8/5Q2 w - - 0 1", 5, 2},
        {"6k1/8/6K1/8/8/8/8/7R w - - 0 1", 5, 2},
        {"8/8/4R3/8/4K3/8/8/4k3 w - - 0 1", 5, 3},
        {"8/k7/8/8/K7/1Q6/8/8 w - - 0 1", 5, 3},
        {"1k6/3Q4/8/8/8/K7/8/8 w - - 0 1", 7, 4},
        {"7k/8/5K2/8/8/8/8/6Q1 b - - 0 1", 3, -1},
    };
    for (const Case& c : cases) {
        Position pos = fromFEN(c.fen);
        const SearchResult ref = search(pos, c.depth);
        CHECK(mateInMoves(ref.score) == c.expectMate, std::string("reference finds the expected mate: ") + c.fen);
        const bool heavy = c.depth >= 7;  // the mate-in-4 search is the slow one: one policy is enough there
        for (TTMode mode : {TT_EXACT_DEPTH, TT_DEPTH_GE}) {
            for (TTReplace pol : POLICIES) {
                if (heavy && pol != TTReplace::BUCKET4) continue;
                TranspositionTable tt(2, pol);
                const SearchResult r = searchTT(pos, c.depth, tt, mode);
                CHECK(r.score == ref.score, std::string("mate distance unchanged (") + (mode == TT_EXACT_DEPTH ? "strict, " : "standard, ") +
                                                policyName(pol) + "): " + c.fen + " got " + std::to_string(r.score) + " expected " + std::to_string(ref.score));
                if (r.hasMove) CHECK(isLegal(pos, r.bestMove), "mating move legal");
            }
        }
        // Table kept across iterations of increasing depth (stale mate entries from shallower searches around).
        TranspositionTable tt(2);
        SearchResult last;
        for (int d = 1; d <= c.depth; ++d) last = searchTT(pos, d, tt, TT_DEPTH_GE);
        CHECK(last.score == ref.score, std::string("mate distance unchanged after iterating with one table: ") + c.fen);
    }
}

static void testIterativeWithTable() {
    const std::vector<std::string> fens = {START_FEN, KIWIPETE, MIDDLEGAME,
                                           "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                                           "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
                                           "7k/8/5K2/8/8/8/8/5Q2 w - - 0 1"};
    for (const std::string& fen : fens) {
        Position pos = fromFEN(fen);
        const Position copy = pos;
        {   // strict: the reported score is the table-less score at the completed depth
            TranspositionTable tt(4);
            SearchLimits lim; lim.maxDepth = 5; lim.tt = &tt; lim.ttMode = TT_EXACT_DEPTH;
            const SearchResult r = searchIterative(pos, lim);
            const SearchResult ref = search(pos, r.depth);
            CHECK(r.hasMove && r.depth >= 1, "iterative+table finishes: " + fen);
            CHECK(r.score == ref.score, "iterative strict score == plain score at depth " + std::to_string(r.depth) + ": " + fen);
            CHECK(isLegal(pos, r.bestMove) && !r.pv.empty() && r.pv[0] == r.bestMove && pvIsLegal(fen, r.pv), "iterative strict move/PV legal: " + fen);
        }
        {   // standard, with a callback; the table is kept for a second search of the same position
            TranspositionTable tt(4);
            SearchLimits lim; lim.maxDepth = 5; lim.tt = &tt;
            int infos = 0;
            const SearchResult r1 = searchIterative(pos, lim, [&](const SearchInfo& i) {
                ++infos;
                if (!pvIsLegal(fen, i.pv)) { ++testsFailed; std::cout << "FAIL: info PV illegal " << fen << "\n"; }
            });
            CHECK(infos >= 1 && r1.hasMove && isLegal(pos, r1.bestMove) && pvIsLegal(fen, r1.pv), "iterative standard: legal move and PV, info per depth: " + fen);
            const SearchResult r2 = searchIterative(pos, lim);
            CHECK(r2.hasMove && isLegal(pos, r2.bestMove), "second search with the warm table is legal: " + fen);
            CHECK(r2.nodes <= r1.nodes, "a warm table never costs more nodes here: " + fen + " (" + std::to_string(r1.nodes) + " -> " + std::to_string(r2.nodes) + ")");
        }
        CHECK(pos == copy, "position restored: " + fen);
    }
    {   // Interrupted search with a table still returns a safe, legal move.
        Position pos = fromFEN(KIWIPETE);
        TranspositionTable tt(4);
        SearchLimits lim; lim.maxNodes = 50; lim.tt = &tt;
        const SearchResult r = searchIterative(pos, lim);
        CHECK(r.hasMove && isLegal(pos, r.bestMove), "tiny node budget + table: legal fallback move");
    }
}

static void testWarmTableAndHashMove() {
    Position pos = fromFEN(MIDDLEGAME);
    TranspositionTable tt(16);
    const SearchResult first = searchTT(pos, 4, tt, TT_DEPTH_GE);
    const int fullAfterFirst = tt.hashfull();
    const SearchResult second = searchTT(pos, 4, tt, TT_DEPTH_GE);
    std::cout << "  [same depth-4 search twice with one table: " << first.nodes << " -> " << second.nodes << " nodes, hashfull " << fullAfterFirst << "]\n";
    CHECK(second.nodes * 4 < first.nodes, "a warm table makes the repeat search at least 4x cheaper");
    CHECK(second.score == first.score, "...with the same score");
    CHECK(tt.hits() > 0 && fullAfterFirst > 0, "the table was used");

    // Hash move: the table's move for the root position is the best move of the search.
    TTEntry e;
    CHECK(tt.probe(hashKey(pos), e) && packedMoveMatches(e.move, first.bestMove), "root entry holds the best move");
    CHECK(e.bound() == BOUND_EXACT && e.depth == 4, "root entry is exact at the searched depth");
}

static void testPoisonedAndHostileEntries() {
    for (const char* fen : {MIDDLEGAME, KIWIPETE, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"}) {
        Position pos = fromFEN(fen);
        const SearchResult ref = search(pos, 3);
        // 1. Garbage moves stored under the root's key and under every child's key: never legal, never used.
        {
            TranspositionTable tt(2);
            MoveList ml; generateLegalMoves(pos, ml);
            tt.store(hashKey(pos), 3, 0, BOUND_LOWER, 0x0FFF, 0);  // from 63 to 63: not a move
            tt.store(hashKey(pos), 3, 0, BOUND_LOWER, 0x7001, 1);  // promotion code on an impossible move
            for (size_t i = 0; i < ml.size(); ++i) {
                const Move m = ml[i];
                const UndoInfo u = makeMove(pos, m);
                tt.store(hashKey(pos), 0, 0, BOUND_UPPER, 0x0041, 0);  // a1 -> b1: legal in few positions; never trusted blindly
                unmakeMove(pos, m, u);
            }
            const SearchResult r = searchTT(pos, 3, tt, TT_EXACT_DEPTH);
            CHECK(r.hasMove && isLegal(pos, r.bestMove), std::string("garbage hash moves never produce an illegal move: ") + fen);
            CHECK(r.score == ref.score, std::string("garbage hash moves do not change the score: ") + fen);
        }
        // 2. A legal move that is wrong for this position (the best move of another position) is only a suggestion.
        {
            TranspositionTable tt(2);
            Position other = fromFEN(START_FEN);
            const SearchResult o = search(other, 2);
            tt.store(hashKey(pos), 3, 0, BOUND_LOWER, packMove(o.bestMove), 0);
            const SearchResult r = searchTT(pos, 3, tt, TT_EXACT_DEPTH);
            CHECK(r.hasMove && isLegal(pos, r.bestMove) && r.score == ref.score, std::string("a wrong-but-plausible hash move only changes ordering: ") + fen);
        }
        // 3. The root is never cut off by the table, even by a (bogus) exact entry that claims a huge depth.
        {
            TranspositionTable tt(2);
            tt.store(hashKey(pos), 60, 9999, BOUND_EXACT, 0, 0);
            const SearchResult r = searchTT(pos, 3, tt, TT_DEPTH_GE);
            CHECK(r.hasMove && isLegal(pos, r.bestMove), std::string("root entry never short-circuits the search: ") + fen);
            CHECK(r.nodes > 1, std::string("the root was searched: ") + fen);
            CHECK(r.score != 9999, std::string("the bogus root score is not returned: ") + fen);
        }
    }
}

static Move reversed(const Move& m) { Move r; r.from = m.to; r.to = m.from; return r; }

static void testPVExtensionFollowsTheTable() {
    // A depth-1 search returns a one-move PV. The table can continue it: the reported line must follow the stored
    // best moves, and must stop at a repeated position instead of looping.
    {
        Position pos = fromFEN(START_FEN);
        const SearchResult r0 = search(pos, 1);
        Position walk = pos;
        makeMove(walk, r0.bestMove);
        TranspositionTable tt(2);
        std::vector<Move> chain;
        Position w = walk;
        for (int i = 0; i < 4; ++i) {  // always the LAST legal move: unlike the first, it is not what a default would pick
            MoveList ml; generateLegalMoves(w, ml);
            const Move m = ml[ml.size() - 1];
            tt.store(hashKey(w), 0, 0, BOUND_LOWER, packMove(m), 0);
            chain.push_back(m);
            makeMove(w, m);
        }
        const SearchResult r = searchTT(pos, 1, tt, TT_DEPTH_GE);
        CHECK(r.bestMove == r0.bestMove, "depth-1 best move unchanged by the chain");
        CHECK(r.pv.size() == 5, "PV extended by exactly the 4 stored moves (size " + std::to_string(r.pv.size()) + ")");
        bool same = r.pv.size() == 5 && r.pv[0] == r0.bestMove;
        for (size_t i = 0; same && i < chain.size(); ++i) same = r.pv[i + 1] == chain[i];
        CHECK(same, "the extension follows the stored moves in order");
    }
    {
        // A cycle: both sides can step a piece back and forth. Find four moves that return to the same position.
        Position pos = fromFEN("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
        const SearchResult r0 = search(pos, 1);
        Position q = pos;
        makeMove(q, r0.bestMove);
        std::vector<Move> cyc;
        MoveList l1; generateLegalMoves(q, l1);
        for (size_t a = 0; a < l1.size() && cyc.empty(); ++a) {
            Position p1 = q; makeMove(p1, l1[a]);
            MoveList l2; generateLegalMoves(p1, l2);
            for (size_t b = 0; b < l2.size() && cyc.empty(); ++b) {
                Position p2 = p1; makeMove(p2, l2[b]);
                Move c3 = reversed(l1[a]), c4 = reversed(l2[b]);
                MoveList l3; generateLegalMoves(p2, l3);
                for (size_t c = 0; c < l3.size() && cyc.empty(); ++c) {
                    if (!(l3[c].from == c3.from && l3[c].to == c3.to)) continue;
                    Position p3 = p2; makeMove(p3, l3[c]);
                    MoveList l4; generateLegalMoves(p3, l4);
                    for (size_t d = 0; d < l4.size() && cyc.empty(); ++d) {
                        if (!(l4[d].from == c4.from && l4[d].to == c4.to)) continue;
                        Position p4 = p3; makeMove(p4, l4[d]);
                        if (hashKey(p4) == hashKey(q)) cyc = {l1[a], l2[b], l3[c], l4[d]};
                    }
                }
            }
        }
        CHECK(cyc.size() == 4, "test setup: found a 4-ply cycle");
        if (cyc.size() == 4) {
            TranspositionTable tt(2);
            Position w = q;
            for (const Move& m : cyc) { tt.store(hashKey(w), 0, 0, BOUND_LOWER, packMove(m), 0); makeMove(w, m); }
            const SearchResult r = searchTT(pos, 1, tt, TT_DEPTH_GE);
            CHECK(r.pv.size() == 5, "cyclic table chain: PV stops at the repeated position (size " + std::to_string(r.pv.size()) + ")");
            CHECK(pvIsLegal("4k3/8/8/8/8/8/8/R3K3 w - - 0 1", r.pv), "...and is legal");
        }
    }
}

static void testHashStaysConsistentInsideSearch() {
    // After any search the position, its base key and full hash are untouched.
    for (const std::string& fen : positions(2)) {
        Position pos = fromFEN(fen);
        const uint64_t base = pos.baseKey(), full = hashKey(pos);
        TranspositionTable tt(2);
        searchTT(pos, 3, tt, TT_DEPTH_GE);
        CHECK(pos.baseKey() == base && hashKey(pos) == full && pos.isConsistent(), "hash unchanged after searchTT: " + fen);
        SearchLimits lim; lim.maxDepth = 3; lim.tt = &tt;
        searchIterative(pos, lim);
        CHECK(pos.baseKey() == base && hashKey(pos) == full && pos.isConsistent(), "hash unchanged after searchIterative: " + fen);
    }
}

int main() {
    initAttackTables();
    const auto t0 = std::chrono::steady_clock::now();
    auto lap = [&](const char* what) {
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "  [" << what << " done at " << s << " s]\n";
    };

    std::cout << "Part A: hashing\n";
    testKeyTables(); testDeterminism(); testIncrementalOnPerftSuite(); lap("suite walk");
    testRandomPlayouts(); lap("random playouts");
    testTranspositions();

    std::cout << "Part B: transposition table\n";
    testPackedMoves(); testMateScoreConversion(); testTableBasics(); testReplacementPolicies();

    std::cout << "Part C: search\n";
    testStrictModeKeepsScores(); lap("strict mode");
    testOrderOnlyMode(); lap("order-only mode");
    testStoredEntriesAreTrue(); testModesTreatDeepEntriesDifferently(); lap("entry truth + modes");
    testStrictModeUnderHeavyReplacement(); lap("heavy replacement");
    testStandardMode(); testMatesKeepTheirDistance(); lap("mates");
    testIterativeWithTable(); testWarmTableAndHashMove(); testPoisonedAndHostileEntries(); testPVExtensionFollowsTheTable(); testHashStaysConsistentInsideSearch();
    lap("all");

    std::cout << "\n" << (testsRun - testsFailed) << "/" << testsRun << " checks passed.";
    if (testsFailed == 0) std::cout << " ALL TESTS PASSED";
    std::cout << "\n";
    return testsFailed == 0 ? 0 : 1;
}
