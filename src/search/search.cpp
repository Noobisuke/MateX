#include "search.hpp"
#include "../board/attacks.hpp"
#include "../board/zobrist.hpp"
#include "../eval/evaluate.hpp"
#include "see.hpp"
#include "../move/legal.hpp"
#include "../move/makemove.hpp"
#include "../rules/gamestate.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace {

constexpr int PAWN_VALUE_FOR_ORDER = 100;

struct PVLine {
    Move moves[MAX_PLY + 2];  // the main search never goes beyond SEARCH_PLY_LIMIT plies
    int length = 0;
};

struct Mode {
    bool alphaBeta;
    bool quiescence;
    unsigned ordering = 0;  // OrderingFlags (alpha-beta only)
};

// Per-search tables used by move ordering. Reset at the start of every search() call.
struct SearchState {
    Move killers[MAX_PLY + 1][2];
    int history[2][64][64] = {};
    TranspositionTable* tt = nullptr;  // Stage 16: nullptr = no table (exactly the Stage 14/15 behaviour)
    TTMode ttMode = TT_DEPTH_GE;       // which table entries may cut a node off (see search.hpp)
    SearchOptions opt;                 // Stage 17 techniques (all off = Stage 16 search)
    SearchStats stats;
    uint64_t keys[MAX_PLY + 2] = {};   // hashKey of the position at each ply of the current path (repetition)
    std::vector<uint64_t> gameKeys;    // positions before the root, oldest first
    int nullPly = -1;                  // ply of the innermost null move on the current path, -1 if none
};

// Limits checked while searching (iterative deepening only). Fixed-depth searches pass nullptr.
struct Control {
    uint64_t nodeLimit = 0;  // 0 = unlimited (counts all nodes of all iterations)
    bool hasDeadline = false;
    std::chrono::steady_clock::time_point deadline;
    const std::atomic<bool>* stop = nullptr;
    bool aborted = false;    // once set, every node returns immediately and the iteration is discarded
};

// Cheap checks every node, the clock and stop flag only every 2048 nodes.
bool checkAbort(Control& c, uint64_t nodes) {
    if (c.aborted) return true;
    if (c.nodeLimit != 0 && nodes >= c.nodeLimit) { c.aborted = true; return true; }
    if ((nodes & 2047) != 0) return false;
    if (c.stop && c.stop->load(std::memory_order_relaxed)) { c.aborted = true; return true; }
    if (c.hasDeadline && std::chrono::steady_clock::now() >= c.deadline) { c.aborted = true; return true; }
    return false;
}

constexpr int ORDER_BAD_CAPTURE = -1000000;  // captures that lose material by SEE: after every quiet move
constexpr int ORDER_HASH = 3000000;      // the transposition-table move: before everything else
constexpr int ORDER_PV = 2000000;        // previous iteration's best line first
constexpr int ORDER_TACTICAL = 1000000;  // captures and promotions: always before quiet moves
constexpr int ORDER_KILLER1 = 900000;
constexpr int ORDER_KILLER2 = 800000;
constexpr int HISTORY_MAX = 700000;      // history scores stay below the killer band

// Terminal or drawn position test that is independent of depth. Returns true and sets `score`.
bool terminalScore(const Position& pos, int ply, int& score) {
    if (!hasLegalMove(pos)) {
        score = isInCheck(pos, pos.sideToMove()) ? -(MATE_SCORE - ply) : 0;  // mated, or stalemate
        return true;
    }
    // Insufficient material is an automatic draw. The fifty-move rule is only a CLAIM (clock 100) until
    // the automatic seventy-five-move draw (150): the root with clock 100..149 must still pick a move, while
    // inside the tree a clock of 100 is scored as a draw (the engine assumes the side that benefits claims it).
    const int clockLimit = ply == 0 ? 150 : 100;
    if (isDeadPosition(pos) || pos.halfmoveClock() >= clockLimit) {
        score = 0;
        return true;
    }
    return false;
}

// Quiescence search: resolve captures and promotions until the position is "quiet", so the static
// evaluation is never taken in the middle of an exchange (the horizon effect).
//  - Not in check: the side to move may "stand pat" (decline every capture), so the static eval is a lower
//    bound on the score. Then try captures and promotions only.
//  - In check: standing pat would be illegal, so every legal move (all evasions) is searched; no legal
//    move means checkmate.
// Terminates: every capture removes a piece and every non-capturing promotion removes a pawn.
// Simplifications: no stalemate detection, and underpromotions are skipped (outside check evasions).
// Caller has already counted this node. `prune` selects alpha-beta (true) or plain negamax (the test reference).
// Known simplification: stalemate is not detected here (a quiet position with no legal move is just evaluated).
// Most valuable victim first, least valuable attacker as tie-break; promotions rank by promoted piece.
// Without this, quiescence tries bad captures first and the tree explodes. (Full move ordering is Stage 14.)
int captureOrderScore(const Move& m) {
    int s = 0;
    if (m.captured != NO_PIECE) s += 10 * pieceValue(typeOf(m.captured)) - pieceValue(typeOf(m.piece));
    else if (m.flag == EN_PASSANT) s += 10 * PAWN_VALUE_FOR_ORDER - PAWN_VALUE_FOR_ORDER;
    if (m.isPromotion()) s += pieceValue(m.promotion);
    return s;
}

// A capture whose victim is worth at least as much as the capturing piece cannot lose material on the first exchange,
// so SEE is only computed when the attacker is worth more than the victim (the cheap pre-filter).
bool losesByShortcut(const Move& m) {
    const int victim = m.flag == EN_PASSANT ? PAWN_VALUE_FOR_ORDER : (m.captured == NO_PIECE ? 0 : pieceValue(typeOf(m.captured)));
    return pieceValue(typeOf(m.piece)) > victim;
}

// Move ordering gives every move a priority; higher is searched first. It never changes the score, only the
// node count:
//   1. captures and promotions, most valuable victim first (MVV-LVA)    [ORDER_CAPTURES]
//   2. killer moves: quiet moves that caused a beta cutoff at this ply  [ORDER_KILLERS]
//   3. remaining quiet moves by history (how often they caused cutoffs) [ORDER_HISTORY]

int quiesce(Position& pos, int alpha, int beta, int ply, uint64_t& nodes, bool prune, Control* ctl, SearchState* st = nullptr) {
    if (ctl && checkAbort(*ctl, nodes)) return 0;
    if (isDeadPosition(pos)) return 0;  // e.g. the last rook was just captured: K+B v K is a draw, not +330
    if (ply >= MAX_PLY - 1) return evaluate(pos);

    const bool inCheck = isInCheck(pos, pos.sideToMove());
    MoveList moves;
    generateLegalMoves(pos, moves);
    if (inCheck && moves.size() == 0) return -(MATE_SCORE - ply);

    int best = -INFINITE_SCORE;
    if (!inCheck) {
        best = evaluate(pos);  // stand pat
        if (prune) {
            if (best >= beta) return best;
            alpha = std::max(alpha, best);
        }
    }

    // Candidate moves: captures and promotions, ordered best-first (all evasions when in check, captures first).
    Move cand[256];
    int score[256];
    int n = 0;
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move& m = moves[i];
        const bool tactical = m.isCapture() || m.isPromotion();
        if (!inCheck && !tactical) continue;
        // Only queen promotions are searched here; underpromotions are almost never best and would multiply the
        // tree by 4 per promoting pawn. (The main search still sees them. All evasions are searched when in check.)
        if (!inCheck && m.isPromotion() && m.promotion != QUEEN) continue;
        // SEE pruning (Stage 17): a capture that loses material on the exchange square is not worth resolving here.
        if (!inCheck && st && st->opt.seeQuiescence && m.isCapture() && !m.isPromotion() && losesByShortcut(m) && see(pos, m) < 0) {
            ++st->stats.seeQuiescencePrunes;
            continue;
        }
        const int sc = tactical ? captureOrderScore(m) : -1;
        int j = n++;
        while (j > 0 && score[j - 1] < sc) { cand[j] = cand[j - 1]; score[j] = score[j - 1]; --j; }
        cand[j] = m;
        score[j] = sc;
    }

    for (int k = 0; k < n; ++k) {
        const Move move = cand[k];
        const UndoInfo undo = makeMove(pos, move);
        ++nodes;
        const int value = -quiesce(pos, -beta, -alpha, ply + 1, nodes, prune, ctl, st);
        unmakeMove(pos, move, undo);
        if (ctl && ctl->aborted) return 0;

        if (value > best) {
            best = value;
            if (prune && value > alpha) {
                alpha = value;
                if (alpha >= beta) break;
            }
        }
    }
    return best;
}

int negamax(Position& pos, int depth, int ply, PVLine& pv, uint64_t& nodes, Mode mode) {
    ++nodes;
    pv.length = 0;

    int score = 0;
    if (terminalScore(pos, ply, score)) return score;
    if (depth == 0) return mode.quiescence ? quiesce(pos, -INFINITE_SCORE, INFINITE_SCORE, ply, nodes, false, nullptr)
                                           : evaluate(pos);

    MoveList moves;
    generateLegalMoves(pos, moves);

    int best = -INFINITE_SCORE;
    PVLine childPV;
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move move = moves[i];
        const UndoInfo undo = makeMove(pos, move);
        const int value = -negamax(pos, depth - 1, ply + 1, childPV, nodes, mode);
        unmakeMove(pos, move, undo);

        if (value > best) {  // strict: ties keep the earliest move, so results are deterministic
            best = value;
            pv.moves[0] = move;
            std::copy(childPV.moves, childPV.moves + childPV.length, pv.moves + 1);
            pv.length = childPV.length + 1;
        }
    }
    return best;
}

void scoreMoves(const Position& pos, const MoveList& moves, int* scores, const SearchState& st, int ply, Color side, unsigned flags, bool seeOrder) {
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move& m = moves[i];
        int sc = 0;
        if (m.isCapture() || m.isPromotion()) {
            if (flags & ORDER_CAPTURES) {
                sc = ORDER_TACTICAL + captureOrderScore(m);
                if (seeOrder && m.isCapture() && !m.isPromotion() && losesByShortcut(m) && see(pos, m) < 0) sc = ORDER_BAD_CAPTURE + captureOrderScore(m);
            }
        } else {
            if ((flags & ORDER_KILLERS) && m == st.killers[ply][0]) sc = ORDER_KILLER1;
            else if ((flags & ORDER_KILLERS) && m == st.killers[ply][1]) sc = ORDER_KILLER2;
            else if (flags & ORDER_HISTORY) sc = st.history[side][m.from][m.to];
        }
        scores[i] = sc;
    }
}

// Quiet move that caused a beta cutoff: remember it as a killer for this ply and credit its history.
void recordCutoff(SearchState& st, const Move& m, int ply, int depth, Color side, unsigned flags) {
    if (m.isCapture() || m.isPromotion()) return;
    if (flags & ORDER_KILLERS) {
        if (!(m == st.killers[ply][0])) {
            st.killers[ply][1] = st.killers[ply][0];
            st.killers[ply][0] = m;
        }
    }
    if (flags & ORDER_HISTORY) {
        int& h = st.history[side][m.from][m.to];
        h += depth * depth;
        if (h > HISTORY_MAX) {  // age every entry so scores stay below the killer band
            for (auto& bySide : st.history) for (auto& row : bySide) for (int& v : row) v /= 2;
        }
    }
}

// Search plies beyond which extensions are refused (ply + depth): keeps every array indexed by ply in bounds.
constexpr int SEARCH_PLY_LIMIT = 96;

// LMR reduction table: reduction grows with the remaining depth and with how late the move is.
struct LmrTable {
    int r[64][64];
    LmrTable() {
        for (int d = 0; d < 64; ++d)
            for (int m = 0; m < 64; ++m)
                r[d][m] = (d < 1 || m < 1) ? 0 : static_cast<int>(0.75 + std::log(static_cast<double>(d)) * std::log(static_cast<double>(m)) / 2.25);
    }
};
const LmrTable& lmrTable() {
    static const LmrTable table;
    return table;
}

bool hasNonPawnMaterial(const Position& pos, Color c) {
    return (pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP) | pos.pieces(c, ROOK) | pos.pieces(c, QUEEN)) != EMPTY_BB;
}

// Has `key` (the position at `ply`) occurred before on the current path or in the game? Only positions with the same
// side to move (an even number of plies back, at least 4) can repeat, and none older than the last irreversible move
// (halfmove clock) or the last null move (a null move is not a real move, so nothing before it counts).
bool isRepetition(const SearchState& st, int ply, uint64_t key, int halfmoveClock) {
    const int preRoot = static_cast<int>(st.gameKeys.size());
    int maxBack = std::min(halfmoveClock, ply + preRoot);
    if (st.nullPly >= 0) maxBack = std::min(maxBack, ply - st.nullPly - 1);
    for (int back = 4; back <= maxBack; back += 2) {
        const int idx = ply - back;
        const uint64_t k = idx >= 0 ? st.keys[idx] : st.gameKeys[static_cast<size_t>(preRoot + idx)];
        if (k == key) return true;
    }
    return false;
}

// Fail-soft alpha-beta in negamax form. Window: only scores strictly between alpha and beta are exact.
//   value >= beta : fail-high, the opponent will avoid this node (a lower bound on the true score)
//   value <= alpha: fail-low, no move here improves our best line (an upper bound on the true score)
// `prevPV` is the principal variation of the previous iteration; while `onPV` is true (every move so far followed
// it) the PV move is searched first.
// Stage 17: with all options off this is exactly the Stage 16 search. A node is a PV node when its window is wider
// than a zero window (beta - alpha > 1); the pruning techniques only apply to non-PV nodes. `allowNull` stops two null
// moves in a row; `excluded` (singular-extension verification) removes one move and disables the table for the node.
int alphaBeta(Position& pos, int depth, int ply, int alpha, int beta, PVLine& pv, uint64_t& nodes, Mode mode,
              SearchState& st, Control* ctl, const PVLine* prevPV, bool onPV, bool allowNull = true,
              const Move* excluded = nullptr) {
    ++nodes;
    pv.length = 0;
    if (ctl && checkAbort(*ctl, nodes)) return 0;
    const SearchOptions& opt = st.opt;
    const bool pvNode = beta - alpha > 1;
    SearchStats& stats = st.stats;
    if (ply > stats.maxPly) stats.maxPly = ply;
    const int extensionLimit = std::min(opt.extensionPlyLimit, SEARCH_PLY_LIMIT);

    uint64_t key = 0;
    if (st.tt || opt.repetition) key = hashKey(pos);
    if (opt.repetition) {
        st.keys[ply] = key;
        if (ply > 0 && isRepetition(st, ply, key, pos.halfmoveClock())) { ++stats.repetitionDraws; return 0; }
    }

    int score = 0;
    if (terminalScore(pos, ply, score)) return score;

    const Color side = pos.sideToMove();
    const bool checkDependent = opt.checkExtension || opt.nullMove || opt.reverseFutility || opt.futility || opt.razoring || opt.lmr;
    const bool inCheck = checkDependent && isInCheck(pos, side);
    if (opt.checkExtension && inCheck && ply + depth < extensionLimit) { ++depth; ++stats.checkExtensions; }
    if (depth <= 0) return mode.quiescence ? quiesce(pos, alpha, beta, ply, nodes, true, ctl, &st) : evaluate(pos);

    // Transposition table probe (Stage 16). Terminal positions were handled above, so every entry describes a
    // position with legal moves. The root (ply 0) is never cut off: the search must produce a move and a PV there.
    const int alphaOrig = alpha;
    uint16_t ttMove = 0;
    int ttScore = 0, ttDepth = -1;
    Bound ttBound = BOUND_NONE;
    if (st.tt && !excluded) {
        TTEntry e;
        if (st.tt->probe(key, e)) {
            ttMove = e.move;
            ttScore = scoreFromTT(e.score, ply);
            ttDepth = e.depth;
            ttBound = e.bound();
            const bool usable = st.ttMode == TT_DEPTH_GE ? e.depth >= depth : (st.ttMode == TT_EXACT_DEPTH && e.depth == depth);
            if (ply > 0 && usable) {
                if (ttBound == BOUND_EXACT || (ttBound == BOUND_LOWER && ttScore >= beta) || (ttBound == BOUND_UPPER && ttScore <= alpha)) {
                    ++stats.ttCutoffs;
                    return ttScore;
                }
            }
        }
    }

    // Static evaluation, only when a technique needs it (and never in check: the eval ignores the threat).
    int staticEval = 0;
    if ((opt.reverseFutility || opt.nullMove || opt.futility || opt.razoring) && !inCheck) staticEval = evaluate(pos);

    if (!excluded && !inCheck && !pvNode) {
        // Razoring: so far below alpha near the horizon that only a capture sequence could save the node.
        if (opt.razoring && depth <= opt.razorMaxDepth && std::abs(alpha) < MATE_BOUND &&
            staticEval + opt.razorMargin + opt.razorStep * (depth - 1) <= alpha) {
            const int v = quiesce(pos, alpha, alpha + 1, ply, nodes, true, ctl, &st);
            if (ctl && ctl->aborted) return 0;
            if (v <= alpha) { ++stats.razorCutoffs; return v; }
        }
        // Reverse futility: so far above beta near the horizon that the opponent will not allow this position.
        if (opt.reverseFutility && depth <= opt.rfpMaxDepth && std::abs(beta) < MATE_BOUND &&
            staticEval - opt.rfpMargin * depth >= beta) {
            ++stats.rfpCutoffs;
            return staticEval - opt.rfpMargin * depth;
        }
        // Null move: give the opponent a free move. If we still reach beta (searched shallower), the real moves will too.
        // Not with only pawns left (zugzwang: passing would be BETTER than any move) and never twice in a row.
        if (opt.nullMove && allowNull && depth >= opt.nullMinDepth && staticEval >= beta && beta > -MATE_BOUND &&
            hasNonPawnMaterial(pos, side)) {
            const int R = opt.nullBase + depth / opt.nullDivisor;
            ++stats.nullTries;
            const NullUndo nullUndo = makeNullMove(pos);
            const int savedNullPly = st.nullPly;
            st.nullPly = ply;
            PVLine dummy;
            const int v = -alphaBeta(pos, depth - 1 - R, ply + 1, -beta, -beta + 1, dummy, nodes, mode, st, ctl, nullptr, false, false);
            st.nullPly = savedNullPly;
            unmakeNullMove(pos, nullUndo);
            if (ctl && ctl->aborted) return 0;
            if (v >= beta) {
                ++stats.nullCutoffs;
                return v >= MATE_BOUND ? beta : v;  // never trust an unproven mate score from a pass
            }
        }
    }

    MoveList moves;
    generateLegalMoves(pos, moves);

    int scores[MoveList::CAPACITY];
    const bool followPV = onPV && prevPV && ply < prevPV->length;
    const bool ordered = mode.ordering != 0 || followPV || ttMove != 0;
    if (ordered) {
        scoreMoves(pos, moves, scores, st, ply, side, mode.ordering, opt.seeOrdering);
        if (followPV)
            for (size_t i = 0; i < moves.size(); ++i)
                if (moves[i] == prevPV->moves[ply]) scores[i] = ORDER_PV;
        if (ttMove != 0)  // never trust the stored move: it only counts if it is one of the legal moves right now
            for (size_t i = 0; i < moves.size(); ++i)
                if (packedMoveMatches(ttMove, moves[i])) { scores[i] = ORDER_HASH; break; }
    }

    const bool zeroWindowSearch = opt.pvs || opt.lmr;  // every move after the first is first tried with a zero window
    const bool futilityActive = opt.futility && !excluded && !pvNode && !inCheck && depth <= opt.futilityMaxDepth &&
                                std::abs(alpha) < MATE_BOUND;
    const int futilityValue = staticEval + opt.futilityMargin * depth;
    const bool singularCandidate = opt.singular && !excluded && ply > 0 && depth >= opt.singularMinDepth && ttMove != 0 &&
                                   (ttBound == BOUND_LOWER || ttBound == BOUND_EXACT) && ttDepth >= depth - 3 &&
                                   std::abs(ttScore) < MATE_BOUND;

    int best = -INFINITE_SCORE;
    Move bestMove{};
    PVLine childPV;
    int moveCount = 0;  // moves actually searched (pruned moves do not count)
    for (size_t i = 0; i < moves.size(); ++i) {
        if (ordered) {  // selection step: bring the best remaining move to position i
            size_t bestIdx = i;
            for (size_t j = i + 1; j < moves.size(); ++j) if (scores[j] > scores[bestIdx]) bestIdx = j;
            if (bestIdx != i) { std::swap(moves[i], moves[bestIdx]); std::swap(scores[i], scores[bestIdx]); }
        }
        const Move move = moves[i];
        if (excluded && move == *excluded) continue;
        const bool quiet = !move.isCapture() && !move.isPromotion();

        // Singular extension: is the table move the ONLY good move? Search every other move to half depth against a
        // bar a little below the table score; if all of them fail low, the table move is forced and gets an extra ply.
        int extension = 0;
        if (singularCandidate && packedMoveMatches(ttMove, move)) {
            ++stats.singularTests;
            const int singularBeta = ttScore - opt.singularMargin * depth;
            PVLine dummy;
            const int v = alphaBeta(pos, (depth - 1) / 2, ply, singularBeta - 1, singularBeta, dummy, nodes, mode, st, ctl, nullptr, false, false, &move);
            if (ctl && ctl->aborted) return 0;
            if (v < singularBeta && ply + depth < extensionLimit) { extension = 1; ++stats.singularExtensions; }
        }

        const UndoInfo undo = makeMove(pos, move);
        const bool givesCheck = (opt.futility || opt.lmr) && isInCheck(pos, pos.sideToMove());

        // Futility pruning: a quiet, non-checking move near the horizon whose best case (static eval plus a margin)
        // still cannot reach alpha. Never before one move has been searched, so `best` is a real result.
        if (futilityActive && quiet && !givesCheck && moveCount > 0 && futilityValue <= alpha) {
            unmakeMove(pos, move, undo);
            ++stats.futilityPrunes;
            if (futilityValue > best) best = futilityValue;
            continue;
        }

        const bool childOnPV = followPV && move == prevPV->moves[ply];
        const int newDepth = depth - 1 + extension;
        int value;
        if (zeroWindowSearch && moveCount > 0) {
            int r = 0;  // late move reduction: quiet moves late in the ordering are probably bad, so search them shallower
            if (opt.lmr && depth >= opt.lmrMinDepth && moveCount >= opt.lmrMoveThreshold && quiet && !inCheck && !givesCheck &&
                !(move == st.killers[ply][0]) && !(move == st.killers[ply][1])) {
                r = lmrTable().r[std::min(depth, 63)][std::min(moveCount, 63)];
                if (pvNode) --r;
                r = std::clamp(r, 0, newDepth - 1);
                if (r > 0) ++stats.lmrReductions;
            }
            value = -alphaBeta(pos, newDepth - r, ply + 1, -alpha - 1, -alpha, childPV, nodes, mode, st, ctl, prevPV, childOnPV);
            if (value > alpha && r > 0 && !(ctl && ctl->aborted)) {  // the reduced search says "good": verify at full depth
                ++stats.lmrResearches;
                value = -alphaBeta(pos, newDepth, ply + 1, -alpha - 1, -alpha, childPV, nodes, mode, st, ctl, prevPV, childOnPV);
            }
            if (value > alpha && value < beta && !(ctl && ctl->aborted)) {  // inside the window: needs the exact value
                ++stats.pvsResearches;
                value = -alphaBeta(pos, newDepth, ply + 1, -beta, -alpha, childPV, nodes, mode, st, ctl, prevPV, childOnPV);
            }
        } else {
            value = -alphaBeta(pos, newDepth, ply + 1, -beta, -alpha, childPV, nodes, mode, st, ctl, prevPV, childOnPV);
        }
        unmakeMove(pos, move, undo);
        if (ctl && ctl->aborted) return 0;
        ++moveCount;

        if (value > best) {
            best = value;
            bestMove = move;
            if (value > alpha) {  // improves the window: this is (so far) the principal variation
                alpha = value;
                pv.moves[0] = move;
                std::copy(childPV.moves, childPV.moves + childPV.length, pv.moves + 1);
                pv.length = childPV.length + 1;
                if (alpha >= beta) {  // beta cutoff
                    recordCutoff(st, move, ply, depth, side, mode.ordering);
                    break;
                }
            }
        }
    }

    if (st.tt && !excluded) {  // an aborted node already returned above, so everything stored here is a finished result
        const Bound b = best >= beta ? BOUND_LOWER : (best > alphaOrig ? BOUND_EXACT : BOUND_UPPER);
        // A fail-low node has no reliable best move (every move was refuted), so none is stored for UPPER.
        st.tt->store(key, depth, best, b, b == BOUND_UPPER ? uint16_t{0} : packMove(bestMove), ply);
    }
    return best;
}

// A transposition cutoff ends the PV early. Walk the table from the end of the PV, following stored best moves
// (each checked against the legal moves, and stopping at a repeated position), so the reported line is complete.
void extendPVFromTT(Position& pos, std::vector<Move>& pv, const TranspositionTable& tt) {
    std::vector<Move> applied;
    std::vector<UndoInfo> undos;
    std::vector<uint64_t> seen;
    for (const Move& m : pv) {
        seen.push_back(hashKey(pos));
        undos.push_back(makeMove(pos, m));
        applied.push_back(m);
    }
    while (static_cast<int>(pv.size()) < MAX_SEARCH_DEPTH) {
        const uint64_t key = hashKey(pos);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) break;
        seen.push_back(key);
        TTEntry e;
        if (!tt.probe(key, e) || e.move == 0) break;
        MoveList legal;
        generateLegalMoves(pos, legal);
        const Move* found = nullptr;
        for (size_t i = 0; i < legal.size(); ++i)
            if (packedMoveMatches(e.move, legal[i])) { found = &legal[i]; break; }
        if (!found) break;
        const Move m = *found;
        undos.push_back(makeMove(pos, m));
        applied.push_back(m);
        pv.push_back(m);
    }
    for (size_t i = applied.size(); i-- > 0;) unmakeMove(pos, applied[i], undos[i]);
}

SearchResult searchImpl(Position& pos, int depth, Mode mode, TranspositionTable* tt = nullptr, TTMode ttMode = TT_DEPTH_GE,
                        const SearchOptions& options = SearchOptions{}, const std::vector<uint64_t>& history = {}) {
    depth = std::clamp(depth, 0, MAX_SEARCH_DEPTH);
    SearchResult result;
    result.depth = depth;

    PVLine pv;
    if (mode.alphaBeta) {
        auto state = std::make_unique<SearchState>();  // large: keep it off the stack
        state->tt = tt;
        state->ttMode = ttMode;
        state->opt = options;
        state->gameKeys = history;
        if (tt) tt->newSearch();
        result.score = alphaBeta(pos, depth, 0, -INFINITE_SCORE, INFINITE_SCORE, pv, result.nodes, mode, *state, nullptr, nullptr, false);
        result.stats = state->stats;
    } else {
        result.score = negamax(pos, depth, 0, pv, result.nodes, mode);
    }
    result.pv.assign(pv.moves, pv.moves + pv.length);
    if (pv.length > 0) {
        result.hasMove = true;
        result.bestMove = pv.moves[0];
        if (tt) extendPVFromTT(pos, result.pv, *tt);
    }
    return result;
}

}  // namespace

SearchOptions SearchOptions::all() {
    SearchOptions o;
    o.pvs = o.aspiration = o.reverseFutility = o.nullMove = o.lmr = o.futility = o.razoring = true;
    o.checkExtension = o.singular = o.seeOrdering = o.seeQuiescence = o.repetition = true;
    return o;
}

// The set kept after the Stage 17 measurements (see docs and bench results): everything except razoring (it lost 23 of
// 148 tactical positions and gained nothing in self-play) and singular extensions (no gain at the node budgets reached,
// 246 against 252 positions solved at 80k nodes). Both stay available as options.
SearchOptions SearchOptions::engineDefault() {
    SearchOptions o = SearchOptions::all();
    o.razoring = false;
    o.singular = false;
    return o;
}

namespace {
struct FlagName { const char* name; bool SearchOptions::*flag; };
const FlagName FLAG_NAMES[] = {
    {"pvs", &SearchOptions::pvs}, {"asp", &SearchOptions::aspiration}, {"rfp", &SearchOptions::reverseFutility},
    {"null", &SearchOptions::nullMove}, {"lmr", &SearchOptions::lmr}, {"fut", &SearchOptions::futility},
    {"raz", &SearchOptions::razoring}, {"ext", &SearchOptions::checkExtension}, {"sing", &SearchOptions::singular},
    {"seeord", &SearchOptions::seeOrdering}, {"seeq", &SearchOptions::seeQuiescence}, {"rep", &SearchOptions::repetition},
};
}  // namespace

SearchOptions SearchOptions::fromString(const std::string& text, bool* ok) {
    SearchOptions o;
    if (ok) *ok = true;
    std::string token;
    auto apply = [&](std::string t) {
        if (t.empty()) return;
        bool remove = false;
        if (t[0] == '-') { remove = true; t.erase(0, 1); }
        if (t == "none") { o = SearchOptions{}; return; }
        if (t == "all" || t == "default") {
            const SearchOptions preset = t == "all" ? SearchOptions::all() : SearchOptions::engineDefault();
            for (const auto& f : FLAG_NAMES) o.*(f.flag) = remove ? false : (preset.*(f.flag) || o.*(f.flag));
            return;
        }
        for (const auto& f : FLAG_NAMES)
            if (t == f.name) { o.*(f.flag) = !remove; return; }
        if (ok) *ok = false;
    };
    for (char c : text) {
        if (c == ',' || c == '+' || c == ' ') { apply(token); token.clear(); }
        else token.push_back(c);
    }
    apply(token);
    return o;
}

std::string SearchOptions::toString() const {
    std::string out;
    for (const auto& f : FLAG_NAMES)
        if (this->*(f.flag)) out += (out.empty() ? "" : ",") + std::string(f.name);
    return out.empty() ? "none" : out;
}

int mateInMoves(int score) {
    if (!isMateScore(score)) return 0;
    if (score > 0) return (MATE_SCORE - score + 1) / 2;
    return -((MATE_SCORE + score + 1) / 2);
}

SearchResult search(Position& pos, int depth) { return searchImpl(pos, depth, {true, true, ORDER_DEFAULT}); }
SearchResult searchOrdered(Position& pos, int depth, unsigned flags) { return searchImpl(pos, depth, {true, true, flags}); }
SearchResult searchTT(Position& pos, int depth, TranspositionTable& tt, TTMode mode, unsigned ordering) {
    return searchImpl(pos, depth, {true, true, ordering}, &tt, mode);
}
SearchResult searchOpt(Position& pos, int depth, const SearchOptions& options, TranspositionTable* tt, TTMode mode,
                       const std::vector<uint64_t>& history) {
    return searchImpl(pos, depth, {true, true, ORDER_DEFAULT}, tt, mode, options, history);
}
SearchResult searchAlphaBeta(Position& pos, int depth) { return searchImpl(pos, depth, {true, false, 0}); }
SearchResult searchMinimax(Position& pos, int depth) { return searchImpl(pos, depth, {false, false, 0}); }
SearchResult searchMinimaxQuiescence(Position& pos, int depth) { return searchImpl(pos, depth, {false, true, 0}); }

int64_t allocateTimeMs(int64_t timeLeftMs, int64_t incrementMs, int movesToGo) {
    if (timeLeftMs <= 0) return 1;
    const int64_t margin = std::max<int64_t>(10, timeLeftMs / 50);  // never plan to use the last 2% (or 10 ms)
    const int divisor = movesToGo > 0 ? std::min(movesToGo, 50) + 1 : 30;
    int64_t t = timeLeftMs / divisor + incrementMs * 3 / 4;
    t = std::min(t, timeLeftMs - margin);
    return std::max<int64_t>(t, 1);
}

// One iterative-deepening search. threadId 0 is the main thread (reports, owns the clock, bumps the table generation);
// helpers (threadId > 0) skip some depths, never report, and add their node counts to `*extraNodes`.
static SearchResult iterativeImpl(Position& pos, const SearchLimits& limits, const InfoCallback& info, int threadId,
                                  std::atomic<uint64_t>* extraNodes) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto elapsedMs = [&]() { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };

    const int maxDepth = std::clamp(limits.maxDepth, 1, MAX_SEARCH_DEPTH);
    Mode mode{true, true, ORDER_DEFAULT};
    auto state = std::make_unique<SearchState>();  // shared by all iterations: killers and history carry over
    state->tt = limits.tt;
    state->ttMode = limits.ttMode;
    state->opt = limits.options;
    state->gameKeys = limits.history;
    const SearchOptions& opt = limits.options;
    if (limits.tt && threadId == 0 && !extraNodes) limits.tt->newSearch();  // (multi-thread: the driver did it) one generation per search; iterations of one search share it

    Control ctl;
    ctl.nodeLimit = limits.maxNodes;
    ctl.stop = limits.stop;
    if (limits.movetimeMs > 0) {
        ctl.hasDeadline = true;
        ctl.deadline = start + std::chrono::milliseconds(limits.movetimeMs);
    }

    SearchResult best;
    uint64_t nodes = 0;
    PVLine prevPV;
    bool havePrev = false;

    uint64_t reported = 0;  // helper nodes already added to *extraNodes
    auto publishNodes = [&]() {
        if (threadId > 0 && extraNodes) { extraNodes->fetch_add(nodes - reported, std::memory_order_relaxed); reported = nodes; }
    };

    for (int depth = 1; depth <= maxDepth; ++depth) {
        // Lazy SMP schedule: a helper skips every third depth (a different one per helper) so the threads drift apart.
        if (threadId > 0 && depth > 3 && (depth + threadId) % 3 == 0) continue;
        PVLine pv;
        // Aspiration windows: from depth aspirationMinDepth on, search a narrow window around the previous score. A score
        // inside the window is exact (identical to the full-window result); outside it, widen on that side and search
        // again. A fail-low leaves no PV, so such a result is never used: only the final in-window search counts.
        int alpha = -INFINITE_SCORE, beta = INFINITE_SCORE, delta = opt.aspirationDelta;
        if (opt.aspiration && depth >= opt.aspirationMinDepth && havePrev && !isMateScore(best.score)) {
            alpha = std::max(best.score - delta, -INFINITE_SCORE);
            beta = std::min(best.score + delta, INFINITE_SCORE);
        }
        int score = 0;
        for (;;) {
            score = alphaBeta(pos, depth, 0, alpha, beta, pv, nodes, mode, *state, &ctl, havePrev ? &prevPV : nullptr, havePrev);
            if (ctl.aborted) break;
            if (score <= alpha && alpha > -INFINITE_SCORE) {
                beta = (alpha + beta) / 2;
                delta += delta / 2 + 1;
                alpha = delta >= 1000 ? -INFINITE_SCORE : std::max(score - delta, -INFINITE_SCORE);
                ++state->stats.aspirationResearches;
            } else if (score >= beta && beta < INFINITE_SCORE) {
                delta += delta / 2 + 1;
                beta = delta >= 1000 ? INFINITE_SCORE : std::min(score + delta, INFINITE_SCORE);
                ++state->stats.aspirationResearches;
            } else {
                break;
            }
        }
        publishNodes();
        if (ctl.aborted) {  // discard the unfinished iteration; the previous one stands
            best.interrupted = true;
            break;
        }

        best.score = score;
        best.depth = depth;
        best.pv.assign(pv.moves, pv.moves + pv.length);
        best.hasMove = pv.length > 0;
        if (best.hasMove) best.bestMove = pv.moves[0];
        if (best.hasMove && limits.tt) extendPVFromTT(pos, best.pv, *limits.tt);  // reporting only: prevPV stays the raw line
        best.nodes = nodes;
        best.timeMs = elapsedMs();
        if (info) info(SearchInfo{depth, score, nodes + (extraNodes && threadId == 0 ? extraNodes->load(std::memory_order_relaxed) : 0), best.timeMs, best.pv});

        if (!best.hasMove) break;  // root is terminal (mate, stalemate, draw): nothing to search deeper
        prevPV = pv;
        havePrev = true;

        // A mate found within the searched depth is the shortest one: searching deeper cannot improve it.
        if (isMateScore(score) && MATE_SCORE - std::abs(score) <= depth) break;
        if (limits.stop && limits.stop->load(std::memory_order_relaxed)) { best.interrupted = depth < maxDepth; break; }
        if (limits.maxNodes != 0 && nodes >= limits.maxNodes) { best.interrupted = depth < maxDepth; break; }
        // Soft time limit: the next iteration usually costs several times the last, so do not start one that
        // is unlikely to finish. The hard deadline above still interrupts anything that overruns.
        if (limits.movetimeMs > 0 && depth < maxDepth && elapsedMs() >= 0.6 * static_cast<double>(limits.movetimeMs)) {
            best.interrupted = true;
            break;
        }
    }

    // Safe fallback: interrupted before even depth 1 finished. Return a legal move anyway (best capture by MVV-LVA,
    // else the first legal move) at depth 0 with the static evaluation, so the caller can always play something.
    if (!best.hasMove && best.interrupted) {
        MoveList legal;
        generateLegalMoves(pos, legal);
        if (legal.size() > 0) {
            size_t pick = 0;
            int pickScore = -1;
            for (size_t i = 0; i < legal.size(); ++i) {
                const int sc = (legal[i].isCapture() || legal[i].isPromotion()) ? captureOrderScore(legal[i]) : 0;
                if (sc > pickScore) { pickScore = sc; pick = i; }
            }
            best.hasMove = true;
            best.bestMove = legal[pick];
            best.pv.assign(1, legal[pick]);
            best.score = evaluate(pos);
            best.depth = 0;
        }
    }
    best.nodes = nodes;
    best.timeMs = elapsedMs();
    best.stats = state->stats;
    return best;
}

SearchResult searchIterative(Position& pos, const SearchLimits& limits, const InfoCallback& info) {
#if MATEX_THREADS
    if (limits.threads > 1 && limits.tt) {
        const int helpers = std::min(limits.threads, 64) - 1;
        std::atomic<bool> helperStop{false};
        std::atomic<uint64_t> extraNodes{0};
        limits.tt->newSearch();  // once, before any helper starts: the generation counter is not atomic
        SearchLimits hl = limits;
        hl.threads = 1;
        hl.movetimeMs = 0;
        hl.maxNodes = 0;
        hl.stop = &helperStop;
        std::vector<std::thread> pool;
        // Stops and joins the helpers on every way out of this scope (also if the main search throws).
        struct Joiner {
            std::atomic<bool>& stop; std::vector<std::thread>& pool;
            ~Joiner() { stop.store(true); for (std::thread& t : pool) if (t.joinable()) t.join(); }
        } joiner{helperStop, pool};
        // Every helper owns its board, copied HERE before any thread starts (the main thread mutates `pos` as it searches).
        std::vector<Position> boards(static_cast<size_t>(helpers), pos);
        for (int t = 1; t <= helpers; ++t)
            pool.emplace_back([&boards, &hl, &extraNodes, t]() {
                iterativeImpl(boards[static_cast<size_t>(t - 1)], hl, nullptr, t, &extraNodes);
            });
        SearchResult r = iterativeImpl(pos, limits, info, 0, &extraNodes);
        helperStop.store(true);
        for (std::thread& t : pool) t.join();
        pool.clear();
        r.nodes += extraNodes.load();
        return r;
    }
#endif
    return iterativeImpl(pos, limits, info, 0, nullptr);
}