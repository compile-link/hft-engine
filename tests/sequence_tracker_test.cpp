#include "sequence_tracker.hpp"
#include <gtest/gtest.h>
#include <string>

namespace {
    hft::TopOfBook tick(uint64_t seq, uint64_t session = 1, const std::string& symbol = "BTCUSDT") {
        hft::TopOfBook t;
        t.set_symbol(symbol);
        t.set_sequence(seq);
        t.set_session(session);
        return t;
    }
} // namespace

TEST(SequenceTracker, FirstTickStartsSession) {
    SequenceTracker s;
    EXPECT_EQ(s.check(tick(100)), SeqResult::SessionStart);
}

TEST(SequenceTracker, IncreasingIsInOrderIncludingSkips) {
    SequenceTracker s;
    s.check(tick(100));
    EXPECT_EQ(s.check(tick(101)), SeqResult::InOrder);
    EXPECT_EQ(s.check(tick(250)), SeqResult::InOrder); // bookTicker IDs skip normally
}

TEST(SequenceTracker, RejectsDuplicate) {
    SequenceTracker s;
    s.check(tick(100));
    EXPECT_EQ(s.check(tick(100)), SeqResult::Duplicate);
}

TEST(SequenceTracker, RejectsOutOfOrder) {
    SequenceTracker s;
    s.check(tick(100));
    EXPECT_EQ(s.check(tick(99)), SeqResult::OutOfOrder);
}

TEST(SequenceTracker, RejectedTickDoesNotMoveBaseline) {
    SequenceTracker s;
    s.check(tick(100));
    s.check(tick(50));
    EXPECT_EQ(s.check(tick(60)), SeqResult::OutOfOrder);
    EXPECT_EQ(s.check(tick(101)), SeqResult::InOrder);
}

TEST(SequenceTracker, RejectsMissingSequence) {
    SequenceTracker s;
    EXPECT_EQ(s.check(tick(0)), SeqResult::MissingSequence);
    // Missing sequence must not establish a baseline
    EXPECT_EQ(s.check(tick(5)), SeqResult::SessionStart);
}

TEST(SequenceTracker, NewSessionResetsBaseline) {
    SequenceTracker s;
    s.check(tick(1000, 1));
    EXPECT_EQ(s.check(tick(10, 2)), SeqResult::SessionStart); // lower ID is fine after reconnect
    EXPECT_EQ(s.check(tick(11, 2)), SeqResult::InOrder);
    EXPECT_EQ(s.check(tick(10, 2)), SeqResult::OutOfOrder);
}

TEST(SequenceTracker, SessionCanSkipNumbers) {
    SequenceTracker s;
    s.check(tick(10, 1));
    EXPECT_EQ(s.check(tick(5, 4)), SeqResult::SessionStart);
}

TEST(SequenceTracker, RejectsTickFromOlderSession) {
    SequenceTracker s;
    s.check(tick(10, 2));
    EXPECT_EQ(s.check(tick(20, 1)), SeqResult::StaleSession);
}

TEST(SequenceTracker, TracksSymbolsIndependently) {
    SequenceTracker s;
    s.check(tick(100, 1, "BTCUSDT"));
    EXPECT_EQ(s.check(tick(5, 1, "ETHUSDT")), SeqResult::SessionStart);
    EXPECT_EQ(s.check(tick(101, 1, "BTCUSDT")), SeqResult::InOrder);
    EXPECT_EQ(s.check(tick(5, 1, "ETHUSDT")), SeqResult::Duplicate);
}

TEST(SequenceTracker, CountsAndSummary) {
    SequenceTracker s;
    s.check(tick(1));     // session_start
    s.check(tick(2));     // in_order
    s.check(tick(2));     // duplicate
    s.check(tick(1));     // out_of_order
    s.check(tick(0));     // missing
    s.check(tick(9, 2));  // session_start
    s.check(tick(50, 1)); // stale_session
    EXPECT_EQ(s.count(SeqResult::SessionStart), 2u);
    EXPECT_EQ(s.count(SeqResult::InOrder), 1u);
    EXPECT_EQ(s.rejected_total(), 4u);
    const std::string sum = s.summary();
    for (std::size_t i = 0; i < static_cast<std::size_t>(SeqResult::Count); ++i) {
        EXPECT_NE(sum.find(seq_result_to_str(static_cast<SeqResult>(i))), std::string::npos);
    }
}
