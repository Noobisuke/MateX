// Stage 20 tests: performance work (magic bitboards, inlining, POPCNT) and Lazy SMP.
//
//   test_stage20 [section ...]     sections: magic tables invariants tt smp uci
//
// magic       bishop / rook attacks against an independent ray-walk: EVERY subset of every relevant-occupancy mask (all
//             107,648 table entries) plus random full occupancies, and the queen union.
// tables      knight / king / pawn attack tables against coordinate arithmetic; isSquareAttacked on random positions.
// invariants  random games: incremental hash and consistency after every make, exact restore after every unmake (the
//             inlined putPiece/removePiece/castlingRightsAfter must not have changed a single bit), plus perft pins.
// tt          the lock-free slot encoding round-trips every field; four threads hammer a tiny table and no probe may ever
//             return an entry whose fields do not belong together (a torn read must look like a miss).
// smp         Lazy SMP: threads = 1 is bit-identical to the old search; with 2..4 threads the move is legal, the PV
//             plays out, mates are found, stop works, nodes are summed, the position is restored, repeated many times.
// uci         the Threads option and a threaded "go".
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/zobrist.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/perft.hpp"
#include "../src/rules/special.hpp"
#include "../src/search/search.hpp"
#include "../src/uci/uci.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static int testsRun = 0, testsFailed = 0;
#define CHECK(cond, msg) do { ++testsRun; if (!(cond)) { ++testsFailed; std::cout << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; } } while (0)

struct Rng {
    uint64_t s;
    uint64_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s >> 17 | (s << 47); }
    size_t below(size_t n) { return static_cast<size_t>(next() % n); }
};

// ---- independent references (deliberately NOT the code under test) ----------------------------------------------
static Bitboard refSliding(int sq, Bitboard occ, const int (*dirs)[2], int nDirs) {
    Bitboard out = 0;
    for (int k = 0; k < nDirs; ++k) {
        int f = sq & 7, r = sq >> 3;
        for (;;) {
            f += dirs[k][0]; r += dirs[k][1];
            if (f < 0 || f > 7 || r < 0 || r > 7) break;
            const Bitboard b = 1ULL << (r * 8 + f);
            out |= b;
            if (occ & b) break;  // a blocker is attacked, nothing behind it is
        }
    }
    return out;
}
static const int BISHOP_DIRS[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
static const int ROOK_DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static Bitboard refBishop(int sq, Bitboard occ) { return refSliding(sq, occ, BISHOP_DIRS, 4); }
static Bitboard refRook(int sq, Bitboard occ) { return refSliding(sq, occ, ROOK_DIRS, 4); }

// Squares whose occupancy can change the attack set: every ray square except the last one before the edge.
static Bitboard refMask(int sq, const int (*dirs)[2]) {
    Bitboard m = 0;
    for (int k = 0; k < 4; ++k) {
        int f = sq & 7, r = sq >> 3;
        for (;;) {
            f += dirs[k][0]; r += dirs[k][1];
            const int nf = f + dirs[k][0], nr = r + dirs[k][1];
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) break;  // (f, r) is the last square of this ray: not relevant
            m |= 1ULL << (r * 8 + f);
        }
    }
    return m;
}

static void testMagic() {
    initAttackTables();
    uint64_t entries = 0;
    for (int sq = 0; sq < 64; ++sq) {
        for (int bishop = 0; bishop < 2; ++bishop) {
            const Bitboard mask = refMask(sq, bishop ? BISHOP_DIRS : ROOK_DIRS);
            Bitboard sub = 0;
            bool allOk = true;
            do {  // Carry-Rippler: visits every subset of mask exactly once
                const Bitboard got = bishop ? bishopAttacksFrom(sq, sub) : rookAttacksFrom(sq, sub);
                const Bitboard want = bishop ? refBishop(sq, sub) : refRook(sq, sub);
                if (got != want) { allOk = false; break; }
                ++entries;
                sub = (sub - mask) & mask;
            } while (sub);
            CHECK(allOk, (bishop ? "bishop" : "rook") << " subsets, square " << sq);
        }
    }
    CHECK(entries == 102400 + 5248, "all 107,648 table entries checked, got " << entries);

    Rng rng{0x20202020ULL};
    int bad = 0;
    for (int i = 0; i < 300000; ++i) {
        const int sq = static_cast<int>(rng.below(64));
        Bitboard occ = rng.next() ^ (rng.next() << 20) ^ (rng.next() << 41);
        if (i & 1) occ &= rng.next();   // sparse
        if (i % 7 == 0) occ |= 1ULL << sq;  // the slider's own square may be set: it must not matter
        if (bishopAttacksFrom(sq, occ) != refBishop(sq, occ)) ++bad;
        if (rookAttacksFrom(sq, occ) != refRook(sq, occ)) ++bad;
        if (queenAttacksFrom(sq, occ) != (refBishop(sq, occ) | refRook(sq, occ))) ++bad;
    }
    CHECK(bad == 0, "random occupancies: " << bad << " mismatches");
    CHECK(bishopAttacksFrom(0, ~0ULL) == (1ULL << 9), "a1 bishop, full board: only b2");
    CHECK(rookAttacksFrom(27, 0) == (0x08080808F7080808ULL), "d4 rook, empty board");
    CHECK(queenAttacksFrom(27, 0) == (refBishop(27, 0) | refRook(27, 0)), "d4 queen, empty board");
}

static void testTables() {
    static const int KN[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    static const int KG[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard kn = 0, kg = 0, pw = 0, pb = 0;
        for (const auto& d : KN) { const int f = (sq & 7) + d[0], r = (sq >> 3) + d[1]; if (f >= 0 && f < 8 && r >= 0 && r < 8) kn |= 1ULL << (r * 8 + f); }
        for (const auto& d : KG) { const int f = (sq & 7) + d[0], r = (sq >> 3) + d[1]; if (f >= 0 && f < 8 && r >= 0 && r < 8) kg |= 1ULL << (r * 8 + f); }
        for (int df = -1; df <= 1; df += 2) {
            const int f = (sq & 7) + df, r = sq >> 3;
            if (f < 0 || f > 7) continue;
            if (r + 1 < 8) pw |= 1ULL << ((r + 1) * 8 + f);
            if (r - 1 >= 0) pb |= 1ULL << ((r - 1) * 8 + f);
        }
        CHECK(knightAttacksFrom(sq) == kn, "knight table " << sq);
        CHECK(kingAttacksFrom(sq) == kg, "king table " << sq);
        CHECK(pawnAttacksFrom(sq, WHITE) == pw, "white pawn table " << sq);
        CHECK(pawnAttacksFrom(sq, BLACK) == pb, "black pawn table " << sq);
    }
    // isSquareAttacked agrees with "some enemy piece attacks it" computed with the references, on real positions.
    Rng rng{77};
    Position pos;
    pos.setStartingPosition();
    int mismatches = 0, probes = 0;
    for (int game = 0; game < 40; ++game) {
        pos.setStartingPosition();
        for (int ply = 0; ply < 80; ++ply) {
            MoveList ml;
            generateLegalMoves(pos, ml);
            if (ml.size() == 0) break;
            makeMove(pos, ml[rng.below(ml.size())]);
            const Bitboard occ = pos.occupancyAll();
            for (int sq = 0; sq < 64; sq += 5) {
                for (Color by : {WHITE, BLACK}) {
                    ++probes;
                    // Reference built from first principles, piece by piece:
                    bool ref = false;
                    for (int s = 0; s < 64 && !ref; ++s) {
                        const Piece p = pos.pieceOn(s);
                        if (p == NO_PIECE || colorOf(p) != by) continue;
                        const int f = s & 7, r = s >> 3, tf = sq & 7, tr = sq >> 3;
                        const int df = std::abs(f - tf), dr = std::abs(r - tr);
                        switch (typeOf(p)) {
                            case PAWN: ref = df == 1 && (tr - r) == (by == WHITE ? 1 : -1); break;
                            case KNIGHT: ref = (df == 1 && dr == 2) || (df == 2 && dr == 1); break;
                            case KING: ref = s != sq && df <= 1 && dr <= 1; break;
                            case BISHOP: ref = (refBishop(s, occ) >> sq) & 1; break;
                            case ROOK: ref = (refRook(s, occ) >> sq) & 1; break;
                            case QUEEN: ref = ((refBishop(s, occ) | refRook(s, occ)) >> sq) & 1; break;
                            default: break;
                        }
                    }
                    if (isSquareAttacked(pos, sq, by) != ref) ++mismatches;
                }
            }
        }
    }
    CHECK(probes > 20000, "enough attack probes: " << probes);
    CHECK(mismatches == 0, "isSquareAttacked vs piece-by-piece reference: " << mismatches << " mismatches in " << probes);
}

static void testInvariants() {
    // Perft pins (well-known node counts): any rule change from the inlining would show here.
    struct P { const char* fen; int depth; uint64_t nodes; };
    const P pins[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
    };
    for (const P& p : pins) {
        Position pos;
        std::string err;
        const bool ok = loadFEN(pos, p.fen, &err);
        CHECK(ok, "load " << p.fen);
        if (ok) CHECK(perft(pos, p.depth) == p.nodes, "perft " << p.depth << " of " << p.fen);
    }
    // castlingRightsAfter against the rule written out by hand: a move from OR to e1/a1/h1/e8/a8/h8 removes exactly
    // the rights tied to that square, nothing else changes.
    {
        auto lostAt = [](int sq) -> int {
            switch (sq) {
                case 4: return WHITE_KINGSIDE | WHITE_QUEENSIDE;
                case 0: return WHITE_QUEENSIDE;
                case 7: return WHITE_KINGSIDE;
                case 60: return BLACK_KINGSIDE | BLACK_QUEENSIDE;
                case 56: return BLACK_QUEENSIDE;
                case 63: return BLACK_KINGSIDE;
                default: return 0;
            }
        };
        int badRights = 0;
        for (int rights = 0; rights < 16; ++rights)
            for (int from = 0; from < 64; ++from)
                for (int to = 0; to < 64; ++to) {
                    Move m;
                    m.from = static_cast<int8_t>(from);
                    m.to = static_cast<int8_t>(to);
                    const int want = rights & ~(lostAt(from) | lostAt(to));
                    if (static_cast<int>(castlingRightsAfter(static_cast<CastlingRights>(rights), m)) != want) ++badRights;
                }
        CHECK(badRights == 0, "castlingRightsAfter wrong in " << badRights << " cases");
    }
    // Random games: after every make the incremental key equals the recomputed one and the position is consistent;
    // after every unmake the position is bit-for-bit what it was.
    Rng rng{4242};
    int checked = 0, bad = 0;
    const char* starts[] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                            "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"};
    for (int g = 0; g < 150; ++g) {
        Position pos;
        loadFEN(pos, starts[g % 3]);
        for (int ply = 0; ply < 120; ++ply) {
            MoveList ml;
            generateLegalMoves(pos, ml);
            if (ml.size() == 0) break;
            const std::string before = toFEN(pos);
            const uint64_t keyBefore = hashKey(pos);
            // try every move and undo it: restore must be exact
            for (size_t i = 0; i < ml.size(); ++i) {
                UndoInfo u = makeMove(pos, ml[i]);
                if (!pos.isConsistent() || pos.baseKey() != recomputeBaseKey(pos)) ++bad;
                unmakeMove(pos, ml[i], u);
                if (toFEN(pos) != before || hashKey(pos) != keyBefore) ++bad;
                ++checked;
            }
            makeMove(pos, ml[rng.below(ml.size())]);
        }
    }
    CHECK(checked > 50000, "make/unmake pairs checked: " << checked);
    CHECK(bad == 0, "make/unmake invariant violations: " << bad);
}

// ---- transposition table ------------------------------------------------------------------------------------------
// Fields derived from the key: a probe that returns an entry whose fields do not match its key is a torn read.
static void derive(uint64_t key, int& depth, int& score, Bound& b, uint16_t& mv) {
    uint64_t h = key * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 29;
    depth = static_cast<int>(h % 60) - 5;               // includes negative depths
    score = static_cast<int>((h >> 8) % 4001) - 2000;   // includes negative scores, never a mate score
    b = static_cast<Bound>(1 + (h >> 20) % 3);
    mv = static_cast<uint16_t>(1 + (h >> 24) % 60000);
}

static void testTT() {
    {
        TranspositionTable tt(1, TTReplace::ALWAYS);
        Rng rng{5};
        int bad = 0, n = 0;
        for (int i = 0; i < 20000; ++i) {
            const uint64_t key = rng.next() | 1;
            int d, s; Bound b; uint16_t m;
            derive(key, d, s, b, m);
            tt.store(key, d, s, b, m, 0);
            TTEntry e;
            if (!tt.probe(key, e) || e.key != key || e.depth != d || e.score != s || e.bound() != b || e.move != m) ++bad;
            ++n;
        }
        CHECK(bad == 0, "slot encoding round trip: " << bad << " of " << n << " wrong");
        TTEntry e;
        CHECK(!tt.probe(0x123456789ABCDEFULL, e), "unknown key is a miss");
        tt.clear();
        CHECK(!tt.probe(1, e), "cleared table misses");
    }
    {
        // mate scores keep their ply conversion
        TranspositionTable tt(1);
        tt.store(99, 5, MATE_SCORE - 7, BOUND_EXACT, 3, 4);
        TTEntry e;
        CHECK(tt.probe(99, e) && scoreFromTT(e.score, 4) == MATE_SCORE - 7, "mate score survives the slot encoding");
        CHECK(scoreFromTT(e.score, 6) == MATE_SCORE - 9, "...and re-bases on the probing ply");
    }
#if MATEX_THREADS
    // Hammer: tiny table so the threads collide constantly. 4 threads x 400k operations.
    for (TTReplace pol : {TTReplace::ALWAYS, TTReplace::BUCKET4}) {
        TranspositionTable tt(1, pol);
        tt.resizeEntries(64);
        std::atomic<long> torn{0}, hits{0};
        std::vector<std::thread> pool;
        for (int t = 0; t < 4; ++t)
            pool.emplace_back([&, t]() {
                Rng rng{static_cast<uint64_t>(1000 + t)};
                for (int i = 0; i < 400000; ++i) {
                    const uint64_t key = (rng.next() % 300) * 0x1000193ULL + 1;  // few keys: constant overwriting
                    int d, s; Bound b; uint16_t m;
                    derive(key, d, s, b, m);
                    if (rng.next() & 1) {
                        tt.store(key, d, s, b, m, 0);
                    } else {
                        TTEntry e;
                        if (tt.probe(key, e)) {
                            ++hits;
                            if (e.key != key || e.depth != d || e.score != s || e.bound() != b || e.move != m) ++torn;
                        }
                    }
                }
            });
        for (auto& th : pool) th.join();
        CHECK(torn.load() == 0, "torn TT entries seen: " << torn.load() << " (policy " << static_cast<int>(pol) << ")");
        CHECK(hits.load() > 1000, "the hammer test really hit the table: " << hits.load());
    }
#endif
}

// ---- Lazy SMP -----------------------------------------------------------------------------------------------------
#if MATEX_THREADS
static bool pvPlays(Position pos, const std::vector<Move>& pv) {
    for (const Move& m : pv) {
        if (!isLegalMove(pos, m)) return false;
        makeMove(pos, m);
    }
    return true;
}
#endif

static void testSmp() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2Q1RK1 w - - 0 9",
    };
    // 1. threads = 1 is the old search, bit for bit: same nodes, score, PV, as a run that never mentions threads.
    for (const char* fen : fens) {
        Position a, b;
        loadFEN(a, fen);
        loadFEN(b, fen);
        TranspositionTable t1(8), t2(8);
        SearchLimits l1, l2;
        l1.maxDepth = l2.maxDepth = 7;
        l1.tt = &t1; l2.tt = &t2;
        l1.options = l2.options = SearchOptions::engineDefault();
        l2.threads = 1;
        const SearchResult r1 = searchIterative(a, l1), r2 = searchIterative(b, l2);
        CHECK(r1.nodes == r2.nodes && r1.score == r2.score && r1.bestMove == r2.bestMove, "threads=1 identical: " << fen);
    }
    // threads > 1 without a table is ignored (helpers could not communicate): same as one thread.
    {
        Position a, b;
        loadFEN(a, fens[1]);
        loadFEN(b, fens[1]);
        SearchLimits l1, l2;
        l1.maxDepth = l2.maxDepth = 5;
        l2.threads = 4;
        const SearchResult r1 = searchIterative(a, l1), r2 = searchIterative(b, l2);
        CHECK(r1.nodes == r2.nodes && r1.score == r2.score, "threads>1 without a table behaves as one thread");
    }
#if MATEX_THREADS
    // 2. Multi-threaded searches are legal and sane, many times over (a race shows up as a crash, a wrong move or TSan).
    int runs = 0;
    for (int rep = 0; rep < 6; ++rep) {
        for (const char* fen : fens) {
            for (int threads : {2, 3, 4}) {
                Position pos;
                loadFEN(pos, fen);
                const std::string before = toFEN(pos);
                const uint64_t key = hashKey(pos);
                TranspositionTable tt(4);
                SearchLimits lim;
                lim.maxDepth = 6;
                lim.tt = &tt;
                lim.threads = threads;
                lim.options = SearchOptions::engineDefault();
                int infoCalls = 0;
                uint64_t lastInfoNodes = 0;
                const SearchResult r = searchIterative(pos, lim, [&](const SearchInfo& i) { ++infoCalls; lastInfoNodes = i.nodes; });
                ++runs;
                CHECK(r.hasMove && isLegalMove(pos, r.bestMove), "legal best move: " << fen << " threads " << threads);
                CHECK(r.depth == 6, "completed depth 6, got " << r.depth);
                CHECK(infoCalls == 6, "one info per completed iteration from the main thread only: " << infoCalls);
                CHECK(!r.pv.empty() && r.pv[0] == r.bestMove && pvPlays(pos, r.pv), "PV is legal and starts with the best move");
                CHECK(std::abs(r.score) < 2000, "sane score " << r.score);
                CHECK(r.nodes > 0, "nodes counted");
                CHECK(r.nodes >= lastInfoNodes, "final nodes (" << r.nodes << ") include everything the last info line counted (" << lastInfoNodes << ")");
                CHECK(toFEN(pos) == before && hashKey(pos) == key && pos.isConsistent(), "root position restored");
            }
        }
    }
    CHECK(runs == 90, "ran " << runs);

    // 3. Mates are still found, and the reported mate is real.
    {
        struct M { const char* fen; int plies; };
        const M mates[] = {{"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 1},                  // Ra8#
                           {"r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", 1},  // Qxf7#
                           {"k7/8/1K6/8/8/8/8/7R w - - 0 1", 3}};                          // Rh8#
        for (const M& m : mates) {
            Position pos;
            loadFEN(pos, m.fen);
            TranspositionTable tt(4);
            SearchLimits lim;
            lim.maxDepth = 6;
            lim.tt = &tt;
            lim.threads = 3;
            lim.options = SearchOptions::engineDefault();
            const SearchResult r = searchIterative(pos, lim);
            CHECK(isMateScore(r.score) && r.score > 0, "mate found with 3 threads: " << m.fen << " score " << r.score);
            Position q = pos;
            bool played = r.hasMove && isLegalMove(q, r.bestMove);
            CHECK(played, "mate move legal");
        }
    }

    // 4. Terminal roots do not hang or crash with helpers.
    {
        const char* terminals[] = {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",   // stalemate
                                   "R5k1/5ppp/8/8/8/8/8/6K1 b - - 0 1"};  // checkmate
        for (const char* fen : terminals) {
            Position pos;
            loadFEN(pos, fen);
            TranspositionTable tt(1);
            SearchLimits lim;
            lim.maxDepth = 5;
            lim.tt = &tt;
            lim.threads = 4;
            const SearchResult r = searchIterative(pos, lim);
            CHECK(!r.hasMove, "no move in a terminal position: " << fen);
        }
    }

    // 5. stop and movetime: helpers must follow the main thread and everything is joined promptly.
    {
        Position pos;
        loadFEN(pos, fens[1]);
        TranspositionTable tt(16);
        std::atomic<bool> stop{false};
        SearchLimits lim;
        lim.tt = &tt;
        lim.threads = 4;
        lim.stop = &stop;
        lim.options = SearchOptions::engineDefault();
        std::thread stopper([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(300)); stop = true; });
        const auto t0 = std::chrono::steady_clock::now();
        const SearchResult r = searchIterative(pos, lim);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        stopper.join();
        CHECK(r.hasMove && isLegalMove(pos, r.bestMove), "legal move after stop");
        CHECK(ms < 1500, "stopped promptly: " << ms << " ms");
        CHECK(r.interrupted, "reported as interrupted");

        Position p2;
        loadFEN(p2, fens[4]);
        TranspositionTable tt2(16);
        SearchLimits l2;
        l2.tt = &tt2;
        l2.threads = 3;
        l2.movetimeMs = 400;
        l2.options = SearchOptions::engineDefault();
        const auto t1 = std::chrono::steady_clock::now();
        const SearchResult r2 = searchIterative(p2, l2);
        const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        CHECK(r2.hasMove && isLegalMove(p2, r2.bestMove), "legal move after movetime");
        CHECK(ms2 < 1500, "movetime respected with helpers: " << ms2 << " ms");
        CHECK(r2.depth >= 2, "reached a useful depth: " << r2.depth);
    }
#endif
}

// ---- UCI ------------------------------------------------------------------------------------------------------------
static void testUci() {
    std::ostringstream os;
    UciEngine eng(os);
    eng.handleLine("uci");
    const std::string out = os.str();
    CHECK(out.find("option name Threads type spin default 1 min 1 max ") != std::string::npos, "Threads option advertised");
    CHECK(eng.threads() == 1, "default is one thread");
    eng.handleLine("setoption name Threads value 3");
#if MATEX_THREADS
    CHECK(eng.threads() == 3, "Threads value 3 accepted");
    eng.handleLine("setoption name Threads value 0");
    CHECK(eng.threads() == 3, "0 is rejected");
    eng.handleLine("setoption name Threads value 65");
    CHECK(eng.threads() == 3, "65 is rejected");
    eng.handleLine("setoption name Threads value abc");
    CHECK(eng.threads() == 3, "garbage is rejected");
    os.str("");
    eng.handleLine("position startpos moves e2e4 e7e5");
    eng.handleLine("go depth 6");
    eng.waitForSearch();
    const std::string r = os.str();
    int best = 0;
    std::istringstream in(r);
    std::string l, move;
    while (std::getline(in, l)) if (l.rfind("bestmove ", 0) == 0) { ++best; move = l.substr(9); }
    CHECK(best == 1, "exactly one bestmove with 3 threads: " << best);
    Position pos;
    loadFEN(pos, "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
    MoveList ml;
    generateLegalMoves(pos, ml);
    bool legal = false;
    for (size_t i = 0; i < ml.size(); ++i) if (ml[i].toString() == move) legal = true;
    CHECK(legal, "bestmove " << move << " is legal");
    CHECK(r.find("info depth 6 ") != std::string::npos, "info lines from the main thread");
    os.str("");
    eng.handleLine("options");
    CHECK(os.str().find("threads 3") != std::string::npos, "options shows threads 3");
    // go infinite + stop with helpers running
    os.str("");
    eng.handleLine("go infinite");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(os.str().find("bestmove") == std::string::npos, "no bestmove before stop");
    eng.handleLine("stop");
    eng.waitForSearch();
    CHECK(os.str().find("bestmove ") != std::string::npos, "bestmove after stop");
#else
    CHECK(eng.threads() == 1, "single-thread build keeps one thread");
#endif
}

int main(int argc, char** argv) {
    std::vector<std::string> want;
    for (int i = 1; i < argc; ++i) want.push_back(argv[i]);
    auto on = [&](const char* name) {
        if (want.empty()) return true;
        for (const std::string& w : want) if (w == name) return true;
        return false;
    };
    if (on("magic")) testMagic();
    if (on("tables")) testTables();
    if (on("invariants")) testInvariants();
    if (on("tt")) testTT();
    if (on("smp")) testSmp();
    if (on("uci")) testUci();
    std::cout << testsRun << " checks, " << testsFailed << " failed\n";
    return testsFailed == 0 ? 0 : 1;
}
