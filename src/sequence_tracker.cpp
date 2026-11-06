#include "sequence_tracker.hpp"
#include <sstream>

const char* seq_result_to_str(SeqResult r) {
    switch (r) {
    case SeqResult::InOrder:
        return "in_order";
    case SeqResult::SessionStart:
        return "session_start";
    case SeqResult::Duplicate:
        return "duplicate";
    case SeqResult::OutOfOrder:
        return "out_of_order";
    case SeqResult::MissingSequence:
        return "missing_sequence";
    case SeqResult::StaleSession:
        return "stale_session";
    default:
        return "unknown";
    }
}

SeqResult SequenceTracker::check(const hft::TopOfBook& tob) {
    const SeqResult r = classify(tob);
    counts_[static_cast<std::size_t>(r)]++;
    return r;
}

SeqResult SequenceTracker::classify(const hft::TopOfBook& tob) {
    if (tob.sequence() == 0) {
        return SeqResult::MissingSequence;
    }
    auto [it, inserted] = by_symbol_.try_emplace(tob.symbol());
    State& st = it->second;

    if (inserted || tob.session() > st.session) {
        st.session = tob.session();
        st.last_seq = tob.sequence();
        return SeqResult::SessionStart;
    }
    if (tob.session() < st.session) {
        return SeqResult::StaleSession;
    }
    if (tob.sequence() == st.last_seq) {
        return SeqResult::Duplicate;
    }
    if (tob.sequence() < st.last_seq) {
        return SeqResult::OutOfOrder;
    }
    st.last_seq = tob.sequence();
    return SeqResult::InOrder;
}

uint64_t SequenceTracker::rejected_total() const {
    uint64_t total = 0;
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        if (!seq_accepted(static_cast<SeqResult>(i))) {
            total += counts_[i];
        }
    }
    return total;
}

std::string SequenceTracker::summary() const {
    std::ostringstream oss;
    oss << "Sequence:";
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        oss << " " << seq_result_to_str(static_cast<SeqResult>(i)) << "=" << counts_[i];
    }
    return oss.str();
}
