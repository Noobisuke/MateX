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
    table_ = std::vector<Slot>(n);  // atomics are not copyable: build a fresh, zeroed vector
    mask_ = n - 1;
    gen_ = 0;
    probes_ = 0; hits_ = 0; stores_ = 0;
}

void TranspositionTable::setPolicy(TTReplace policy) {
    policy_ = policy;
    clusterSize_ = clusterSizeOf(policy);
    clear();
}

void TranspositionTable::clear() {
    for (Slot& s : table_) {
        s.keyXorData.store(0, std::memory_order_relaxed);
        s.data.store(0, std::memory_order_relaxed);
    }
    gen_ = 0;
    probes_ = 0; hits_ = 0; stores_ = 0;
}

uint64_t TranspositionTable::packData(const TTEntry& e) {
    return static_cast<uint64_t>(e.move) | (static_cast<uint64_t>(static_cast<uint16_t>(e.score)) << 16) |
           (static_cast<uint64_t>(static_cast<uint8_t>(e.depth)) << 32) | (static_cast<uint64_t>(e.genBound) << 40);
}

TTEntry TranspositionTable::unpack(uint64_t keyXorData, uint64_t data) {
    TTEntry e;
    e.key = keyXorData ^ data;
    e.move = static_cast<uint16_t>(data);
    e.score = static_cast<int16_t>(static_cast<uint16_t>(data >> 16));
    e.depth = static_cast<int8_t>(static_cast<uint8_t>(data >> 32));
    e.genBound = static_cast<uint8_t>(data >> 40);
    return e;
}

TTEntry TranspositionTable::load(size_t i) const {
    const uint64_t a = table_[i].keyXorData.load(std::memory_order_relaxed);
    const uint64_t d = table_[i].data.load(std::memory_order_relaxed);
    return unpack(a, d);
}

void TranspositionTable::put(size_t i, const TTEntry& e) {
    const uint64_t d = packData(e);
    table_[i].keyXorData.store(e.key ^ d, std::memory_order_relaxed);
    table_[i].data.store(d, std::memory_order_relaxed);
}

bool TranspositionTable::probe(uint64_t key, TTEntry& out) const {
    probes_.store(probes_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    const size_t base = clusterBase(key);
    for (size_t i = 0; i < clusterSize_; ++i) {
        const TTEntry e = load(base + i);
        if (e.key == key && e.bound() != BOUND_NONE) {
            out = e;
            hits_.store(hits_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

void TranspositionTable::store(uint64_t key, int depth, int score, Bound bound, uint16_t move, int ply) {
    stores_.store(stores_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
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
        TTEntry e = load(base + i);
        if (e.key == key && e.bound() != BOUND_NONE) {
            if (policy_ == TTReplace::ALWAYS || bound == BOUND_EXACT || e.generation() != gen_ || depth + 4 > e.depth) {
                if (fresh.move == 0) fresh.move = e.move;
                put(base + i, fresh);
            } else if (e.move == 0) {
                e.move = fresh.move;  // a shallower result still contributes a move
                put(base + i, e);
            }
            return;
        }
    }

    // 2. An empty slot.
    for (size_t i = 0; i < clusterSize_; ++i) {
        if (load(base + i).bound() == BOUND_NONE) { put(base + i, fresh); return; }
    }

    // 3. Everything is occupied by other positions: the policy picks a victim.
    switch (policy_) {
        case TTReplace::ALWAYS:
            put(base, fresh);
            return;
        case TTReplace::DEPTH_AGE: {
            const TTEntry e = load(base);
            if (depth >= e.depth || e.generation() != gen_) put(base, fresh);  // otherwise the new result is dropped
            return;
        }
        case TTReplace::TWO_TIER: {
            const TTEntry deep = load(base);
            if (depth >= deep.depth || deep.generation() != gen_) put(base, fresh);
            else put(base + 1, fresh);  // the always-replace slot
            return;
        }
        case TTReplace::BUCKET4: {
            // Value of an entry = its depth minus 8 per generation of age. The least valuable one goes.
            size_t victim = base;
            int victimValue = 1 << 30;
            for (size_t i = 0; i < clusterSize_; ++i) {
                const TTEntry e = load(base + i);
                const int age = static_cast<int>((gen_ - e.generation()) & 63u);
                const int value = e.depth - 8 * age;
                if (value < victimValue) { victimValue = value; victim = base + i; }
            }
            put(victim, fresh);
            return;
        }
    }
}

int TranspositionTable::hashfull() const {
    const size_t sample = std::min<size_t>(1000, table_.size());
    size_t used = 0;
    for (size_t i = 0; i < sample; ++i)
    {
        const TTEntry e = load(i);
        if (e.bound() != BOUND_NONE && e.generation() == gen_) ++used;
    }
    return static_cast<int>(used * 1000 / sample);
}
