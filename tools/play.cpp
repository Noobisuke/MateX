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
#include "../src/eval/evaluate.hpp"
#include "../src/board/zobrist.hpp"
#include "../src/search/search.hpp"
#include <iomanip>
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
    struct Played { Move move; UndoInfo undo; uint64_t keyBefore; };  // keyBefore: hash of the position before the move (repetition history)
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
    TranspositionTable tt(16);  // Stage 16: shared by every go/engine command, so later searches reuse earlier work
    setEvalFlags(evalDefault());  // Stage 18: the engine plays with the recommended evaluation ('evalopts' changes it)
    SearchOptions options = SearchOptions::engineDefault();  // Stage 17: search techniques ('options' shows or changes them)
    auto gameKeys = [&]() {     // positions played so far, oldest first: the search treats a repetition of one as a draw
        std::vector<uint64_t> keys;
        for (const Played& pl : played) keys.push_back(pl.keyBefore);
        return keys;
    };

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
                         "  eval        show the static evaluation terms\n"
                         "  go [d]      search to depth d (default 3) and show best move, score, PV\n"
                         "  go time <ms>        think for <ms> milliseconds (iterative deepening)\n"
                         "  engine [d] | engine time <ms>   let the engine play its best move\n"
                         "  hash        show the Zobrist key of the position (full key and incremental part)\n"
                         "  hashfull    transposition-table fill and hit statistics   clearhash   empty the table\n"
                         "  options [list]   show or set the search techniques: none, default, all, or e.g. pvs,null,lmr\n"
                         "                   (names: pvs asp rfp null lmr fut raz ext sing seeord seeq rep; '-x' removes one)\n"
                         "  evalopts [list]  show or set the evaluation features: none, default, all, or e.g. taper,passed\n"
                         "                   (names: taper mobility passed pawns outpost rook kingzone threats space; '-x' removes one)\n"
                         "  undo        take back the last move\n"
                         "  fen <FEN>   load a position\n"
                         "  new         start position\n"
                         "  quit\n";
        } else if (cmd == "eval") {
            const EvalTerms t = evaluateTerms(pos);
            std::cout << "material " << t.material << "  pst " << t.pst << "  pawns " << t.pawns
                      << "  bishopPair " << t.bishopPair << "  mobility " << t.mobility
                      << "  kingSafety " << t.kingSafety << "  outposts " << t.outposts << "  rooks " << t.rooks
                      << "  threats " << t.threats << "  space " << t.space << "  (White minus Black)\n"
                      << "total (White) " << t.total() << ", side to move " << evaluate(pos)
                      << " cp, phase " << gamePhase(pos) << "/24" << (isEndgame(pos) ? " (basic rule: endgame)" : " (basic rule: middlegame)") << "\n";
        } else if (cmd == "go" || cmd == "engine") {
            // go [depth]        fixed depth (default 3)       go time <ms>   iterative deepening for <ms> milliseconds
            std::string arg;
            in >> arg;
            SearchResult r;
            if (arg == "time") {
                long long ms = 0;
                if (!(in >> ms) || ms < 1 || ms > 600000) { std::cout << "Usage: " << cmd << " time <1-600000 milliseconds>\n"; continue; }
                SearchLimits limits;
                limits.movetimeMs = ms;
                limits.tt = &tt;
                limits.options = options;
                limits.history = gameKeys();
                r = searchIterative(pos, limits, [](const SearchInfo& i) {
                    std::cout << "  info depth " << i.depth << " score " << i.score << " nodes " << i.nodes
                              << " time " << static_cast<long>(i.timeMs) << "ms pv";
                    for (const Move& m : i.pv) std::cout << ' ' << m.toString();
                    std::cout << "\n";
                });
            } else {
                int depth = 3;  // an empty or invalid argument keeps the default
                if (!arg.empty()) {
                    try { depth = std::stoi(arg); } catch (...) { depth = 0; }
                }
                if (depth < 1 || depth > 10) { std::cout << "Depth must be 1-10, or use: " << cmd << " time <ms>\n"; continue; }
                r = searchOpt(pos, depth, options, &tt, TT_DEPTH_GE, gameKeys());
            }
            if (!r.hasMove) { std::cout << "No legal move (game over).\n"; continue; }
            std::cout << "best " << r.bestMove.toString() << "  depth " << r.depth << "  score " << r.score;
            if (isMateScore(r.score)) std::cout << "  (mate in " << mateInMoves(r.score) << ")";
            std::cout << "  nodes " << r.nodes << "  pv";
            for (const Move& m : r.pv) std::cout << ' ' << m.toString();
            std::cout << "\n";
            if (cmd == "engine") {
                const uint64_t keyBefore = hashKey(pos);
                played.push_back({r.bestMove, makeMove(pos, r.bestMove), keyBefore});
                history.push(pos);
                show(pos, history);
            }
        } else if (cmd == "options") {
            std::string rest;
            std::getline(in, rest);
            const size_t first = rest.find_first_not_of(" \t");
            if (first == std::string::npos) {
                std::cout << "search techniques: " << options.toString() << "\n";
            } else {
                bool ok = false;
                const SearchOptions parsed = SearchOptions::fromString(rest.substr(first), &ok);
                if (!ok) std::cout << "Unknown technique name. Names: pvs asp rfp null lmr fut raz ext sing seeord seeq rep (or none, default, all).\n";
                else { options = parsed; std::cout << "search techniques: " << options.toString() << "\n"; }
            }
        } else if (cmd == "evalopts") {
            std::string rest;
            std::getline(in, rest);
            const size_t first = rest.find_first_not_of(" \t");
            if (first != std::string::npos) {
                unsigned f = 0;
                if (!parseEvalFlags(rest.substr(first), f)) { std::cout << "Unknown evaluation feature. Names: taper mobility passed pawns outpost rook kingzone threats space (or none, default, all).\n"; continue; }
                setEvalFlags(f);
            }
            std::cout << "evaluation features: " << evalFlagsToString(evalFlags()) << "\n";
        } else if (cmd == "hash") {
            std::cout << std::hex << std::setfill('0') << "key " << std::setw(16) << hashKey(pos) << "  (placement+side+castling part "
                      << std::setw(16) << pos.baseKey() << ")" << std::dec << std::setfill(' ') << "\n";
        } else if (cmd == "hashfull") {
            std::cout << "table: " << tt.entryCount() << " entries, " << tt.hashfull() << " permille full, probes " << tt.probes()
                      << ", hits " << tt.hits() << ", stores " << tt.stores() << "\n";
        } else if (cmd == "clearhash") {
            tt.clear();
            std::cout << "Transposition table cleared.\n";
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
            const uint64_t keyBefore = hashKey(pos);
            played.push_back({move, makeMove(pos, move), keyBefore});
            history.push(pos);
            if (!pos.isConsistent()) std::cout << "INTERNAL ERROR: inconsistent position!\n";
            show(pos, history);
        }
    }
    return 0;
}