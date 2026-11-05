// Interactive move-playing tool, for manually checking the engine rules.
//
// Plays legal moves with the real makeMove/unmakeMove (Stage 7); `undo` calls unmakeMove.
//
// Commands:  e2e4 | e7e8q (UCI move)   moves   undo   fen <FEN>   new   help   quit

#include "../src/board/position.hpp"
#include "../src/board/fen.hpp"
#include "../src/board/attacks.hpp"
#include "../src/move/legal.hpp"
#include "../src/move/makemove.hpp"
#include "../src/rules/special.hpp"
#include "../src/rules/validate.hpp"
#include "../src/rules/gamestate.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void show(const Position& pos, const GameHistory& history) {
    std::cout << "\n" << pos.toString();
    std::cout << "FEN: " << toFEN(pos) << "\n";
    const GameStatus status = evaluateGameStatus(pos, history);
    const bool white = pos.sideToMove() == WHITE;

    switch (status.result) {
        case GameResult::CHECKMATE:
            std::cout << "CHECKMATE. " << (white ? "Black" : "White") << " wins.\n";
            return;
        case GameResult::ONGOING: {
            MoveList legal;
            generateLegalMoves(pos, legal);
            std::cout << (white ? "White" : "Black") << " to move" << (status.inCheck ? " (IN CHECK)" : "")
                      << ", " << legal.size() << " legal moves.\n";
            if (status.fiftyMoveClaimable) std::cout << "A draw can be CLAIMED: fifty-move rule.\n";
            if (status.threefoldClaimable) std::cout << "A draw can be CLAIMED: threefold repetition.\n";
            return;
        }
        default:
            std::cout << "GAME OVER, " << toString(status.result) << ".\n";
    }
}

static void listMoves(const Position& pos) {
    MoveList legal;
    generateLegalMoves(pos, legal);
    for (size_t i = 0; i < legal.size(); ++i) std::cout << legal[i].toString() << ' ';
    std::cout << "\n";
}

int main(int argc, char** argv) {
    initAttackTables();
    struct Played { Move move; UndoInfo undo; };
    Position pos;
    pos.setStartingPosition();
    std::vector<Played> played;
    GameHistory history;

    if (argc > 1) {
        std::string fen = argv[1];
        for (int i = 2; i < argc; ++i) { fen += ' '; fen += argv[i]; }
        std::string err;
        if (!loadFEN(pos, fen, &err) || !validatePosition(pos, &err)) { std::cout << "Bad FEN: " << err << "\n"; return 1; }
    }
    history.reset(pos);

    std::cout << "Type a move like e2e4 (promotion: e7e8q). 'help' for commands.\n";
    show(pos, history);

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd;
        if (!(in >> cmd)) continue;

        if (cmd == "quit" || cmd == "exit") break;
        if (cmd == "help") {
            std::cout << "  <move>      play a legal move in UCI form (e2e4, e1g1, e7e8q)\n"
                         "  moves       list legal moves\n"
                         "  undo        take back the last move\n"
                         "  fen <FEN>   load a position\n"
                         "  new         start position\n"
                         "  quit\n";
        } else if (cmd == "moves") {
            listMoves(pos);
        } else if (cmd == "undo") {
            if (!played.empty()) {
                unmakeMove(pos, played.back().move, played.back().undo);
                played.pop_back();
                history.pop();
            } else {
                std::cout << "Nothing to undo.\n";
            }
            show(pos, history);
        } else if (cmd == "new") {
            pos.setStartingPosition();
            played.clear();
            history.reset(pos);
            show(pos, history);
        } else if (cmd == "fen") {
            std::string rest;
            std::getline(in, rest);
            Position p;
            std::string err;
            if (loadFEN(p, rest, &err) && validatePosition(p, &err)) { pos = p; played.clear(); history.reset(pos); show(pos, history); }
            else std::cout << "Bad FEN: " << err << "\n";
        } else {
            MoveList legal;
            generateLegalMoves(pos, legal);
            const Move* found = nullptr;
            for (size_t i = 0; i < legal.size(); ++i)
                if (legal[i].toString() == cmd) { found = &legal[i]; break; }
            if (!found) {
                std::cout << "Illegal or unknown move '" << cmd << "'. Type 'moves' to list legal moves.\n";
                continue;
            }
            const Move move = *found;
            played.push_back({move, makeMove(pos, move)});
            history.push(pos);
            if (!pos.isConsistent()) std::cout << "INTERNAL ERROR: inconsistent position!\n";
            show(pos, history);
        }
    }
    return 0;
}