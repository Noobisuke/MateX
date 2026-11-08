// hash_cli: reads one FEN per line from stdin and prints "<16 hex digits> <base key hex>" for each.
// Used for batch cross-checks of the Zobrist key against an independent implementation (python-chess).
#include "../src/board/attacks.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/zobrist.hpp"
#include <cstdio>
#include <iostream>
#include <string>

int main() {
    initAttackTables();
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Position pos;
        std::string err;
        if (!loadFEN(pos, line, &err)) { std::cout << "ERROR " << err << "\n"; continue; }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%016llx %016llx", static_cast<unsigned long long>(hashKey(pos)),
                      static_cast<unsigned long long>(pos.baseKey()));
        std::cout << buf << "\n";
    }
    return 0;
}
