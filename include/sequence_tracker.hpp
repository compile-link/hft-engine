#pragma once

#include "market_data.pb.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

enum class SeqResult : std::size_t {
    InOrder = 0,     // sequence greater than the last accepted one (skips are normal for bookTicker)
    SessionStart,    // first tick of a new session for this symbol: baseline (re)established
    Duplicate,       // same sequence as the last accepted tick
    OutOfOrder,      // sequence lower than the last accepted tick
    MissingSequence, // sequence == 0 (not provided by the source)
    StaleSession,    // tick from an older session than the one already seen
    Count
};

const char* seq_result_to_str(SeqResult r);
inline bool seq_accepted(SeqResult r) { return r == SeqResult::InOrder || r == SeqResult::SessionStart; }

// Per-symbol continuity check on TopOfBook::sequence within a source session.
// A session change (reconnect, replay loop) resets the baseline: IDs across a
// disconnect are not comparable, so the gap is reported as SessionStart, not an error.
class SequenceTracker {
  public:
    SeqResult check(const hft::TopOfBook& tob);

    uint64_t count(SeqResult r) const { return counts_[static_cast<std::size_t>(r)]; }
    uint64_t rejected_total() const;
    std::string summary() const;

  private:
    struct State {
        uint64_t session = 0;
        uint64_t last_seq = 0;
    };
    SeqResult classify(const hft::TopOfBook& tob);

    std::unordered_map<std::string, State> by_symbol_;
    std::array<uint64_t, static_cast<std::size_t>(SeqResult::Count)> counts_{};
};
