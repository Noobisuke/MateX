// tactics: run an EPD test suite (positions with "bm" best moves) at a fixed node, depth or time budget.
//   tactics suite.epd [--nodes N] [--depth D] [--ms T] [--opts "pvs,null,..."] [--tt MB] [--verbose] [--emit]
// --emit prints "SOLVED <id> | <fen> | <uci move>" for every solved position (used to build regression sets).
// A position counts as solved when the engine's move (in SAN) is one of the bm moves. The suite itself is not part
// of the repository (the classic "Win At Chess" file is widely available); the tool works with any EPD.
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/move/legal.hpp"
#include "../src/search/search.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string squareName(int sq) { return std::string(1, static_cast<char>('a' + sq % 8)) + static_cast<char>('1' + sq / 8); }

std::string strip(const std::string& s) {
    std::string o;
    for (char c : s) if (c != '+' && c != '#' && c != '!' && c != '?' && c != '\r' && c != '\n') o.push_back(c);
    return o;
}

// Standard algebraic notation of a legal move (without check marks).
std::string toSAN(const Position& pos, const Move& m) {
    if (m.flag == CASTLE_KINGSIDE) return "O-O";
    if (m.flag == CASTLE_QUEENSIDE) return "O-O-O";
    const PieceType pt = typeOf(m.piece);
    std::string s;
    if (pt == PAWN) {
        if (m.isCapture()) s += static_cast<char>('a' + m.from % 8), s += 'x';
        s += squareName(m.to);
        if (m.isPromotion()) {
            s += '=';
            s += "NBRQ"[m.promotion - KNIGHT];
        }
        return s;
    }
    s += "PNBRQK"[pt];
    MoveList legal;
    generateLegalMoves(pos, legal);
    bool ambiguous = false, sameFile = false, sameRank = false;
    for (size_t i = 0; i < legal.size(); ++i) {
        const Move& o = legal[i];
        if (o.from == m.from || o.to != m.to || o.piece != m.piece) continue;
        ambiguous = true;
        if (o.from % 8 == m.from % 8) sameFile = true;
        if (o.from / 8 == m.from / 8) sameRank = true;
    }
    if (ambiguous) {
        if (!sameFile) s += static_cast<char>('a' + m.from % 8);
        else if (!sameRank) s += static_cast<char>('1' + m.from / 8);
        else s += squareName(m.from);
    }
    if (m.isCapture()) s += 'x';
    s += squareName(m.to);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    initAttackTables();
    if (argc < 2) { std::cerr << "usage: tactics file.epd [--nodes N] [--depth D] [--ms T] [--opts ...] [--tt MB] [--verbose]\n"; return 1; }
    uint64_t nodes = 0;
    int depth = 64, ttMb = 16;
    int64_t ms = 0;
    bool verbose = false, emit = false;
    SearchOptions opts;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--nodes") nodes = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--depth") depth = std::atoi(next().c_str());
        else if (a == "--ms") ms = std::atoll(next().c_str());
        else if (a == "--tt") ttMb = std::atoi(next().c_str());
        else if (a == "--opts") { bool ok; opts = SearchOptions::fromString(next(), &ok); if (!ok) { std::cerr << "unknown option name\n"; return 1; } }
        else if (a == "--verbose") verbose = true;
        else if (a == "--emit") emit = true;
    }
    if (nodes == 0 && ms == 0 && depth >= 64) nodes = 100000;

    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
    TranspositionTable tt(static_cast<size_t>(ttMb));
    std::string line;
    int total = 0, solved = 0;
    uint64_t totalNodes = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::getline(in, line)) {
        if (line.size() < 20) continue;
        std::istringstream ss(line);
        std::string board, side, castle, ep;
        ss >> board >> side >> castle >> ep;
        const std::string& rest = line;  // the opcodes are found by name, so the whole line can be searched
        const size_t bm = rest.find("bm ");
        if (bm == std::string::npos) continue;
        const size_t end = rest.find(';', bm);
        std::istringstream moves(rest.substr(bm + 3, end - bm - 3));
        std::vector<std::string> best;
        for (std::string m; moves >> m;) best.push_back(strip(m));
        std::string id = "?";
        const size_t idp = rest.find("id \"");
        if (idp != std::string::npos) id = rest.substr(idp + 4, rest.find('"', idp + 4) - idp - 4);

        Position pos;
        if (!loadFEN(pos, board + " " + side + " " + castle + " " + ep + " 0 1", nullptr)) continue;
        tt.clear();
        SearchLimits lim;
        lim.maxDepth = depth;
        lim.maxNodes = nodes;
        lim.movetimeMs = ms;
        lim.tt = &tt;
        lim.options = opts;
        const SearchResult r = searchIterative(pos, lim);
        ++total;
        totalNodes += r.nodes;
        const std::string san = r.hasMove ? strip(toSAN(pos, r.bestMove)) : "-";
        bool ok = false;
        for (const auto& b : best) if (b == san) ok = true;
        if (ok) {
            ++solved;
            if (emit) std::cout << "SOLVED " << id << " | " << toFEN(pos) << " | " << r.bestMove.toString() << "\n";
        }
        else if (verbose) std::cout << "  unsolved " << id << ": played " << san << ", wanted " << best[0] << " (depth " << r.depth << ", score " << r.score << ")\n";
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "TACTICS opts=[" << opts.toString() << "] solved " << solved << "/" << total << "  nodes " << totalNodes << "  " << secs << " s\n";
    return 0;
}