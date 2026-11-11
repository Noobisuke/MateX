// Stage 19 tests: the UCI front end (src/uci/).
//
//   test_stage19 [section ...]     sections: go time position go search options debug robust
//
// go        parsing of the "go" parameters.
// time      the time-allocation rule, number for number.
// position  "position" parsing: startpos / fen / moves, castling, en passant, promotion, illegal input, history keys,
//           and a differential test against direct makeMove on random games.
// search    go depth / nodes / movetime / clocks / mate / infinite + stop, output grammar (info lines, PVs legal), no-legal-move
//           positions, a new "go" replacing a running one, quit while searching, end of input.
// options   setoption: Hash, Clear Hash, Move Overhead, Techniques, EvalFeatures, Debug Log File, bad values.
// debug     d, eval, perft, divide, bench (deterministic), options.
// robust    thousands of garbage / mutated lines: the engine never crashes and still answers "isready".
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/zobrist.hpp"
#include "../src/eval/evaluate.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/perft/perft.hpp"
#include "../src/uci/timeman.hpp"
#include "../src/uci/uci.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

static int testsRun = 0, testsFailed = 0;
#define CHECK(cond, msg) do { ++testsRun; if (!(cond)) { ++testsFailed; std::cout << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; } } while (0)

static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
static const char* KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

struct Rng {
    uint64_t s;
    uint64_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s >> 33; }
    size_t below(size_t n) { return static_cast<size_t>(next() % n); }
};

static void sleepMs(int ms) {
#if MATEX_THREADS
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#else
    (void)ms;  // single-threaded build: the tests that need a second thread are skipped
#endif
}

// A stream buffer that the engine's worker thread and the test thread can use at the same time.
class SyncBuf : public std::streambuf {
    struct Lock { MateXMutex& m; explicit Lock(MateXMutex& mm) : m(mm) { m.lock(); } ~Lock() { m.unlock(); } };
public:
    std::string snapshot() { Lock g(m_); return text_; }
    void clear() { Lock g(m_); text_.clear(); }
protected:
    int overflow(int c) override { if (c != EOF) { Lock g(m_); text_ += static_cast<char>(c); } return c; }
    std::streamsize xsputn(const char* s, std::streamsize n) override { Lock g(m_); text_.append(s, static_cast<size_t>(n)); return n; }
private:
    MateXMutex m_;
    std::string text_;
};

static std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) out.push_back(l);
    return out;
}

struct Session {
    SyncBuf buf;
    std::ostream os{&buf};
    UciEngine engine{os};
    void send(const std::string& line) { if (std::getenv("UCI_TRACE")) std::cerr << "> " << line.substr(0, 120) << "\n"; engine.handleLine(line); }
    void done() { engine.waitForSearch(); }
    std::string text() { return buf.snapshot(); }
    std::vector<std::string> lines() { return linesOf(buf.snapshot()); }
    void clear() { buf.clear(); }
    // Sends the line, waits for a finite search, returns the output it produced.
    std::string run(const std::string& line) { clear(); send(line); done(); return text(); }
};

static bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
static bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

static std::string bestmoveOf(const std::string& text) {
    for (const std::string& l : linesOf(text))
        if (startsWith(l, "bestmove ")) return splitTokens(l)[1];
    return "";
}
static int countLines(const std::string& text, const std::string& prefix) {
    int n = 0;
    for (const std::string& l : linesOf(text)) if (startsWith(l, prefix)) ++n;
    return n;
}

static Position fromFEN(const std::string& fen) {
    Position pos;
    std::string err;
    if (!loadFEN(pos, fen, &err)) { std::cout << "SETUP FAILED: " << fen << ": " << err << "\n"; ++testsFailed; }
    return pos;
}
static bool legalMove(const Position& p, const std::string& uci) {
    MoveList ml;
    generateLegalMoves(p, ml);
    for (size_t i = 0; i < ml.size(); ++i) if (ml[i].toString() == uci) return true;
    return false;
}
static bool playUci(Position& p, const std::string& uci) {
    MoveList ml;
    generateLegalMoves(p, ml);
    for (size_t i = 0; i < ml.size(); ++i) if (ml[i].toString() == uci) { makeMove(p, ml[i]); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
static void testGoParsing() {
    std::cout << "go parameters\n";
    GoParams g;
    std::string err;
    CHECK(parseGo({"depth", "7"}, g, &err) && g.depth == 7 && err.empty(), "depth");
    CHECK(parseGo({"nodes", "12345"}, g) && g.nodes == 12345, "nodes");
    CHECK(parseGo({"movetime", "250"}, g) && g.movetime == 250, "movetime");
    CHECK(parseGo({"wtime", "60000", "btime", "59000", "winc", "1000", "binc", "500", "movestogo", "20"}, g) && g.wtime == 60000 &&
              g.btime == 59000 && g.winc == 1000 && g.binc == 500 && g.movestogo == 20, "clock parameters");
    CHECK(parseGo({"infinite"}, g) && g.infinite && !g.ponder, "infinite");
    CHECK(parseGo({"ponder", "wtime", "1000", "btime", "1000"}, g) && g.ponder && g.infinite, "ponder is treated as infinite");
    CHECK(parseGo({"mate", "3"}, g) && g.mate == 3, "mate");
    CHECK(parseGo({"perft", "4"}, g) && g.perft == 4, "perft extension");
    CHECK(parseGo({}, g) && g.depth == 0 && g.wtime == -1 && g.btime == -1 && !g.infinite, "empty go: nothing set");
    CHECK(parseGo({"searchmoves", "e2e4", "d2d4", "depth", "3"}, g) && g.searchmoves && g.depth == 3, "searchmoves list is skipped, the next keyword still parses");
    CHECK(parseGo({"depth", "3", "searchmoves", "e7e8q"}, g) && g.searchmoves && g.depth == 3, "searchmoves at the end");
    CHECK(parseGo({"wtime", "-50", "btime", "1000"}, g) && g.wtime == -50, "a negative clock is kept");
    err.clear();
    CHECK(!parseGo({"depth"}, g, &err) && !err.empty(), "missing number is reported");
    err.clear();
    CHECK(!parseGo({"depth", "abc"}, g, &err) && !err.empty(), "bad number is reported");
    err.clear();
    CHECK(!parseGo({"banana", "depth", "4"}, g, &err) && g.depth == 4 && !err.empty(), "unknown token is reported, the rest still parses");
    CHECK(!parseGo({"nodes", "99999999999999999999"}, g), "a number that is too long is rejected");
    CHECK(parseGo({"depth", "999999999999"}, g) && g.depth > 0, "huge depth is clamped, not wrapped");
    CHECK(parseGo({"movetime", "-5"}, g) && g.movetime == 0, "negative movetime becomes 0");
    CHECK(parseGo({"winc", "-50", "binc", "-7"}, g) && g.winc == 0 && g.binc == 0, "negative increments become 0");
}

static void testTimeAllocation() {
    std::cout << "time allocation\n";
    GoParams g;
    parseGo({"movetime", "1000"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 970, "movetime minus overhead");
    CHECK(allocateTime(g, BLACK, 0) == 1000, "movetime, no overhead, either colour");
    parseGo({"movetime", "10"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 1, "movetime smaller than the overhead: 1 ms, never 0 or negative");
    parseGo({"wtime", "60000", "btime", "30000"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 2000, "sudden death: 60000/30");
    CHECK(allocateTime(g, BLACK, 30) == 1000, "sudden death uses the mover's own clock: 30000/30");
    parseGo({"wtime", "60000", "btime", "60000", "movestogo", "10"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 6000, "movestogo 10: 60000/10");
    parseGo({"wtime", "60000", "btime", "60000", "movestogo", "500"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 1200, "movestogo is capped at 50: 60000/50");
    parseGo({"wtime", "60000", "btime", "60000", "winc", "1000", "binc", "0"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 2750, "increment: 2000 + 3/4 of 1000");
    CHECK(allocateTime(g, BLACK, 30) == 2000, "the opponent's increment is irrelevant");
    parseGo({"wtime", "1000", "btime", "1000", "movestogo", "1"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 776, "last move before the control: at most 80% of (clock - overhead)");
    parseGo({"wtime", "100", "btime", "100"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 3, "almost no time: 100/30");
    parseGo({"wtime", "20", "btime", "20"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 1, "clock below the overhead: still at least 1 ms");
    parseGo({"wtime", "-100", "btime", "5000"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 1, "a flagged clock: 1 ms");
    parseGo({"depth", "5"}, g);
    CHECK(allocateTime(g, WHITE, 30) == 0, "no clock: no time limit");
    parseGo({"wtime", "5000"}, g);
    CHECK(allocateTime(g, BLACK, 30) == 0, "only the opponent's clock: no limit (the engine adds a fallback)");
    parseGo({"wtime", "3000", "btime", "3000", "movetime", "500"}, g);
    CHECK(allocateTime(g, WHITE, 0) == 500, "movetime wins over the clocks");
    // The allocation never exceeds the clock.
    bool never = true;
    for (int64_t clock = 0; clock <= 5000; clock += 37)
        for (int inc = 0; inc <= 3000; inc += 500)
            for (int mtg : {0, 1, 2, 10, 40}) {
                GoParams h; h.wtime = clock; h.btime = clock; h.winc = inc; h.binc = inc; h.movestogo = mtg;
                const int64_t t = allocateTime(h, WHITE, 30);
                if (t < 1 || (clock > 31 && t > (clock - 30))) never = false;
            }
    CHECK(never, "allocation is at least 1 ms and never more than clock - overhead");
}

static void testPosition() {
    std::cout << "position\n";
    {
        Session s;
        CHECK(toFEN(s.engine.position()) == START_FEN, "the engine starts at the start position");
        s.send("position startpos");
        CHECK(toFEN(s.engine.position()) == START_FEN && s.engine.history().empty(), "position startpos");
        s.send("position startpos moves e2e4 e7e5 g1f3");
        CHECK(toFEN(s.engine.position()) == "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2", "startpos + three moves");
        CHECK(s.engine.history().size() == 3, "history holds the positions before the last one");
        Position p = fromFEN(START_FEN);
        CHECK(s.engine.history()[0] == hashKey(p), "history[0] is the hash of the start position");
        playUci(p, "e2e4");
        CHECK(s.engine.history()[1] == hashKey(p), "history[1] is the hash after e2e4");
        s.send("position startpos moves g1f3 g8f6 f3g1 f6g8");
        CHECK(s.engine.history().size() == 4 && hashKey(s.engine.position()) == s.engine.history()[0],
              "after a knight shuffle the current position equals the first history entry");
        s.send("position fen " + std::string(KIWIPETE));
        CHECK(toFEN(s.engine.position()) == KIWIPETE && s.engine.history().empty(), "position fen");
        s.send("position fen " + std::string(KIWIPETE) + " moves e1g1 b4c3");
        CHECK(toFEN(s.engine.position()) == "r3k2r/p1ppqpb1/bn2pnp1/3PN3/4P3/2p2Q1p/PPPBBPPP/R4RK1 w kq - 0 2", "fen + castling and a capture");
        s.send("position fen 4k3/P7/8/8/8/8/8/4K3 w - - 0 1 moves a7a8q");
        CHECK(toFEN(s.engine.position()) == "Q3k3/8/8/8/8/8/8/4K3 b - - 0 1", "promotion move text a7a8q");
        s.send("position fen 4k3/P7/8/8/8/8/8/4K3 w - - 0 1 moves a7a8N");
        CHECK(toFEN(s.engine.position()) == "N3k3/8/8/8/8/8/8/4K3 b - - 0 1", "an upper-case promotion letter is accepted");
        s.send("position fen rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3 moves d4e3");
        CHECK(toFEN(s.engine.position()) == "rnbqkbnr/ppp1pppp/8/8/8/4p3/PPPP1PPP/RNBQKBNR w KQkq - 0 4", "en passant capture");
    }
    {  // Bad input leaves the position untouched and says why.
        Session s;
        s.send("position startpos moves e2e4");
        const std::string before = toFEN(s.engine.position());
        const size_t hist = s.engine.history().size();
        const char* bad[] = {"position startpos moves e2e5", "position startpos moves e2e4 e2e4", "position startpos moves xyz",
                             "position fen garbage", "position fen", "position", "position bogus", "position startpos nonsense",
                             "position fen 8/8/8/8/8/8/8/8 w - - 0 1",                                   // no kings
                             "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w",    // too few fields
                             "position fen 4k3/8/8/8/8/8/8/3KK3 w - - 0 1",                              // two white kings
                             "position fen 4k3/8/8/8/8/8/8/4K2k w - - 0 1"};                             // two black kings
        for (const char* b : bad) {
            s.clear();
            s.send(b);
            CHECK(toFEN(s.engine.position()) == before && s.engine.history().size() == hist, std::string("position unchanged after: ") + b);
            CHECK(contains(s.text(), "info string"), std::string("an explanation is printed for: ") + b);
        }
        s.send("position startpos moves e2e4 e7e5 d1h5 b8c6 f1c4 g8f6 h5f7");
        CHECK(toFEN(s.engine.position()) == "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4", "scholar's mate position");
    }
    {  // ucinewgame
        Session s;
        s.send("position startpos moves e2e4 e7e5");
        s.send("ucinewgame");
        CHECK(toFEN(s.engine.position()) == START_FEN && s.engine.history().empty(), "ucinewgame resets the position and the history");
    }
    {  // Differential: UCI move text applied by the engine equals makeMove on random games.
        Session s;
        Rng rng{12345};
        int games = 0, plies = 0;
        for (int g = 0; g < 60; ++g) {
            Position p = fromFEN(g % 3 == 0 ? KIWIPETE : START_FEN);
            const std::string base = g % 3 == 0 ? "position fen " + std::string(KIWIPETE) : "position startpos";
            std::string line = base + " moves";
            bool moved = false;
            for (int ply = 0; ply < 80; ++ply) {
                MoveList ml;
                generateLegalMoves(p, ml);
                if (ml.size() == 0) break;
                const Move m = ml[rng.below(ml.size())];
                line += " " + m.toString();
                makeMove(p, m);
                moved = true;
                ++plies;
            }
            if (!moved) continue;
            s.send(line);
            ++games;
            if (toFEN(s.engine.position()) != toFEN(p)) { CHECK(false, "differential game " + std::to_string(g) + ": " + line); break; }
            CHECK(hashKey(s.engine.position()) == hashKey(p), "differential hash");
        }
        CHECK(games >= 50 && plies > 2000, "the differential test covered enough games");
    }
}

static bool pvLegal(const std::string& fen, const std::vector<std::string>& pv) {
    Position p = fromFEN(fen);
    for (const std::string& m : pv) if (!playUci(p, m)) return false;
    return true;
}

// Checks the grammar of every info line: depth rises, tokens in order, PV legal. Returns the number of info lines seen.
static int checkInfoLines(const std::string& text, const std::string& rootFen, const std::string& what) {
    int n = 0, lastDepth = 0;
    for (const std::string& l : linesOf(text)) {
        if (!startsWith(l, "info depth ")) continue;
        ++n;
        const auto t = splitTokens(l);
        // info depth D score (cp X | mate N) nodes N nps N time T hashfull H [pv ...]
        bool ok = t.size() >= 14 && t[0] == "info" && t[1] == "depth" && t[3] == "score" && (t[4] == "cp" || t[4] == "mate") && t[6] == "nodes" &&
                  t[8] == "nps" && t[10] == "time" && t[12] == "hashfull";
        CHECK(ok, what + ": info line grammar: " + l);
        if (!ok) continue;
        const int depth = std::atoi(t[2].c_str());
        CHECK(depth > lastDepth, what + ": depth increases");
        lastDepth = depth;
        CHECK(std::atoi(t[7].c_str()) > 0, what + ": nodes positive");
        const int hf = std::atoi(t[13].c_str());
        CHECK(hf >= 0 && hf <= 1000, what + ": hashfull in 0..1000");
        std::vector<std::string> pv;
        if (t.size() > 15 && t[14] == "pv") pv.assign(t.begin() + 15, t.end());
        CHECK(!pv.empty() && pvLegal(rootFen, pv), what + ": PV is a legal line: " + l);
    }
    return n;
}

static void testSearch() {
    std::cout << "go and search\n";
    {  // go depth
        Session s;
        const std::string out = s.run("go depth 4");
        const std::string best = bestmoveOf(out);
        CHECK(legalMove(fromFEN(START_FEN), best), "go depth 4 from the start gives a legal move: " + best);
        CHECK(checkInfoLines(out, START_FEN, "depth 4") == 4, "one info line per depth 1..4");
        CHECK(countLines(out, "bestmove") == 1, "exactly one bestmove");
        CHECK(s.lines().back().compare(0, 8, "bestmove") == 0, "bestmove is the last line");
    }
    {  // go depth in a middlegame and a root that is not the start
        Session s;
        s.send("position fen " + std::string(KIWIPETE));
        const std::string out = s.run("go depth 3");
        CHECK(legalMove(fromFEN(KIWIPETE), bestmoveOf(out)), "kiwipete: legal bestmove");
        checkInfoLines(out, KIWIPETE, "kiwipete");
    }
    {  // nodes
        Session s;
        const std::string out = s.run("go nodes 3000");
        CHECK(legalMove(fromFEN(START_FEN), bestmoveOf(out)), "go nodes gives a legal move");
        const auto lines = linesOf(out);
        uint64_t lastNodes = 0;
        for (const auto& l : lines) if (startsWith(l, "info depth")) lastNodes = std::strtoull(splitTokens(l)[7].c_str(), nullptr, 10);
        CHECK(lastNodes <= 3000, "the node limit is respected (last completed iteration: " + std::to_string(lastNodes) + ")");
    }
    {  // movetime
        Session s;
        s.clear();
        const auto t0 = std::chrono::steady_clock::now();
        s.send("go movetime 200");
        s.done();
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(legalMove(fromFEN(START_FEN), bestmoveOf(s.text())), "go movetime gives a legal move");
        CHECK(ms >= 10 && ms < 600, "go movetime 200 returned after " + std::to_string(ms) + " ms");
    }
    {  // clocks
        Session s;
        s.clear();
        const auto t0 = std::chrono::steady_clock::now();
        s.send("go wtime 300 btime 300 winc 0 binc 0");
        s.done();
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(legalMove(fromFEN(START_FEN), bestmoveOf(s.text())), "go with a short clock gives a legal move");
        CHECK(ms < 300, "300 ms on the clock: the move came after " + std::to_string(ms) + " ms");
        s.send("position startpos moves e2e4");
        const std::string out = s.run("go wtime 100 btime 400 winc 50 binc 50 movestogo 5");
        CHECK(legalMove(fromFEN("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"), bestmoveOf(out)), "Black to move uses btime");
        const std::string only = s.run("go wtime 2000");
        CHECK(bestmoveOf(only) != "", "only the opponent's clock was given: still answers");
    }
    {  // mate
        Session s;
        s.send("position fen 6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1");  // Ra8#
        const std::string out = s.run("go depth 4");
        CHECK(bestmoveOf(out) == "a1a8", "back-rank mate in one: " + bestmoveOf(out));
        CHECK(contains(out, "score mate 1"), "mate is reported as 'score mate 1'");
        s.send("position fen 6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1");
        const std::string m = s.run("go mate 1");
        CHECK(bestmoveOf(m) == "a1a8", "go mate 1");
        // "go mate 2" needs a 4-ply search (mate in 2 moves = 3 plies, plus the search's horizon): Kf6-g6, Kg8, Ra8#.
        s.send("position fen 7k/8/5K2/8/8/8/8/R7 w - - 0 1");
        const std::string m2 = s.run("go mate 2");
        CHECK(contains(m2, "score mate 2"), "go mate 2 searches deep enough to find the mate in 2");
        // Black is mated: the score is negative.
        s.send("position fen 7k/8/6QK/8/8/8/8/8 b - - 0 1");  // Black king h8, White Qg6 + Kh6: no legal move and not in check = stalemate
        const std::string st = s.run("go depth 3");
        CHECK(bestmoveOf(st) == "0000", "stalemate: bestmove 0000");
        s.send("position fen R5k1/5ppp/8/8/8/8/8/4K3 b - - 0 1");  // already mated
        const std::string mated = s.run("go depth 3");
        CHECK(bestmoveOf(mated) == "0000", "checkmated side to move: bestmove 0000");
        s.send("position fen 4k3/8/8/8/8/8/8/R3K3 b - - 0 1");
        const std::string neg = s.run("go depth 1");
        CHECK(legalMove(fromFEN("4k3/8/8/8/8/8/8/R3K3 b - - 0 1"), bestmoveOf(neg)), "a normal black move");
    }
    {  // other endgames: legal answers
        Session s;
        s.send("position fen k7/8/1K6/8/8/8/8/7r b - - 0 1");
        const std::string out = s.run("go depth 4");
        CHECK(legalMove(fromFEN("k7/8/1K6/8/8/8/8/7r b - - 0 1"), bestmoveOf(out)), "black rook endgame: legal move");
        s.send("position fen 6k1/8/8/8/8/8/5PPP/6K1 w - - 0 1");
        CHECK(legalMove(fromFEN("6k1/8/8/8/8/8/5PPP/6K1 w - - 0 1"), bestmoveOf(s.run("go depth 4"))), "quiet endgame: legal move");
    }
    {  // checkmate delivered by the side that is "mated in n" shows a negative number
        Session s;
        s.send("position fen 7k/6pp/8/8/8/8/6PP/r6K w - - 0 1");   // White's king is boxed in on h1 by Ra1 and its own pawns: White is mated in 0 (Ra1#)
        const std::string out = s.run("go depth 3");
        CHECK(bestmoveOf(out) == "0000", "already checkmated (back rank): no move");
        s.send("position fen 7k/8/8/8/8/8/6PP/r5K1 w - - 0 1");   // Kg1, pawns g2 h2, Ra1+: this is check, Kf2/Kh... king can go f2
        CHECK(bestmoveOf(s.run("go depth 3")) != "", "in check with an escape");
    }
    {  // infinite + stop
        Session s;
        if (UciEngine::threadsAvailable()) {
            s.clear();
            s.send("go infinite");
            sleepMs(300);
            CHECK(s.engine.searching(), "go infinite is running");
            CHECK(countLines(s.text(), "info depth") >= 1, "infinite search reports progress");
            CHECK(countLines(s.text(), "bestmove") == 0, "no bestmove while the search is infinite");
            s.send("isready");
            CHECK(contains(s.text(), "readyok"), "isready is answered during a search");
            s.send("stop");
            s.done();
            CHECK(countLines(s.text(), "bestmove") == 1, "stop produces exactly one bestmove");
            CHECK(legalMove(fromFEN(START_FEN), bestmoveOf(s.text())), "the move after stop is legal");
            CHECK(!s.engine.searching(), "not searching after stop");
            // An infinite search that finds a forced mate must still wait for "stop".
            s.send("position fen 6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1");
            s.clear();
            s.send("go infinite");
            sleepMs(300);
            CHECK(countLines(s.text(), "bestmove") == 0, "infinite + forced mate: still no bestmove before stop");
            s.send("stop");
            s.done();
            CHECK(bestmoveOf(s.text()) == "a1a8", "after stop the mating move is reported");
            // A bare "go" is infinite as well.
            s.send("position startpos");
            s.clear();
            s.send("go");
            sleepMs(150);
            CHECK(countLines(s.text(), "bestmove") == 0 && s.engine.searching(), "a bare 'go' is infinite");
            s.send("stop");
            s.done();
            CHECK(countLines(s.text(), "bestmove") == 1, "bare go: bestmove after stop");
            // A new go replaces the running search: the old one finishes with its bestmove first.
            s.clear();
            s.send("go infinite");
            sleepMs(100);
            s.send("go depth 2");
            s.done();
            CHECK(countLines(s.text(), "bestmove") == 2, "a second go stops the first search (two bestmoves in total)");
            // go ponder is not supported: it is explained, and treated as an infinite search.
            s.clear();
            s.send("go ponder wtime 1000 btime 1000");
            sleepMs(100);
            CHECK(contains(s.text(), "pondering is not supported") && countLines(s.text(), "bestmove") == 0, "go ponder: explained and searching until stop");
            s.send("stop");
            s.done();
            // A "position" command ends a running search too.
            s.clear();
            s.send("go infinite");
            sleepMs(100);
            s.send("position startpos moves e2e4");
            CHECK(!s.engine.searching() && countLines(s.text(), "bestmove") == 1, "position during a search stops it and reports its move");
            s.send("go depth 1");
            s.done();
            CHECK(countLines(s.text(), "bestmove") == 2, "a search can be started after that");
            // stop with nothing running
            s.clear();
            s.send("stop");
            s.send("isready");
            CHECK(!contains(s.text(), "bestmove") && contains(s.text(), "readyok"), "stop without a search is harmless");
            // quit while searching
            s.send("go infinite");
            sleepMs(100);
            const auto t0 = std::chrono::steady_clock::now();
            const bool keep = s.engine.handleLine("quit");
            const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            CHECK(!keep, "quit returns false");
            CHECK(ms < 500, "quit stops a running search quickly (" + std::to_string(ms) + " ms)");
            CHECK(!s.engine.searching(), "not searching after quit");
        }
    }
#if !MATEX_THREADS
    {  // Without threads "go infinite" cannot be stopped, so it is limited instead of hanging.
        Session s;
        const std::string out = s.run("go infinite");
        CHECK(contains(out, "no thread support") && countLines(out, "bestmove") == 1, "single-threaded build: go infinite is limited and answers");
    }
#endif
    {  // run(): end of input waits for a finite search, stops an infinite one
        SyncBuf buf;
        std::ostream os(&buf);
        UciEngine e(os);
        std::istringstream in("position startpos\ngo depth 5\n");
        e.run(in);
        CHECK(countLines(buf.snapshot(), "bestmove") == 1, "end of input: the finite search finished and printed its bestmove");
        if (UciEngine::threadsAvailable()) {
            SyncBuf buf2;
            std::ostream os2(&buf2);
            UciEngine e2(os2);
            std::istringstream in2("go infinite\n");
            e2.run(in2);
            CHECK(countLines(buf2.snapshot(), "bestmove") == 1, "end of input: an infinite search is stopped and reports its move");
        }
        SyncBuf buf3;
        std::ostream os3(&buf3);
        UciEngine e3(os3);
        std::istringstream in3("uci\nisready\nquit\nisready\n");
        e3.run(in3);
        CHECK(countLines(buf3.snapshot(), "readyok") == 1, "nothing is processed after quit");
    }
    {  // ucinewgame empties the transposition table: the same search then visits exactly the same nodes again.
        Session s;
        auto lastNodes = [](const std::string& t) {
            uint64_t n = 0;
            for (const auto& l : linesOf(t)) if (startsWith(l, "info depth")) n = std::strtoull(splitTokens(l)[7].c_str(), nullptr, 10);
            return n;
        };
        const uint64_t first = lastNodes(s.run("go depth 6"));
        const uint64_t warm = lastNodes(s.run("go depth 6"));
        s.send("ucinewgame");
        const uint64_t cold = lastNodes(s.run("go depth 6"));
        CHECK(warm != first, "a second identical search reuses the table (" + std::to_string(warm) + " vs " + std::to_string(first) + " nodes)");
        CHECK(cold == first, "after ucinewgame the search is identical to the first one (" + std::to_string(cold) + " vs " + std::to_string(first) + ")");
    }
    {  // The game history reaches the search: a move that repeats an earlier position is avoided.
        const std::string root = "position fen 4k3/8/8/8/8/8/8/R3K3 w - - 0 1";
        Session s;
        s.send(root);
        CHECK(bestmoveOf(s.run("go depth 5")) == "a1a7", "K+R vs K without history: the rook goes to a7");
        s.send(root + " moves a1a7 e8d8 a7a1 d8e8 a1a7 e8d8 a7a1 d8e8");
        CHECK(s.engine.history().size() == 8, "eight moves of history");
        const std::string with = bestmoveOf(s.run("go depth 5"));
        CHECK(with != "a1a7" && with != "", "with the history the engine avoids repeating a1a7 (played " + with + ")");
    }
    {  // the engine never plays an illegal move over a whole game against itself
        Session s;
        Position p = fromFEN(START_FEN);
        std::string moves;
        bool ok = true;
        for (int ply = 0; ply < 60 && ok; ++ply) {
            MoveList ml;
            generateLegalMoves(p, ml);
            if (ml.size() == 0) break;
            s.send("position startpos" + std::string(moves.empty() ? "" : " moves" + moves));
            const std::string out = s.run("go depth 2");
            const std::string m = bestmoveOf(out);
            if (!playUci(p, m)) { ok = false; CHECK(false, "self-play produced an illegal move " + m + " at ply " + std::to_string(ply)); break; }
            moves += " " + m;
        }
        CHECK(ok, "60 plies of self-play, all moves legal");
    }
}

static std::string engineText(Session& s, const std::string& line) { s.clear(); s.send(line); return s.text(); }

static void testOptions() {
    std::cout << "options\n";
    Session s;
    const std::string uciOut = engineText(s, "uci");
    CHECK(contains(uciOut, "id name MateX") && contains(uciOut, "id author") && contains(uciOut, "uciok"), "uci handshake");
    CHECK(uciOut.rfind("uciok") > uciOut.find("option name Hash"), "uciok comes after the options");
    for (const char* name : {"Hash", "Clear Hash", "Move Overhead", "Techniques", "EvalFeatures", "Debug Log File"})
        CHECK(contains(uciOut, std::string("option name ") + name + " type"), std::string("option advertised: ") + name);
    CHECK(contains(engineText(s, "isready"), "readyok"), "isready");

    CHECK(s.engine.hashMB() == 16, "default hash 16 MB");
    s.send("setoption name Hash value 64");
    CHECK(s.engine.hashMB() == 64, "Hash 64");
    s.send("setoption name hash value 1");
    CHECK(s.engine.hashMB() == 1, "option names are case-insensitive");
    for (const char* bad : {"setoption name Hash value 0", "setoption name Hash value 5000", "setoption name Hash value abc", "setoption name Hash", "setoption name Hash value -4"}) {
        const std::string out = engineText(s, bad);
        CHECK(s.engine.hashMB() == 1 && contains(out, "info string"), std::string("bad Hash value rejected: ") + bad);
    }
    s.send("setoption name Hash value 16");
    CHECK(contains(s.run("go depth 4"), "bestmove"), "search works after resizing the table");
    s.send("setoption name Clear Hash");
    CHECK(!contains(s.text(), "unknown"), "Clear Hash is accepted without a value");

    s.send("setoption name Move Overhead value 100");
    CHECK(s.engine.moveOverhead() == 100, "Move Overhead 100");
    engineText(s, "setoption name Move Overhead value 6000");
    CHECK(s.engine.moveOverhead() == 100, "Move Overhead above the maximum is rejected");
    engineText(s, "setoption name Move Overhead value 0");
    CHECK(s.engine.moveOverhead() == 0, "Move Overhead 0 is allowed");

    const unsigned oldEval = evalFlags();
    s.send("setoption name EvalFeatures value none");
    CHECK(evalFlags() == EVAL_BASIC, "EvalFeatures none");
    s.send("setoption name EvalFeatures value taper,passed");
    CHECK(evalFlags() == (EVAL_TAPER | EVAL_PASSED), "EvalFeatures taper,passed");
    CHECK(contains(engineText(s, "options"), "taper,passed"), "options shows the evaluation features");
    const std::string badEval = engineText(s, "setoption name EvalFeatures value nonsense");
    CHECK(evalFlags() == (EVAL_TAPER | EVAL_PASSED) && contains(badEval, "info string"), "an unknown evaluation feature is rejected and nothing changes");
    s.send("setoption name EvalFeatures value default");
    CHECK(evalFlags() == evalDefault(), "EvalFeatures default");
    CHECK(oldEval == evalDefault(), "a new engine starts with the recommended evaluation");

    s.send("setoption name Techniques value none");
    CHECK(contains(engineText(s, "options"), "Techniques") || contains(s.text(), "search techniques"), "options lists the techniques");
    const std::string badT = engineText(s, "setoption name Techniques value pvs,bogus");
    CHECK(contains(badT, "info string"), "unknown technique rejected");
    s.send("setoption name Techniques value default");
    const std::string unk = engineText(s, "setoption name Banana value 3");
    CHECK(contains(unk, "unknown option"), "unknown option is reported");
    CHECK(contains(engineText(s, "setoption"), "usage"), "setoption without arguments prints usage");
    CHECK(contains(engineText(s, "setoption value 3"), "usage"), "setoption without 'name' prints usage");

    // The settings really change the search: with all techniques off the node count differs from the default set.
    s.send("position startpos");
    s.send("setoption name Techniques value none");
    const std::string off = s.run("go depth 5");
    s.send("setoption name Techniques value default");
    s.send("setoption name Clear Hash");
    const std::string on = s.run("go depth 5");
    auto lastNodes = [](const std::string& t) {
        uint64_t n = 0;
        for (const auto& l : linesOf(t)) if (startsWith(l, "info depth")) n = std::strtoull(splitTokens(l)[7].c_str(), nullptr, 10);
        return n;
    };
    CHECK(lastNodes(off) > lastNodes(on), "default techniques search fewer nodes than none (" + std::to_string(lastNodes(on)) + " < " + std::to_string(lastNodes(off)) + ")");

    // Debug Log File
    const std::string path = "matex_uci_test.log";
    std::remove(path.c_str());
    s.send("setoption name Debug Log File value " + path);
    s.send("isready");
    s.send("position startpos moves e2e4");
    s.send("setoption name Debug Log File value <empty>");
    s.send("isready");  // not logged
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string log = ss.str();
    CHECK(contains(log, ">> isready") && contains(log, "<< readyok") && contains(log, ">> position startpos moves e2e4"), "the log records input (>>) and output (<<)");
    CHECK(countLines(log, ">> isready") == 1, "logging stops when the option is cleared");
    std::remove(path.c_str());
    s.send("setoption name Debug Log File value /nonexistent_dir_xyz/a.log");
    CHECK(contains(s.text(), "could not open"), "an unwritable log file is reported");
    s.send("setoption name EvalFeatures value default");
}

static void testDebugCommands() {
    std::cout << "debug commands\n";
    Session s;
    auto nodesOf = [](const std::string& t) {
        for (const auto& l : linesOf(t)) if (startsWith(l, "Nodes searched: ")) return std::strtoull(l.c_str() + 16, nullptr, 10);
        return static_cast<unsigned long long>(0);
    };
    CHECK(nodesOf(engineText(s, "perft 1")) == 20, "perft 1 = 20");
    CHECK(nodesOf(engineText(s, "perft 2")) == 400, "perft 2 = 400");
    CHECK(nodesOf(engineText(s, "perft 3")) == 8902, "perft 3 = 8902");
    CHECK(nodesOf(engineText(s, "perft 4")) == 197281, "perft 4 = 197281");
    const std::string dv = engineText(s, "divide 2");
    CHECK(nodesOf(dv) == 400 && contains(dv, "e2e4: 20") && contains(dv, "g1f3: 20"), "divide 2 lists every root move with 20 replies");
    CHECK(countLines(dv, "a2a3: ") == 1, "divide lists each move once");
    CHECK(nodesOf(engineText(s, "go perft 3")) == 8902, "go perft 3");
    s.send("position fen " + std::string(KIWIPETE));
    CHECK(nodesOf(engineText(s, "perft 3")) == 97862, "kiwipete perft 3 = 97862");
    s.send("position startpos moves e2e4 d7d5 e4d5");
    CHECK(nodesOf(engineText(s, "perft 2")) == nodesOf(engineText(s, "divide 2")), "perft and divide agree after moves");
    for (const char* bad : {"perft", "perft 0", "perft x", "divide -3", "divide"})
        CHECK(contains(engineText(s, bad), "usage"), std::string("usage message for: ") + bad);

    s.send("position startpos moves e2e4 e7e5");
    const std::string d = engineText(s, "d");
    CHECK(contains(d, "Fen: rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2"), "d prints the FEN");
    CHECK(contains(d, "Key: 0x") && contains(d, "Legal moves: 29") && contains(d, "In check: no"), "d prints key, legal-move count and check status");
    s.send("position fen 4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    CHECK(contains(engineText(s, "d"), "In check: yes"), "d shows check");
    const std::string ev = engineText(s, "eval");
    CHECK(contains(ev, "material") && contains(ev, "total (White)") && contains(ev, "features"), "eval prints the terms");
    s.send("position startpos");
    CHECK(contains(engineText(s, "eval"), "side to move 0 cp"), "the start position evaluates to 0");
    CHECK(contains(engineText(s, "debug on"), "") && s.engine.debug(), "debug on");
    CHECK(contains(engineText(s, "position startpos moves e2e4"), "position set"), "debug mode reports position changes");
    s.send("debug off");
    CHECK(!s.engine.debug() && !contains(engineText(s, "position startpos"), "position set"), "debug off");
    CHECK(contains(engineText(s, "debug maybe"), "usage"), "debug with a bad argument prints usage");

    // bench: deterministic, one line per position, a signature total.
    const std::string b1 = engineText(s, "bench 3");
    const std::string b2 = engineText(s, "bench 3");
    CHECK(countLines(b1, "position ") == static_cast<int>(benchFens().size()), "bench prints one line per position");
    CHECK(nodesOf(b1) > 0 && nodesOf(b1) == nodesOf(b2), "bench is deterministic: " + std::to_string(nodesOf(b1)));
    uint64_t sum = 0;
    for (const auto& l : linesOf(b1)) {
        const size_t at = l.find(" nodes ");
        if (startsWith(l, "position ") && at != std::string::npos) sum += std::strtoull(l.c_str() + at + 7, nullptr, 10);
    }
    CHECK(sum == nodesOf(b1), "the bench total is the sum of the positions");
    CHECK(contains(b1, "Nodes/second"), "bench prints the speed");
    for (const char* bad : {"bench 0", "bench 99", "bench x"}) CHECK(contains(engineText(s, bad), "usage"), std::string("usage for: ") + bad);
    // The signature is pinned: a change in the search or the evaluation changes it on purpose (update it together with the notes).
    s.send("setoption name Techniques value default");
    s.send("setoption name EvalFeatures value default");
    CHECK(nodesOf(engineText(s, "bench 4")) == 22145ULL, "bench 4 signature (default techniques and evaluation): " + std::to_string(nodesOf(s.text())));
    CHECK(contains(engineText(s, "options"), "hash 16 MB"), "options prints the configuration");
    CHECK(contains(engineText(s, "frobnicate now"), "unknown command"), "an unknown command is reported");
    CHECK(engineText(s, "").empty() && engineText(s, "   \t ").empty(), "empty lines are ignored");
}

static void testRobustness() {
    std::cout << "robustness\n";
    Session s;
    Rng rng{987654321};
    const std::vector<std::string> pool = {"position", "startpos", "fen", "moves", "e2e4", "e7e8q", "a1a1", "0000", "setoption", "name", "value", "Hash", "-1",
                                           "999999999999999999999", "depth", "nodes", "movetime", "wtime", "btime", "stop", "ucinewgame", "d", "eval",
                                           "isready", "uci", "debug", "on", "off", "////", "\x01\x02", "\xff\xfe", "w", "KQkq", "-", "8/8/8/8/8/8/8/8",
                                           "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR", "0", "1", "Move", "Overhead", "Techniques", "EvalFeatures", "all", "none"};
    int lines = 0;
    for (int i = 0; i < 3000; ++i) {
        std::string line;
        const size_t n = rng.below(9);
        for (size_t k = 0; k < n; ++k) line += (k ? " " : "") + pool[rng.below(pool.size())];
        // never start a real search from random text: no 'go', 'perft', 'divide' or 'bench' as the first word
        const auto t = splitTokens(line);
        if (!t.empty() && (t[0] == "go" || t[0] == "perft" || t[0] == "divide" || t[0] == "bench" || t[0] == "quit")) continue;
        s.send(line);
        ++lines;
    }
    CHECK(lines > 2500, "3000 random lines were sent");
    s.send("setoption name Hash value 16");
    s.send("setoption name Move Overhead value 30");
    s.send("setoption name EvalFeatures value default");
    s.send("setoption name Techniques value default");
    s.send("position startpos");
    s.clear();
    s.send("isready");
    CHECK(s.text() == "readyok\n", "the engine still answers isready after random input");
    // Mutated real commands.
    const std::vector<std::string> real = {"position startpos moves e2e4 e7e5 g1f3", "position fen " + std::string(KIWIPETE) + " moves e1g1",
                                           "setoption name Hash value 8", "go depth 1", "go nodes 50", "go movetime 5", "go wtime 100 btime 100"};
    for (int i = 0; i < 1500; ++i) {
        std::string line = real[rng.below(real.size())];
        const size_t edits = 1 + rng.below(3);
        for (size_t k = 0; k < edits && !line.empty(); ++k) {
            const size_t at = rng.below(line.size());
            switch (rng.below(3)) {
                case 0: line.erase(at, 1 + rng.below(3)); break;
                case 1: line.insert(at, 1, static_cast<char>(32 + rng.below(95))); break;
                default: line[at] = static_cast<char>(32 + rng.below(95)); break;
            }
        }
        {
            const auto t = splitTokens(line);
            if (!t.empty() && (t[0] == "go" || t[0] == "perft" || t[0] == "divide" || t[0] == "bench" || t[0] == "quit")) {
                if (t[0] != "go") continue;
                line += " depth 1 nodes 200";  // keep it short whatever the mutation did
            }
        }
        s.send(line);
        s.done();
    }
    s.send("setoption name Hash value 16");
    s.send("position startpos");
    s.clear();
    s.send("isready");
    CHECK(s.text() == "readyok\n", "the engine still answers isready after mutated commands");
    // Bounded go lines with random junk.
    for (int i = 0; i < 200; ++i) {
        std::string line = "go depth 1 nodes 100";
        const size_t n = rng.below(4);
        for (size_t k = 0; k < n; ++k) line += " " + pool[rng.below(pool.size())];
        if (contains(line, "infinite") || contains(line, "perft") ) continue;
        s.clear();
        s.send(line);
        s.done();
        CHECK(countLines(s.text(), "bestmove") == 1, "junk after a bounded go still ends in one bestmove: " + line);
    }
    // Very long line, binary data.
    s.send(std::string(100000, 'x'));
    s.send("position startpos moves" + std::string(5000, ' ') + "e2e4");
    s.send(std::string("\0\0\0", 3));
    s.clear();
    s.send("isready");
    CHECK(s.text() == "readyok\n", "long and binary lines are survived");
}

int main(int argc, char** argv) {
    initAttackTables();
    std::set<std::string> want;
    for (int i = 1; i < argc; ++i) want.insert(argv[i]);
    auto on = [&](const char* n) { return want.empty() || want.count(n) > 0; };
    std::cout << "threads: " << (UciEngine::threadsAvailable() ? "yes" : "no (fallback)") << "\n";
    if (on("go")) testGoParsing();
    if (on("time")) testTimeAllocation();
    if (on("position")) testPosition();
    if (on("search")) testSearch();
    if (on("options")) testOptions();
    if (on("debug")) testDebugCommands();
    if (on("robust")) testRobustness();
    std::cout << (testsRun - testsFailed) << "/" << testsRun << " checks passed" << (testsFailed == 0 ? ". ALL TESTS PASSED" : ". SOME TESTS FAILED") << "\n";
    return testsFailed == 0 ? 0 : 1;
}