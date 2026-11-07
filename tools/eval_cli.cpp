// eval_cli: reads FENs (one per line) from stdin, prints the evaluation terms.
// Output per line: material pst pawns bishopPair mobility kingSafety total endgame
// With "--stm" the total is printed from the side to move's view.
#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/eval/evaluate.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    initAttackTables();
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        Position pos;
        std::string err;
        if (!loadFEN(pos, line, &err)) { std::cout << "ERR " << err << "\n"; continue; }
        const EvalTerms t = evaluateTerms(pos);
        std::cout << t.material << ' ' << t.pst << ' ' << t.pawns << ' ' << t.bishopPair << ' '
                  << t.mobility << ' ' << t.kingSafety << ' ' << t.total() << ' ' << (isEndgame(pos) ? 1 : 0)
                  << ' ' << evaluate(pos) << "\n";
    }
    (void)argc; (void)argv;
    return 0;
}