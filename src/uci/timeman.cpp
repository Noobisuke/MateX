#include "timeman.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace {
bool toInt(const std::string& s, int64_t& v) {
    if (s.empty() || s.size() > 18) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); ++k) if (s[k] < '0' || s[k] > '9') return false;
    v = std::strtoll(s.c_str(), nullptr, 10);
    return true;
}
}  // namespace

bool parseGo(const std::vector<std::string>& t, GoParams& out, std::string* error) {
    out = GoParams{};
    bool ok = true;
    auto fail = [&](const std::string& msg) {
        ok = false;
        if (error && error->empty()) *error = msg;
    };
    for (size_t i = 0; i < t.size(); ++i) {
        const std::string& w = t[i];
        if (w == "infinite") { out.infinite = true; continue; }
        if (w == "ponder") { out.ponder = true; out.infinite = true; continue; }
        if (w == "searchmoves") {  // the move list that follows is skipped (it runs to the end of the line or the next keyword)
            out.searchmoves = true;
            while (i + 1 < t.size() && t[i + 1].size() >= 4 && t[i + 1].size() <= 5 && std::isalpha(static_cast<unsigned char>(t[i + 1][0])) &&
                   std::isdigit(static_cast<unsigned char>(t[i + 1][1])))
                ++i;
            continue;
        }
        const bool numeric = w == "depth" || w == "nodes" || w == "movetime" || w == "wtime" || w == "btime" || w == "winc" ||
                             w == "binc" || w == "movestogo" || w == "mate" || w == "perft";
        if (!numeric) { fail("unknown go parameter: " + w); continue; }
        int64_t v = 0;
        if (i + 1 >= t.size() || !toInt(t[i + 1], v)) { fail("missing or bad number after " + w); continue; }
        ++i;
        const int64_t big = std::numeric_limits<int>::max();
        if (w == "depth") out.depth = static_cast<int>(std::clamp<int64_t>(v, 0, big));
        else if (w == "nodes") out.nodes = v > 0 ? static_cast<uint64_t>(v) : 0;
        else if (w == "movetime") out.movetime = std::max<int64_t>(v, 0);
        else if (w == "wtime") out.wtime = v;  // may be negative (a flagged player): treated as "almost no time" below
        else if (w == "btime") out.btime = v;
        else if (w == "winc") out.winc = std::max<int64_t>(v, 0);
        else if (w == "binc") out.binc = std::max<int64_t>(v, 0);
        else if (w == "movestogo") out.movestogo = static_cast<int>(std::clamp<int64_t>(v, 0, big));
        else if (w == "mate") out.mate = static_cast<int>(std::clamp<int64_t>(v, 0, big));
        else if (w == "perft") out.perft = static_cast<int>(std::clamp<int64_t>(v, 0, 64));
    }
    return ok;
}

int64_t allocateTime(const GoParams& go, Color stm, int64_t overhead) {
    overhead = std::max<int64_t>(overhead, 0);
    if (go.movetime > 0) return std::max<int64_t>(go.movetime - overhead, 1);
    const int64_t remaining = stm == WHITE ? go.wtime : go.btime;
    const int64_t inc = stm == WHITE ? go.winc : go.binc;
    if (remaining == -1 && (stm == WHITE ? go.btime : go.wtime) == -1) return 0;  // no clock at all
    if (remaining < 0 && remaining != -1) return 1;                               // flagged
    if (remaining == -1) return 0;                                                // only the opponent's clock was given
    const int movesLeft = go.movestogo > 0 ? std::min(go.movestogo, 50) : 30;
    const int64_t avail = std::max<int64_t>(remaining - overhead, 1);
    const int64_t base = remaining / movesLeft + inc * 3 / 4;
    return std::max<int64_t>(std::min(base, avail * 4 / 5), 1);
}