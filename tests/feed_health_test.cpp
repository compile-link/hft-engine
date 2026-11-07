#include "feed_health.hpp"
#include <gtest/gtest.h>
#include <string>

namespace {
    constexpr uint64_t k_ms = 1'000'000;
    constexpr uint64_t k_start = 1'000'000 * k_ms; // arbitrary non-zero steady-clock origin

    FeedHealthConfig make_cfg() {
        FeedHealthConfig c;
        c.max_no_valid = std::chrono::milliseconds(10'000);
        c.startup_grace = std::chrono::milliseconds(15'000);
        return c;
    }

    FeedHealthSnapshot at(uint64_t now_offset_ms) {
        FeedHealthSnapshot s;
        s.start_ns = k_start;
        s.now_ns = k_start + now_offset_ms * k_ms;
        return s;
    }

    uint64_t t(uint64_t offset_ms) {
        return k_start + offset_ms * k_ms;
    }
} // namespace

// --- Startup grace ---

TEST(FeedHealth, HealthyDuringStartupGrace) {
    EXPECT_EQ(evaluate_feed_health(make_cfg(), at(15'000)), FeedStopReason::None);
}

TEST(FeedHealth, DeadFeedWhenNothingArrivesAfterGrace) {
    EXPECT_EQ(evaluate_feed_health(make_cfg(), at(15'001)), FeedStopReason::DeadFeed);
}

TEST(FeedHealth, StartupUsesGraceNotMaxNoValid) {
    // 12s in, no valid tick yet: past max_no_valid but within startup_grace
    EXPECT_EQ(evaluate_feed_health(make_cfg(), at(12'000)), FeedStopReason::None);
}

// --- Steady state ---

TEST(FeedHealth, HealthyWhileValidTicksArrive) {
    auto s = at(60'000);
    s.last_valid_ns = t(59'000);
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::None);
}

TEST(FeedHealth, HealthyAtExactlyMaxNoValid) {
    auto s = at(60'000);
    s.last_valid_ns = t(50'000);
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::None);
}

TEST(FeedHealth, StopsJustPastMaxNoValid) {
    auto s = at(60'001);
    s.last_valid_ns = t(50'000);
    EXPECT_NE(evaluate_feed_health(make_cfg(), s), FeedStopReason::None);
}

TEST(FeedHealth, ClockBehindReferenceIsHealthy) {
    auto s = at(1'000);
    s.last_valid_ns = t(2'000); // valid stamped after the snapshot clock read
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::None);
}

// --- Classification ---

TEST(FeedHealth, DeadFeedWhenNoFramesInWindow) {
    auto s = at(60'000);
    s.last_valid_ns = t(30'000);
    s.last_frame_ns = t(30'000); // connection went silent together with data
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::DeadFeed);
}

TEST(FeedHealth, StaleFeedWhenOnlyHeartbeatsArrive) {
    auto s = at(60'000);
    s.last_valid_ns = t(30'000);
    s.last_frame_ns = t(55'000); // ping within window, no data
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::StaleFeed);
}

TEST(FeedHealth, InvalidDataWhenTicksRejected) {
    auto s = at(60'000);
    s.last_valid_ns = t(30'000);
    s.last_rejected_ns = t(59'000);
    s.last_frame_ns = t(59'000);
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::InvalidData);
}

TEST(FeedHealth, InvalidDataWhenParseErrors) {
    auto s = at(60'000);
    s.last_valid_ns = t(30'000);
    s.last_parse_err_ns = t(59'000);
    s.last_frame_ns = t(59'000);
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::InvalidData);
}

TEST(FeedHealth, InvalidDataAtStartupForPersistentSymbolMismatch) {
    // Every tick rejected since connect: e.g. symbol_mismatch
    auto s = at(16'000);
    s.last_rejected_ns = t(15'900);
    s.last_frame_ns = t(15'900);
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::InvalidData);
}

TEST(FeedHealth, OldRejectsOutsideWindowDoNotCount) {
    auto s = at(60'000);
    s.last_valid_ns = t(30'000);
    s.last_rejected_ns = t(40'000); // older than now - window
    EXPECT_EQ(evaluate_feed_health(make_cfg(), s), FeedStopReason::DeadFeed);
}

// --- Monitor wiring and formatting ---

TEST(FeedHealthMonitor, SnapshotReflectsRecordedTicks) {
    FeedHealthMonitor m(make_cfg(), k_start);
    m.on_valid(t(100));
    m.on_rejected(t(200));
    const auto s = m.snapshot(t(300), t(250), t(150));
    EXPECT_EQ(s.start_ns, k_start);
    EXPECT_EQ(s.last_valid_ns, t(100));
    EXPECT_EQ(s.last_rejected_ns, t(200));
    EXPECT_EQ(s.last_frame_ns, t(250));
    EXPECT_EQ(s.last_parse_err_ns, t(150));
    EXPECT_EQ(m.evaluate(s), FeedStopReason::None);
}

TEST(FeedHealth, DescribeReportsAgesAndNever) {
    auto s = at(5'000);
    s.last_valid_ns = t(4'000);
    const std::string d = describe_feed_health(s);
    EXPECT_NE(d.find("valid_age_ms=1000"), std::string::npos);
    EXPECT_NE(d.find("frame_age_ms=-1"), std::string::npos);
    EXPECT_NE(d.find("uptime_ms=5000"), std::string::npos);
}

TEST(FeedHealth, ReasonNames) {
    EXPECT_STREQ(feed_stop_reason_to_str(FeedStopReason::None), "none");
    EXPECT_STREQ(feed_stop_reason_to_str(FeedStopReason::DeadFeed), "dead_feed");
    EXPECT_STREQ(feed_stop_reason_to_str(FeedStopReason::InvalidData), "invalid_data");
    EXPECT_STREQ(feed_stop_reason_to_str(FeedStopReason::StaleFeed), "stale_feed");
}
