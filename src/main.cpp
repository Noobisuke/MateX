#include "board/position.hpp"
#include "board/fen.hpp"
#include "board/attacks.hpp"
#include "move/movegen.hpp"
#include <iostream>

int main(int argc, char** argv) {
    initAttackTables();

    Position pos;

    if (argc > 1) {
        // Load an arbitrary FEN from the command line, e.g.:
        //   ./chess_engine "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"
        std::string fen = argv[1];
        for (int i = 2; i < argc; ++i) { fen += ' '; fen += argv[i]; }

        std::string err;
        if (!loadFEN(pos, fen, &err)) {
            std::cout << "Failed to load FEN: " << err << "\n";
            return 1;
        }
        std::cout << "=== Loaded Position ===\n\n";
    } else {
        pos.setStartingPosition();
        std::cout << "=== Starting Position ===\n\n";
    }

    std::cout << pos.toString();

    std::cout << "\nConsistency check: "
              << (pos.isConsistent() ? "OK" : "FAILED") << "\n";
    std::cout << "FEN: " << toFEN(pos) << "\n";

    std::cout << "White in check: " << (isInCheck(pos, WHITE) ? "yes" : "no") << "\n";
    std::cout << "Black in check: " << (isInCheck(pos, BLACK) ? "yes" : "no") << "\n";

    MoveList moves;
    generatePseudoLegalMoves(pos, moves);
    std::cout << "\nPseudo-legal moves (" << moves.size() << "):";
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i % 12 == 0) std::cout << "\n  ";
        std::cout << moves[i].toString() << ' ';
    }
    std::cout << "\n(pseudo-legal: may include moves that leave the king in check; Stage 5 filters those)\n";

    return 0;
}