#pragma once
// Stage 11: plain negamax. Stage 12: alpha-beta. Stage 13: quiescence search at depth 0.
// Earlier variants are kept as references that later ones must agree with (see the list at the bottom).
//
// Scores are centipawns from the side to move's point of view.
// Mate scores:  being checkmated at ply p (root = ply 0) scores  -(MATE_SCORE - p);
//               delivering mate at ply p scores                   +(MATE_SCORE - p).
// So a larger |score| means a FASTER mate, and mate scores never collide with eval scores.

#include "../board/position.hpp"
#include "../move/move.hpp"
#include "tt.hpp"
#include <cstdint>
#include <atomic>
#include <functional>
#include <vector>

constexpr int MATE_SCORE = 30000;
constexpr int INFINITE_SCORE = 32000;
constexpr int MAX_SEARCH_DEPTH = 64;
constexpr int MAX_PLY = 128;  // main search plus quiescence never goes deeper than this
constexpr int MATE_BOUND = MATE_SCORE - MAX_PLY;  // |score| >= this is a mate score

inline bool isMateScore(int score) { return score >= MATE_BOUND || score <= -MATE_BOUND; }
// Full moves until mate: +n means the side to move mates in n, -n means it is mated in n. 0 if not a mate score.
int mateInMoves(int score);

struct SearchResult {
    bool hasMove = false;         // false if the root position is already terminal (or depth 0)
    Move bestMove{};
    int score = 0;
    int depth = 0;                // depth of the (last completed) search
    uint64_t nodes = 0;           // every negamax call, leaves included
    bool interrupted = false;     // iterative deepening only: stopped by a limit before reaching maxDepth
    double timeMs = 0;            // iterative deepening only
    std::vector<Move> pv;         // principal variation, pv[0] == bestMove
};

// Move-ordering techniques for the alpha-beta search (Stage 14). Combine with |.
enum OrderingFlags : unsigned {
    ORDER_NONE = 0,
    ORDER_CAPTURES = 1,   // captures and promotions first, MVV-LVA
    ORDER_KILLERS = 2,    // killer moves (two per ply)
    ORDER_HISTORY = 4,    // history heuristic for the remaining quiet moves
    ORDER_DEFAULT = ORDER_CAPTURES | ORDER_KILLERS | ORDER_HISTORY,
};

// Search to a fixed depth (plies). Depth is clamped to [0, MAX_SEARCH_DEPTH].
// `pos` is modified during the search and restored before returning.
//
// Four variants, each the reference for the next:
SearchResult searchMinimax(Position& pos, int depth);            // Stage 11: plain negamax, static eval at depth 0
SearchResult searchAlphaBeta(Position& pos, int depth);          // Stage 12: + alpha-beta pruning
SearchResult searchMinimaxQuiescence(Position& pos, int depth);  // Stage 13 reference: plain negamax + unpruned quiescence
SearchResult searchOrdered(Position& pos, int depth, unsigned flags);  // Stage 14: alpha-beta + quiescence + chosen ordering
SearchResult search(Position& pos, int depth);                   // engine default: alpha-beta + quiescence + ORDER_DEFAULT

// ---- Stage 16: transposition table ---------------------------------------------------------------------------
// How a table entry may cut off a node.
enum TTMode {
    TT_DEPTH_GE = 0,     // standard: an entry searched to at least the requested depth may be used (stronger, but
                         // the score can differ from a plain fixed-depth search because deeper results leak in)
    TT_EXACT_DEPTH = 1,  // test mode: only entries of EXACTLY the requested depth are used. The table then cannot
                         // change the score: it must equal the search without a table. Nodes still drop.
    TT_ORDER_ONLY = 2,   // the table only supplies the hash move for ordering, no cutoffs (never changes the score;
                         // measures how much of the saving comes from ordering alone)
};

// Alpha-beta + quiescence + ORDER_DEFAULT (or `ordering`) with a transposition table and the hash move first.
// The table is NOT cleared: entries from earlier searches stay usable (newSearch() is called to age them).
// The root is never cut off, so a best move is always returned. The PV is extended from the table where a
// cutoff truncated it.
SearchResult searchTT(Position& pos, int depth, TranspositionTable& tt, TTMode mode = TT_DEPTH_GE,
                      unsigned ordering = ORDER_DEFAULT);

// ---- Stage 15: iterative deepening and time management ---------------------------------------------------------
struct SearchLimits {
    int maxDepth = MAX_SEARCH_DEPTH;
    int64_t movetimeMs = 0;                  // 0 = no time limit
    uint64_t maxNodes = 0;                   // 0 = no node limit (counted across all iterations)
    const std::atomic<bool>* stop = nullptr; // set to true from anywhere to interrupt
    TranspositionTable* tt = nullptr;        // Stage 16: optional table, shared across calls (nullptr = none)
    TTMode ttMode = TT_DEPTH_GE;
};

struct SearchInfo {
    int depth;
    int score;
    uint64_t nodes;
    double timeMs;
    std::vector<Move> pv;
};
using InfoCallback = std::function<void(const SearchInfo&)>;

// Searches depth 1, 2, 3, ... until a limit is hit. Always returns the result of the last COMPLETED iteration, so
// the move is legal and fully searched even when interrupted. If even depth 1 cannot finish (very tight limit), a
// legal fallback move is returned with depth 0 and the static evaluation. `result.depth` is the last completed depth, `result.interrupted` says whether a limit stopped it early (not set when a mate was found).
// `info` (optional) is called after every completed iteration. `pos` is restored before returning.
SearchResult searchIterative(Position& pos, const SearchLimits& limits, const InfoCallback& info = nullptr);

// Simple clock allocation: time for this move from the remaining time, the increment and moves to go (0 = unknown).
int64_t allocateTimeMs(int64_t timeLeftMs, int64_t incrementMs, int movesToGo = 0);