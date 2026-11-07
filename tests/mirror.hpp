#pragma once
// Shared FEN transforms for symmetry tests.
#include <string>
#include <vector>

inline std::vector<std::string> fenSplit(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == sep) { out.push_back(s.substr(start, i - start)); start = i + 1; }
    return out;
}

inline char swapCaseChar(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Flip ranks, swap colours, swap side to move, castling case, ep rank.
inline std::string mirrorFEN(const std::string& fen) {
    const auto f = fenSplit(fen, ' ');
    const auto ranks = fenSplit(f[0], '/');
    std::string placement;
    for (int i = static_cast<int>(ranks.size()) - 1; i >= 0; --i) {
        for (char c : ranks[i]) placement += swapCaseChar(c);
        if (i > 0) placement += '/';
    }
    std::string castling = "-";
    if (f[2] != "-") { castling.clear(); for (char c : f[2]) castling += swapCaseChar(c); }
    std::string ep = "-";
    if (f[3] != "-") ep = std::string(1, f[3][0]) + (f[3][1] == '3' ? '6' : '3');
    return placement + " " + (f[1] == "w" ? "b" : "w") + " " + castling + " " + ep + " " + f[4] + " " + f[5];
}

// Flip files (a<->h). Castling and en passant are dropped (evaluation ignores them).
inline std::string flipFilesFEN(const std::string& fen) {
    const auto f = fenSplit(fen, ' ');
    const auto ranks = fenSplit(f[0], '/');
    std::string placement;
    for (size_t i = 0; i < ranks.size(); ++i) {
        placement.append(ranks[i].rbegin(), ranks[i].rend());
        if (i + 1 < ranks.size()) placement += '/';
    }
    return placement + " " + f[1] + " - - " + f[4] + " " + f[5];
}