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
#include <string>
#include <vector>

constexpr int MATE_SCORE = 30000;
constexpr int INFINITE_SCORE = 32000;
constexpr int MAX_SEARCH_DEPTH = 64;
constexpr int MAX_PLY = 128;  // main search plus quiescence never goes deeper than this
constexpr int MATE_BOUND = MATE_SCORE - MAX_PLY;  // |score| >= this is a mate score

inline bool isMateScore(int score) { return score >= MATE_BOUND || score <= -MATE_BOUND; }
// Full moves until mate: +n means the side to move mates in n, -n means it is mated in n. 0 if not a mate score.
int mateInMoves(int score);

// ---- Stage 17: advanced search techniques ---------------------------------------------------------------------
// Every technique is a flag, all OFF by default: with a default-constructed SearchOptions the search is exactly the
// Stage 16 search (same scores AND same node counts, pinned by tests). That makes every technique measurable on its
// own. Score-preserving ones (never change the root score): pvs, aspiration, seeOrdering. Heuristics (change the
// tree on purpose): all the others.
struct SearchOptions {
    bool pvs = false;              // principal variation search: zero-window searches for every move after the first
    bool aspiration = false;       // iterative deepening searches a narrow window around the previous score
    bool reverseFutility = false;  // "static eval is so far above beta that the node will fail high"
    bool nullMove = false;         // "pass" and see whether the opponent still cannot get below beta
    bool lmr = false;              // late move reductions (needs the zero-window searches, which it switches on)
    bool futility = false;         // skip quiet moves near the horizon that cannot reach alpha
    bool razoring = false;         // near the horizon, hopeless nodes go straight to quiescence
    bool checkExtension = false;   // a node in check is searched one ply deeper
    bool singular = false;         // extend the table move when every alternative is clearly worse
    bool seeOrdering = false;      // captures that lose material by SEE are tried after the quiet moves
    bool seeQuiescence = false;    // quiescence skips captures that lose material by SEE
    bool repetition = false;       // a position repeating one on the current path (or in the game) scores 0

    // Parameters (centipawns / plies). Defaults are the values the tests and benchmarks use.
    int aspirationDelta = 25;
    int aspirationMinDepth = 4;
    int rfpMargin = 90;            // per remaining ply
    int rfpMaxDepth = 6;
    int nullMinDepth = 3;
    int nullBase = 2;              // reduction R = nullBase + depth / nullDivisor
    int nullDivisor = 4;
    int lmrMinDepth = 3;
    int lmrMoveThreshold = 3;      // the first 3 moves are never reduced
    int futilityMargin = 120;      // per remaining ply
    int futilityMaxDepth = 3;
    int razorMargin = 300;         // depth 1; plus razorStep per additional ply
    int razorStep = 200;
    int razorMaxDepth = 2;
    int singularMinDepth = 6;
    int singularMargin = 2;        // singular beta = table score - singularMargin * depth
    int extensionPlyLimit = 96;    // no extension once ply + depth reaches this (capped at 96: the ply-indexed arrays are sized for it)

    static SearchOptions none() { return SearchOptions{}; }
    static SearchOptions all();            // every technique on
    static SearchOptions engineDefault();  // the set kept after the Stage 17 measurements
    // "none", "all", "default", or a list like "pvs,null,lmr"; a leading '-' removes a flag ("all,-sing").
    // Names: pvs asp rfp null lmr fut raz ext sing seeord seeq rep. Sets *ok = false on an unknown name.
    static SearchOptions fromString(const std::string& text, bool* ok = nullptr);
    std::string toString() const;
};

// How often each technique fired during one search (proof that it is active, and what it did).
struct SearchStats {
    uint64_t ttCutoffs = 0;
    uint64_t rfpCutoffs = 0;
    uint64_t razorCutoffs = 0;
    uint64_t nullTries = 0, nullCutoffs = 0;
    uint64_t lmrReductions = 0, lmrResearches = 0;
    uint64_t futilityPrunes = 0;
    uint64_t pvsResearches = 0;
    uint64_t aspirationResearches = 0;
    uint64_t checkExtensions = 0;
    uint64_t singularTests = 0, singularExtensions = 0;
    uint64_t seeQuiescencePrunes = 0;
    uint64_t repetitionDraws = 0;
    int maxPly = 0;                // deepest main-search ply reached (check extensions can chain)
};

struct SearchResult {
    bool hasMove = false;         // false if the root position is already terminal (or depth 0)
    Move bestMove{};
    int score = 0;
    int depth = 0;                // depth of the (last completed) search
    uint64_t nodes = 0;           // every negamax call, leaves included
    bool interrupted = false;     // iterative deepening only: stopped by a limit before reaching maxDepth
    double timeMs = 0;            // iterative deepening only
    std::vector<Move> pv;         // principal variation, pv[0] == bestMove
    SearchStats stats;            // Stage 17
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

// Fixed-depth search with any combination of Stage 17 techniques (and optionally a table). `history` lists the keys
// (hashKey) of the positions played before `pos`, oldest first, for repetition detection.
// (Aspiration windows only exist in searchIterative: a fixed-depth search has no previous score.)
SearchResult searchOpt(Position& pos, int depth, const SearchOptions& options, TranspositionTable* tt = nullptr,
                       TTMode mode = TT_DEPTH_GE, const std::vector<uint64_t>& history = {});

// ---- Stage 15: iterative deepening and time management ---------------------------------------------------------
struct SearchLimits {
    int maxDepth = MAX_SEARCH_DEPTH;
    int64_t movetimeMs = 0;                  // 0 = no time limit
    uint64_t maxNodes = 0;                   // 0 = no node limit (counted across all iterations)
    const std::atomic<bool>* stop = nullptr; // set to true from anywhere to interrupt
    TranspositionTable* tt = nullptr;        // Stage 16: optional table, shared across calls (nullptr = none)
    TTMode ttMode = TT_DEPTH_GE;
    SearchOptions options;                   // Stage 17: advanced techniques (default: all off = Stage 16 behaviour)
    std::vector<uint64_t> history;           // keys of the positions before the root, oldest first (repetition)
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