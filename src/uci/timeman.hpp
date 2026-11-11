#pragma once
// Stage 19: the parameters of a UCI "go" command and the time-allocation rule.
//
// Pure functions only (no engine state), so the rule can be tested exactly.
#include "../board/types.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct GoParams {
    int depth = 0;                 // go depth N        (0 = not given)
    uint64_t nodes = 0;            // go nodes N        (0 = not given)
    int64_t movetime = 0;          // go movetime MS    (0 = not given)
    int64_t wtime = -1, btime = -1;  // remaining clock in ms (-1 = not given)
    int64_t winc = 0, binc = 0;    // increment per move in ms
    int movestogo = 0;             // moves until the next time control (0 = sudden death)
    int mate = 0;                  // go mate N: accepted, searched like "go depth 2N" (see Uci)
    int perft = 0;                 // go perft N (debugging extension, as in other engines)
    bool infinite = false;         // go infinite (also go ponder: this engine does not ponder, it searches infinitely)
    bool ponder = false;
    bool searchmoves = false;      // searchmoves was given: not supported, the list is ignored
    bool hasClock(Color side) const { return (side == WHITE ? wtime : btime) >= 0; }
};

// Parses the tokens after "go". Unknown tokens and bad numbers are reported in `error` (the rest still parses);
// returns false if anything was wrong.
bool parseGo(const std::vector<std::string>& tokens, GoParams& out, std::string* error = nullptr);

// How many milliseconds to think about the next move; 0 means "no time limit" (depth / nodes / infinite).
//   movetime given:      movetime - overhead (at least 1)
//   clock given:         remaining/movesLeft + 3/4 of the increment, where movesLeft = movestogo (at most 50) or 30;
//                        never more than 80% of (remaining - overhead), at least 1 ms
// `overhead` is the safety margin for GUI/pipe latency (the "Move Overhead" option).
int64_t allocateTime(const GoParams& go, Color sideToMove, int64_t overheadMs);