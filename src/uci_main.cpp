// matex: the UCI engine executable.
//
//   matex                 speak UCI on stdin/stdout (what a chess GUI runs)
//   matex bench [depth]   run the fixed benchmark and exit (prints "Nodes searched")
//   matex --version
#include "uci/uci.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    UciEngine engine(std::cout);
    if (argc > 1) {
        const std::string a = argv[1];
        if (a == "--version" || a == "-v") { std::cout << MATEX_NAME << " " << MATEX_VERSION << "\n"; return 0; }
        std::string line;
        for (int i = 1; i < argc; ++i) line += std::string(i > 1 ? " " : "") + argv[i];
        engine.handleLine(line);
        engine.waitForSearch();
        return 0;
    }
    engine.run(std::cin);
    return 0;
}