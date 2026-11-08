#include "tt.hpp"
#include "search.hpp"  // MATE_BOUND / MATE_SCORE
#include <algorithm>

uint16_t packMove(const Move& m) {
    const unsigned promo = (m.promotion == NO_PIECE_TYPE) ? 0u : static_cast<unsigned>(m.promotion);  // 1..4 for N,B,R,Q
    return static_cast<uint16_t>(static_cast<unsigned>(m.from) | (static_cast<unsigned>(m.to) << 6) | (promo << 12));
}

bool packedMoveMatches(uint16_t packed, const Move& m) {
    return packed != 0 && packed == packMove(m);
}

int scoreToTT(int score, int ply) {
    if (score >= MATE_BOUND) return score + ply;   // "mate in N from here" instead of "from the root"
    if (score <= -MATE_BOUND) return score - ply;
    return score;
}

int scoreFromTT(int stored, int ply) {
    if (stored >= MATE_BOUND) return stored - ply;
    if (stored <= -MATE_BOUND) return stored + ply;
    return stored;
}

size_t TranspositionTable::clusterSizeOf(TTReplace p) {
    switch (p) {
        case TTReplace::ALWAYS:
        case TTReplace::DEPTH_AGE: return 1;
        case TTReplace::TWO_TIER: return 2;
        case TTReplace::BUCKET4: return 4;
    }
    return 1;
}

TranspositionTable::TranspositionTable(size_t megabytes, TTReplace policy) : policy_(policy) {
    clusterSize_ = clusterSizeOf(policy);
    resize(megabytes);
}

void TranspositionTable::resize(size_t megabytes) {
    resizeEntries(std::max<size_t>(1, megabytes) * 1024 * 1024 / sizeof(TTEntry));
}

void TranspositionTable::resizeEntries(size_t wanted) {
    size_t n = clusterSize_;
    while (n * 2 <= wanted) n *= 2;
    table_.assign(n, TTEntry{});
    mask_ = n - 1;
    gen_ = 0;
    probes_ = hits_ = stores_ = 0;
}

void TranspositionTable::setPolicy(TTReplace policy) {
    policy_ = policy;
    clusterSize_ = clusterSizeOf(policy);
    clear();
}

void TranspositionTable::clear() {
    std::fill(table_.begin(), table_.end(), TTEntry{});
    gen_ = 0;
    probes_ = hits_ = stores_ = 0;
}

bool TranspositionTable::probe(uint64_t key, TTEntry& out) const {
    ++probes_;
    const size_t base = clusterBase(key);
    for (size_t i = 0; i < clusterSize_; ++i) {
        const TTEntry& e = table_[base + i];
        if (e.key == key && e.bound() != BOUND_NONE) {
            out = e;
            ++hits_;
            return true;
        }
    }
    return false;
}

void TranspositionTable::store(uint64_t key, int depth, int score, Bound bound, uint16_t move, int ply) {
    ++stores_;
    const size_t base = clusterBase(key);
    TTEntry fresh;
    fresh.key = key;
    fresh.move = move;
    fresh.score = static_cast<int16_t>(scoreToTT(score, ply));
    fresh.depth = static_cast<int8_t>(std::clamp(depth, -128, 127));
    fresh.genBound = static_cast<uint8_t>((gen_ << 2) | static_cast<unsigned>(bound));

    // 1. The same position is already here: update it (keeping its move if we have none), unless the old
    //    result is clearly more valuable (much deeper, same search, and ours is not exact).
    for (size_t i = 0; i < clusterSize_; ++i) {
        TTEntry& e = table_[base + i];
        if (e.key == key && e.bound() != BOUND_NONE) {
            if (policy_ == TTReplace::ALWAYS || bound == BOUND_EXACT || e.generation() != gen_ || depth + 4 > e.depth) {
                if (fresh.move == 0) fresh.move = e.move;
                e = fresh;
            } else if (e.move == 0) {
                e.move = fresh.move;  // a shallower result still contributes a move
            }
            return;
        }
    }

    // 2. An empty slot.
    for (size_t i = 0; i < clusterSize_; ++i) {
        TTEntry& e = table_[base + i];
        if (e.bound() == BOUND_NONE) { e = fresh; return; }
    }

    // 3. Everything is occupied by other positions: the policy picks a victim.
    switch (policy_) {
        case TTReplace::ALWAYS:
            table_[base] = fresh;
            return;
        case TTReplace::DEPTH_AGE: {
            TTEntry& e = table_[base];
            if (depth >= e.depth || e.generation() != gen_) e = fresh;  // otherwise the new result is dropped
            return;
        }
        case TTReplace::TWO_TIER: {
            TTEntry& deep = table_[base];
            if (depth >= deep.depth || deep.generation() != gen_) deep = fresh;
            else table_[base + 1] = fresh;  // the always-replace slot
            return;
        }
        case TTReplace::BUCKET4: {
            // Value of an entry = its depth minus 8 per generation of age. The least valuable one goes.
            size_t victim = base;
            int victimValue = 1 << 30;
            for (size_t i = 0; i < clusterSize_; ++i) {
                const TTEntry& e = table_[base + i];
                const int age = static_cast<int>((gen_ - e.generation()) & 63u);
                const int value = e.depth - 8 * age;
                if (value < victimValue) { victimValue = value; victim = base + i; }
            }
            table_[victim] = fresh;
            return;
        }
    }
}

int TranspositionTable::hashfull() const {
    const size_t sample = std::min<size_t>(1000, table_.size());
    size_t used = 0;
    for (size_t i = 0; i < sample; ++i)
        if (table_[i].bound() != BOUND_NONE && table_[i].generation() == gen_) ++used;
    return static_cast<int>(used * 1000 / sample);
}
