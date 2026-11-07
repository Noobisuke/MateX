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
#include <cstdint>
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
    int depth = 0;
    uint64_t nodes = 0;           // every negamax call, leaves included
    std::vector<Move> pv;         // principal variation, pv[0] == bestMove
};

// Search to a fixed depth (plies). Depth is clamped to [0, MAX_SEARCH_DEPTH].
// `pos` is modified during the search and restored before returning.
//
// Four variants, each the reference for the next:
SearchResult searchMinimax(Position& pos, int depth);            // Stage 11: plain negamax, static eval at depth 0
SearchResult searchAlphaBeta(Position& pos, int depth);          // Stage 12: + alpha-beta pruning
SearchResult searchMinimaxQuiescence(Position& pos, int depth);  // Stage 13 reference: plain negamax + unpruned quiescence
SearchResult search(Position& pos, int depth);                   // Stage 13: alpha-beta + quiescence (engine default)