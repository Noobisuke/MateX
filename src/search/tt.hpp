#pragma once
// Stage 16: transposition table.
//
// A big hash table remembering what the search learned about positions, so the same position reached by a
// different move order (a "transposition") is not searched again, and so the best move found earlier can be
// tried first ("hash move"). Indexed by the Zobrist key (see board/zobrist.hpp).
//
// An entry stores the FULL 64-bit key (checked on probe, so a different position landing in the same slot is
// detected), the searched depth, a score, how to interpret that score (the bound), and the best move.
//
// Bound types (the score is only a bound unless the search finished inside its alpha-beta window):
//   BOUND_EXACT : alpha < score < beta  -> the true value of the position at that depth
//   BOUND_LOWER : score >= beta (fail high, a cutoff) -> the true value is AT LEAST score
//   BOUND_UPPER : score <= alpha (fail low)           -> the true value is AT MOST score
//
// Mate scores are relative to the root ("mated at ply p = -(MATE-p)"), but a TT entry is shared by paths of
// different length. So a mate score is stored relative to the entry's own position (distance from THAT node)
// and converted back with the probing node's ply: see scoreToTT / scoreFromTT.
//
// Replacement: when a slot is needed, which entry is thrown away? Four policies, compared by bench_tt.
#include "../move/move.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

enum class TTReplace {
    ALWAYS,       // 1 slot per index, always overwrite
    DEPTH_AGE,    // 1 slot per index, overwrite only if the new depth >= old depth or the old entry is from an older search
    TWO_TIER,     // 2 slots: slot 0 depth-preferred (as above), slot 1 always-replace
    BUCKET4,      // 4 slots (one cache line): replace the entry with the lowest "depth minus age penalty"
};

// A move packed in 16 bits: from (6) | to (6) << 6 | promotion code (3) << 12. 0 means "no move" (a1a1 never occurs).
uint16_t packMove(const Move& m);
bool packedMoveMatches(uint16_t packed, const Move& m);  // compares from, to and promotion piece

// Mate-score conversion. Non-mate scores pass through unchanged.
int scoreToTT(int score, int ply);
int scoreFromTT(int stored, int ply);

struct TTEntry {
    uint64_t key = 0;       // full Zobrist key; 0 with bound NONE marks an empty slot
    uint16_t move = 0;      // packed best move, 0 = none
    int16_t score = 0;      // stored (ply-independent) score
    int8_t depth = 0;
    uint8_t genBound = 0;   // bits 2..7: search generation ("age"), bits 0..1: Bound

    Bound bound() const { return static_cast<Bound>(genBound & 3); }
    unsigned generation() const { return genBound >> 2; }
};
static_assert(sizeof(TTEntry) == 16, "TTEntry should be 16 bytes (4 per cache line)");

class TranspositionTable {
public:
    explicit TranspositionTable(size_t megabytes = 16, TTReplace policy = TTReplace::BUCKET4);

    // Resizes (rounded DOWN to a power-of-two number of entries, at least one cluster) and clears.
    void resize(size_t megabytes);
    void resizeEntries(size_t entries);  // same, but by entry count (16 bytes each): for tests and benchmarks
    void setPolicy(TTReplace policy);   // also clears: slot layout depends on the policy
    TTReplace policy() const { return policy_; }

    void clear();                       // forget everything, reset statistics
    void newSearch() { gen_ = (gen_ + 1) & 63; }  // call once per search: older entries become replaceable

    // Looks the position up. Returns true and fills `out` when an entry with exactly this key exists.
    bool probe(uint64_t key, TTEntry& out) const;

    // Stores a result for the node at `ply`. `move` is a packed move or 0. `depth` is the remaining depth searched.
    // The mate score is converted for storage. An existing entry for the same key keeps its move when `move` is 0.
    void store(uint64_t key, int depth, int score, Bound bound, uint16_t move, int ply);

    size_t entryCount() const { return table_.size(); }
    size_t clusterSize() const { return clusterSize_; }
    int hashfull() const;               // permille (0..1000) of a sample of entries written by the current search

    // Statistics (reset by clear()).
    uint64_t probes() const { return probes_; }
    uint64_t hits() const { return hits_; }
    uint64_t stores() const { return stores_; }

private:
    size_t clusterBase(uint64_t key) const { return static_cast<size_t>(key & mask_) & ~(clusterSize_ - 1); }
    static size_t clusterSizeOf(TTReplace p);

    std::vector<TTEntry> table_;
    size_t mask_ = 0;          // table_.size() - 1 (size is a power of two)
    size_t clusterSize_ = 4;
    TTReplace policy_;
    unsigned gen_ = 0;         // 0..63
    mutable uint64_t probes_ = 0, hits_ = 0;
    uint64_t stores_ = 0;
};
