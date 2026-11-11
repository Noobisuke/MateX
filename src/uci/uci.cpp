#include "uci.hpp"
#include "../board/attacks.hpp"
#include "../board/fen.hpp"
#include "../board/zobrist.hpp"
#include "../eval/evaluate.hpp"
#include "../move/legal.hpp"
#include "../move/makemove.hpp"
#include "../perft/perft.hpp"
#include "../rules/validate.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>

namespace {

struct Guard {  // minimal lock_guard that also works with the no-thread mutex
    MateXMutex& m;
    explicit Guard(MateXMutex& mm) : m(mm) { m.lock(); }
    ~Guard() { m.unlock(); }
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string join(const std::vector<std::string>& t, size_t from, size_t to) {
    std::string s;
    for (size_t i = from; i < to && i < t.size(); ++i) {
        if (!s.empty()) s += ' ';
        s += t[i];
    }
    return s;
}

bool parseIntStrict(const std::string& s, long long& v) {
    if (s.empty() || s.size() > 12) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); ++k) if (!std::isdigit(static_cast<unsigned char>(s[k]))) return false;
    v = std::stoll(s);
    return true;
}

std::string scoreText(int score) {
    if (isMateScore(score)) return "mate " + std::to_string(mateInMoves(score));
    return "cp " + std::to_string(score);
}

std::string keyHex(uint64_t k) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%016llx", static_cast<unsigned long long>(k));
    return buf;
}

}  // namespace

std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

const std::vector<std::string>& benchFens() {
    static const std::vector<std::string> fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 w - - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1",
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
    };
    return fens;
}

bool UciEngine::threadsAvailable() { return MATEX_THREADS != 0; }

UciEngine::UciEngine(std::ostream& out) : out_(out), tt_(16) {
    initAttackTables();
    pos_.setStartingPosition();
    searchOptions_ = SearchOptions::engineDefault();
    setEvalFlags(evalDefault());  // the library default is the basic evaluation; the engine plays with the recommended one
}

UciEngine::~UciEngine() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}

void UciEngine::emit(const std::string& line) {
    Guard g(outMutex_);
    out_ << line << '\n' << std::flush;
    if (log_.is_open()) log_ << "<< " << line << '\n' << std::flush;
}

void UciEngine::stopAndJoin() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    stop_ = false;
}

void UciEngine::waitForSearch() {
    if (worker_.joinable()) worker_.join();
}

bool UciEngine::handleLine(const std::string& raw) {
    {
        Guard g(outMutex_);
        if (log_.is_open()) log_ << ">> " << raw << '\n' << std::flush;
    }
    const std::vector<std::string> tok = splitTokens(raw);
    if (tok.empty()) return true;
    const std::string cmd = tok[0];
    const std::vector<std::string> args(tok.begin() + 1, tok.end());

    try {
        if (cmd == "quit") { stopAndJoin(); return false; }
        if (cmd == "stop") { stop_ = true; return true; }
        if (cmd == "isready") { emit("readyok"); return true; }
        if (cmd == "uci") { cmdUci(); return true; }
        if (cmd == "ucinewgame") { cmdNewGame(); return true; }
        if (cmd == "position") { cmdPosition(args); return true; }
        if (cmd == "go") { cmdGo(args); return true; }
        if (cmd == "setoption") { cmdSetOption(args); return true; }
        if (cmd == "debug") {
            if (!args.empty() && args[0] == "on") debug_ = true;
            else if (!args.empty() && args[0] == "off") debug_ = false;
            else info("usage: debug on|off");
            return true;
        }
        if (cmd == "d") { cmdDisplay(); return true; }
        if (cmd == "eval") { cmdEval(); return true; }
        if (cmd == "perft" || cmd == "divide") {
            long long n = 0;
            if (args.empty() || !parseIntStrict(args[0], n) || n < 1) info("usage: " + cmd + " <depth >= 1>");
            else cmdPerft(static_cast<int>(std::min<long long>(n, 10)), cmd == "divide");
            return true;
        }
        if (cmd == "bench") { cmdBench(args); return true; }
        if (cmd == "options") {
            emit("search techniques: " + searchOptions_.toString() + "; evaluation features: " + evalFlagsToString(evalFlags()) +
                 "; hash " + std::to_string(hashMB_) + " MB; move overhead " + std::to_string(moveOverhead_) + " ms; threads " +
                 (threadsAvailable() ? "yes" : "no"));
            return true;
        }
        info("unknown command: " + cmd);
    } catch (const std::exception& e) {  // never let one bad line kill the engine
        info(std::string("error handling '") + cmd + "': " + e.what());
    }
    return true;
}

void UciEngine::run(std::istream& in) {
    std::string line;
    while (std::getline(in, line))
        if (!handleLine(line)) return;
    if (searching_.load() && infiniteSearch_.load()) stop_ = true;  // end of input: a finite search may finish, an infinite one must stop
    waitForSearch();
}

void UciEngine::cmdUci() {
    emit(std::string("id name ") + MATEX_NAME + " " + MATEX_VERSION);
    emit("id author MateX contributors");
    emit("option name Hash type spin default 16 min 1 max 4096");
    emit("option name Clear Hash type button");
    emit("option name Move Overhead type spin default 30 min 0 max 5000");
    emit("option name Techniques type string default default");
    emit("option name EvalFeatures type string default default");
    emit("option name Debug Log File type string default <empty>");
    emit("uciok");
}

void UciEngine::cmdNewGame() {
    stopAndJoin();
    tt_.clear();
    pos_.setStartingPosition();
    history_.clear();
}

void UciEngine::cmdPosition(const std::vector<std::string>& t) {
    if (t.empty()) { info("usage: position startpos|fen <FEN> [moves <m1> <m2> ...]"); return; }
    Position np;
    std::vector<uint64_t> nh;
    size_t i = 0;
    std::string err;
    if (t[0] == "startpos") {
        np.setStartingPosition();
        i = 1;
    } else if (t[0] == "fen") {
        size_t end = 1;
        while (end < t.size() && t[end] != "moves") ++end;
        const std::string fen = join(t, 1, end);
        if (!loadFEN(np, fen, &err)) { info("bad FEN (position not changed): " + err); return; }
        if (!validatePosition(np, &err)) { info("invalid position (position not changed): " + err); return; }
        i = end;
    } else {
        info("position: expected 'startpos' or 'fen', got '" + t[0] + "' (position not changed)");
        return;
    }
    if (i < t.size()) {
        if (t[i] != "moves") { info("position: unexpected '" + t[i] + "' (position not changed)"); return; }
        for (size_t k = i + 1; k < t.size(); ++k) {
            MoveList ml;
            generateLegalMoves(np, ml);
            const std::string want = lower(t[k]);
            bool found = false;
            for (size_t m = 0; m < ml.size(); ++m) {
                if (ml[m].toString() == want) {
                    nh.push_back(hashKey(np));
                    makeMove(np, ml[m]);
                    found = true;
                    break;
                }
            }
            if (!found) { info("illegal move '" + t[k] + "' (position not changed)"); return; }
        }
    }
    stopAndJoin();  // a position change ends a running search
    pos_ = np;
    history_ = std::move(nh);
    if (debug_) info("position set: " + toFEN(pos_) + ", " + std::to_string(history_.size()) + " earlier positions");
}

void UciEngine::cmdGo(const std::vector<std::string>& t) {
    GoParams go;
    std::string err;
    if (!parseGo(t, go, &err)) info("go: " + err);
    stopAndJoin();  // a new search replaces a running one (its bestmove is written first)

    if (go.perft > 0) { cmdPerft(go.perft, true); return; }
    if (go.searchmoves) info("searchmoves is not supported: searching all moves");
    if (go.ponder) info("pondering is not supported: searching until 'stop'");
    if (go.mate > 0 && go.depth == 0) go.depth = std::min(MAX_SEARCH_DEPTH, 2 * go.mate);  // mate in N moves = 2N-1 plies

    int64_t timeMs = allocateTime(go, pos_.sideToMove(), moveOverhead_);
    const bool anyClock = go.wtime >= 0 || go.btime >= 0;
    if (timeMs == 0 && !go.infinite && go.depth == 0 && go.nodes == 0 && anyClock) timeMs = 1000;  // only the opponent's clock was given
    if (timeMs == 0 && go.depth == 0 && go.nodes == 0) go.infinite = true;                         // a bare "go" means infinite

    if (!threadsAvailable() && go.infinite) {
        info("no thread support: 'go infinite' is limited to " + std::to_string(kNoThreadInfiniteMs) + " ms");
        go.infinite = false;
        timeMs = kNoThreadInfiniteMs;
    }
    if (debug_) info("go: time " + std::to_string(timeMs) + " ms, depth " + std::to_string(go.depth) + ", nodes " + std::to_string(go.nodes) +
                     (go.infinite ? ", infinite" : ""));

    stop_ = false;
    searching_ = true;
    infiniteSearch_ = go.infinite;
    Position p = pos_;
    std::vector<uint64_t> h = history_;
    const SearchOptions opts = searchOptions_;
#if MATEX_THREADS
    worker_ = MateXThread([this, go, p, h, opts, timeMs]() mutable { searchTask(go, p, h, opts, timeMs); });
#else
    searchTask(go, p, h, opts, timeMs);
#endif
}

void UciEngine::searchTask(GoParams go, Position pos, std::vector<uint64_t> history, SearchOptions opts, int64_t timeMs) {
    SearchLimits lim;
    lim.maxDepth = go.depth > 0 ? std::min(go.depth, MAX_SEARCH_DEPTH) : MAX_SEARCH_DEPTH;
    lim.movetimeMs = timeMs;
    lim.maxNodes = go.nodes;
    lim.stop = &stop_;
    lim.tt = &tt_;
    lim.options = opts;
    lim.history = std::move(history);

    SearchResult r;
    try {
        r = searchIterative(pos, lim, [this](const SearchInfo& i) {
            const long long ms = static_cast<long long>(i.timeMs);
            const unsigned long long nps = ms > 0 ? static_cast<unsigned long long>(i.nodes) * 1000ULL / static_cast<unsigned long long>(ms) : 0ULL;
            std::string line = "info depth " + std::to_string(i.depth) + " score " + scoreText(i.score) + " nodes " + std::to_string(i.nodes) +
                               " nps " + std::to_string(nps) + " time " + std::to_string(ms) + " hashfull " + std::to_string(tt_.hashfull());
            if (!i.pv.empty()) {
                line += " pv";
                for (const Move& m : i.pv) line += " " + m.toString();
            }
            emit(line);
        });
    } catch (const std::exception& e) {
        info(std::string("search failed: ") + e.what());
    }

    // "go infinite" must not report a move until the GUI says "stop", even if the search ended by itself (found mate, depth cap).
#if MATEX_THREADS
    if (go.infinite)
        while (!stop_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    emit(r.hasMove ? "bestmove " + r.bestMove.toString() : std::string("bestmove 0000"));
    searching_ = false;
}

void UciEngine::cmdSetOption(const std::vector<std::string>& t) {
    size_t i = 0;
    if (t.empty() || t[0] != "name") { info("usage: setoption name <id> [value <x>]"); return; }
    size_t valueAt = t.size();
    for (size_t k = 1; k < t.size(); ++k) if (t[k] == "value") { valueAt = k; break; }
    const std::string name = lower(join(t, 1, valueAt));
    const std::string value = valueAt < t.size() ? join(t, valueAt + 1, t.size()) : std::string();
    (void)i;

    auto spin = [&](long long lo, long long hi, long long& out) {
        long long v = 0;
        if (!parseIntStrict(value, v) || v < lo || v > hi) {
            info("option '" + name + "' needs a number from " + std::to_string(lo) + " to " + std::to_string(hi) + " (unchanged)");
            return false;
        }
        out = v;
        return true;
    };

    if (name == "hash") {
        long long mb;
        if (!spin(1, 4096, mb)) return;
        stopAndJoin();
        try {
            tt_.resize(static_cast<size_t>(mb));
            hashMB_ = static_cast<int>(mb);
        } catch (const std::exception& e) {
            info(std::string("could not allocate the hash table: ") + e.what());
            tt_.resize(1);
            hashMB_ = 1;
        }
    } else if (name == "clear hash") {
        stopAndJoin();
        tt_.clear();
    } else if (name == "move overhead") {
        long long v;
        if (!spin(0, 5000, v)) return;
        moveOverhead_ = static_cast<int>(v);
    } else if (name == "techniques") {
        bool ok = false;
        const SearchOptions o = SearchOptions::fromString(value, &ok);
        if (!ok) { info("unknown technique name in '" + value + "' (unchanged)"); return; }
        searchOptions_ = o;
    } else if (name == "evalfeatures") {
        unsigned f = 0;
        if (!parseEvalFlags(value, f)) { info("unknown evaluation feature in '" + value + "' (unchanged)"); return; }
        stopAndJoin();  // the evaluation setting is process-wide: never change it under a running search
        setEvalFlags(f);
    } else if (name == "debug log file") {
        Guard g(outMutex_);
        if (log_.is_open()) log_.close();
        logPath_.clear();
        if (!value.empty() && value != "<empty>") {
            log_.open(value, std::ios::app);
            if (log_.is_open()) logPath_ = value;
            else out_ << "info string could not open the log file '" << value << "'" << std::endl;
        }
    } else {
        info("unknown option: " + name);
    }
}

void UciEngine::cmdDisplay() {
    MoveList ml;
    generateLegalMoves(pos_, ml);
    std::string s = pos_.toString();
    s += "Fen: " + toFEN(pos_) + "\n";
    s += "Key: " + keyHex(hashKey(pos_)) + "\n";
    s += std::string("In check: ") + (isInCheck(pos_, pos_.sideToMove()) ? "yes" : "no") + "\n";
    s += "Legal moves: " + std::to_string(ml.size()) + "\n";
    s += "Static evaluation (side to move): " + std::to_string(evaluate(pos_)) + " cp";
    emit(s);
}

void UciEngine::cmdEval() {
    const EvalTerms e = evaluateTerms(pos_);
    emit("material " + std::to_string(e.material) + "  pst " + std::to_string(e.pst) + "  pawns " + std::to_string(e.pawns) + "  bishopPair " +
         std::to_string(e.bishopPair) + "  mobility " + std::to_string(e.mobility) + "  kingSafety " + std::to_string(e.kingSafety) +
         "  outposts " + std::to_string(e.outposts) + "  rooks " + std::to_string(e.rooks) + "  threats " + std::to_string(e.threats) +
         "  space " + std::to_string(e.space) + "  (White minus Black)\n" + "total (White) " + std::to_string(e.total()) + ", side to move " +
         std::to_string(evaluate(pos_)) + " cp, features " + evalFlagsToString(evalFlags()));
}

void UciEngine::cmdPerft(int depth, bool divideOutput) {
    Position p = pos_;
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t total = 0;
    std::string text;
    if (divideOutput) {
        for (const DivideEntry& e : divide(p, depth)) {
            text += e.move.toString() + ": " + std::to_string(e.nodes) + "\n";
            total += e.nodes;
        }
    } else {
        total = perftBulk(p, depth);
    }
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    text += "Nodes searched: " + std::to_string(total) + "\nTime (ms): " + std::to_string(ms);
    emit(text);
}

void UciEngine::cmdBench(const std::vector<std::string>& t) {
    long long depth = 5;
    if (!t.empty() && (!parseIntStrict(t[0], depth) || depth < 1 || depth > 20)) { info("usage: bench [depth 1-20]"); return; }
    stopAndJoin();
    uint64_t nodes = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const auto& fens = benchFens();
    for (size_t k = 0; k < fens.size(); ++k) {
        Position p;
        std::string err;
        if (!loadFEN(p, fens[k], &err)) { info("bench position " + std::to_string(k + 1) + " failed: " + err); continue; }
        TranspositionTable table(16);  // a fresh table per position: the node counts do not depend on the order
        SearchLimits lim;
        lim.maxDepth = static_cast<int>(depth);
        lim.tt = &table;
        lim.options = searchOptions_;
        const SearchResult r = searchIterative(p, lim);
        nodes += r.nodes;
        emit("position " + std::to_string(k + 1) + "/" + std::to_string(fens.size()) + ": depth " + std::to_string(r.depth) + " score " +
             scoreText(r.score) + " nodes " + std::to_string(r.nodes) + " best " + (r.hasMove ? r.bestMove.toString() : std::string("0000")));
    }
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    emit("Nodes searched: " + std::to_string(nodes) + "\nTime (ms): " + std::to_string(ms) + "\nNodes/second: " +
         std::to_string(ms > 0 ? nodes * 1000ULL / static_cast<uint64_t>(ms) : 0ULL));
}