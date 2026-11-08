#include "search.hpp"
#include "../board/attacks.hpp"
#include "../eval/evaluate.hpp"
#include "../move/legal.hpp"
#include "../move/makemove.hpp"
#include "../rules/gamestate.hpp"
#include <algorithm>
#include <chrono>
#include <memory>

namespace {

constexpr int PAWN_VALUE_FOR_ORDER = 100;

struct PVLine {
    Move moves[MAX_SEARCH_DEPTH + 1];
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

// Move ordering gives every move a priority; higher is searched first. It never changes the score, only the
// node count:
//   1. captures and promotions, most valuable victim first (MVV-LVA)    [ORDER_CAPTURES]
//   2. killer moves: quiet moves that caused a beta cutoff at this ply  [ORDER_KILLERS]
//   3. remaining quiet moves by history (how often they caused cutoffs) [ORDER_HISTORY]

int quiesce(Position& pos, int alpha, int beta, int ply, uint64_t& nodes, bool prune, Control* ctl) {
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
        const int value = -quiesce(pos, -beta, -alpha, ply + 1, nodes, prune, ctl);
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

void scoreMoves(const MoveList& moves, int* scores, const SearchState& st, int ply, Color side, unsigned flags) {
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move& m = moves[i];
        int sc = 0;
        if (m.isCapture() || m.isPromotion()) {
            if (flags & ORDER_CAPTURES) sc = ORDER_TACTICAL + captureOrderScore(m);
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

// Fail-soft alpha-beta in negamax form. Window: only scores strictly between alpha and beta are exact.
//   value >= beta : fail-high, the opponent will avoid this node (a lower bound on the true score)
//   value <= alpha: fail-low, no move here improves our best line (an upper bound on the true score)
// `prevPV` is the principal variation of the previous iteration; while `onPV` is true (every move so far followed
// it) the PV move is searched first.
int alphaBeta(Position& pos, int depth, int ply, int alpha, int beta, PVLine& pv, uint64_t& nodes, Mode mode,
              SearchState& st, Control* ctl, const PVLine* prevPV, bool onPV) {
    ++nodes;
    pv.length = 0;
    if (ctl && checkAbort(*ctl, nodes)) return 0;

    int score = 0;
    if (terminalScore(pos, ply, score)) return score;
    if (depth == 0) return mode.quiescence ? quiesce(pos, alpha, beta, ply, nodes, true, ctl) : evaluate(pos);

    MoveList moves;
    generateLegalMoves(pos, moves);

    const Color side = pos.sideToMove();
    int scores[MoveList::CAPACITY];
    const bool followPV = onPV && prevPV && ply < prevPV->length;
    const bool ordered = mode.ordering != 0 || followPV;
    if (ordered) {
        scoreMoves(moves, scores, st, ply, side, mode.ordering);
        if (followPV)
            for (size_t i = 0; i < moves.size(); ++i)
                if (moves[i] == prevPV->moves[ply]) scores[i] = ORDER_PV;
    }

    int best = -INFINITE_SCORE;
    PVLine childPV;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (ordered) {  // selection step: bring the best remaining move to position i
            size_t bestIdx = i;
            for (size_t j = i + 1; j < moves.size(); ++j) if (scores[j] > scores[bestIdx]) bestIdx = j;
            if (bestIdx != i) { std::swap(moves[i], moves[bestIdx]); std::swap(scores[i], scores[bestIdx]); }
        }
        const Move move = moves[i];
        const UndoInfo undo = makeMove(pos, move);
        const bool childOnPV = followPV && move == prevPV->moves[ply];
        const int value = -alphaBeta(pos, depth - 1, ply + 1, -beta, -alpha, childPV, nodes, mode, st, ctl, prevPV, childOnPV);
        unmakeMove(pos, move, undo);
        if (ctl && ctl->aborted) return 0;

        if (value > best) {
            best = value;
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
    return best;
}

SearchResult searchImpl(Position& pos, int depth, Mode mode) {
    depth = std::clamp(depth, 0, MAX_SEARCH_DEPTH);
    SearchResult result;
    result.depth = depth;

    PVLine pv;
    if (mode.alphaBeta) {
        auto state = std::make_unique<SearchState>();  // large: keep it off the stack
        result.score = alphaBeta(pos, depth, 0, -INFINITE_SCORE, INFINITE_SCORE, pv, result.nodes, mode, *state, nullptr, nullptr, false);
    } else {
        result.score = negamax(pos, depth, 0, pv, result.nodes, mode);
    }
    result.pv.assign(pv.moves, pv.moves + pv.length);
    if (pv.length > 0) {
        result.hasMove = true;
        result.bestMove = pv.moves[0];
    }
    return result;
}

}  // namespace

int mateInMoves(int score) {
    if (!isMateScore(score)) return 0;
    if (score > 0) return (MATE_SCORE - score + 1) / 2;
    return -((MATE_SCORE + score + 1) / 2);
}

SearchResult search(Position& pos, int depth) { return searchImpl(pos, depth, {true, true, ORDER_DEFAULT}); }
SearchResult searchOrdered(Position& pos, int depth, unsigned flags) { return searchImpl(pos, depth, {true, true, flags}); }
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

SearchResult searchIterative(Position& pos, const SearchLimits& limits, const InfoCallback& info) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto elapsedMs = [&]() { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };

    const int maxDepth = std::clamp(limits.maxDepth, 1, MAX_SEARCH_DEPTH);
    Mode mode{true, true, ORDER_DEFAULT};
    auto state = std::make_unique<SearchState>();  // shared by all iterations: killers and history carry over

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

    for (int depth = 1; depth <= maxDepth; ++depth) {
        PVLine pv;
        const int score = alphaBeta(pos, depth, 0, -INFINITE_SCORE, INFINITE_SCORE, pv, nodes, mode, *state, &ctl,
                                    havePrev ? &prevPV : nullptr, havePrev);
        if (ctl.aborted) {  // discard the unfinished iteration; the previous one stands
            best.interrupted = true;
            break;
        }

        best.score = score;
        best.depth = depth;
        best.pv.assign(pv.moves, pv.moves + pv.length);
        best.hasMove = pv.length > 0;
        if (best.hasMove) best.bestMove = pv.moves[0];
        best.nodes = nodes;
        best.timeMs = elapsedMs();
        if (info) info(SearchInfo{depth, score, nodes, best.timeMs, best.pv});

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
    return best;
}