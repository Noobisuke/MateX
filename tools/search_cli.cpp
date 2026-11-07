// search_cli: for each stdin line "<depth> <FEN>", print "score nodes bestmove" (bestmove "-" if none).
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/search/search.hpp"
#include <iostream>
#include <sstream>
#include <string>

int main() {
    initAttackTables();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int depth = 0;
        if (!(in >> depth)) continue;
        std::string fen;
        std::getline(in, fen);
        Position pos;
        std::string err;
        if (!loadFEN(pos, fen, &err)) { std::cout << "ERR " << err << "\n"; continue; }
        const SearchResult r = search(pos, depth);
        std::cout << r.score << ' ' << r.nodes << ' ' << (r.hasMove ? r.bestMove.toString() : "-") << "\n";
    }
    return 0;
}