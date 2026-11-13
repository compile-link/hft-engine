#include "replay_source.hpp"
#include "time_utils.hpp"
#include <chrono>
#include <gtest/gtest.h>

// recv_ts_ns marks when the tick reaches the engine. Replay pacing simulates the
// wait for the next tick, so it must fall before the stamp, not inside e2e.
TEST(ReplaySource, RecvTimestampTakenAfterPacingSleep) {
    ReplayConfig cfg;
    cfg.sleep_per_tick = std::chrono::milliseconds(20);
    cfg.max_runtime = std::nullopt;
    ReplaySource src(cfg);

    hft::TopOfBook tob;
    const uint64_t before_ns = time_utils::now_ns();
    ASSERT_TRUE(src.next(tob));
    const uint64_t after_ns = time_utils::now_ns();

    EXPECT_GE(tob.recv_ts_ns(), before_ns + 20'000'000u);
    EXPECT_LE(tob.recv_ts_ns(), after_ns);
}

namespace {
    ReplayConfig unpaced(bool loop) {
        ReplayConfig cfg;
        cfg.loop = loop;
        cfg.sleep_per_tick = std::chrono::milliseconds(0);
        cfg.max_runtime = std::nullopt;
        return cfg;
    }
} // namespace

// Replay goes through the same bookTicker parse as live, so ticks carry what parse produces
TEST(ReplaySource, EmitsParsedBookTickerFrames) {
    ReplaySource src(unpaced(false));
    hft::TopOfBook tob;
    uint64_t prev_seq = 0;
    int n = 0;
    while (src.next(tob)) {
        EXPECT_EQ(tob.symbol(), ReplaySource::k_symbol);
        EXPECT_GT(tob.bid_px(), 0.0);
        EXPECT_GT(tob.ask_px(), tob.bid_px());
        EXPECT_EQ(tob.exchange_ts_ns(), 0u); // bookTicker carries no event time
        EXPECT_GT(tob.recv_ts_ns(), 0u);
        EXPECT_GT(tob.sequence(), prev_seq);
        EXPECT_EQ(tob.session(), 1u);
        prev_seq = tob.sequence();
        ++n;
    }
    EXPECT_GT(n, 0);
    EXPECT_GT(src.activity().last_frame_ns.load(), 0u);
    EXPECT_EQ(src.activity().last_parse_err_ns.load(), 0u);
}

TEST(ReplaySource, LoopStartsNewSession) {
    ReplaySource once(unpaced(false));
    hft::TopOfBook tob;
    size_t pass_len = 0;
    while (once.next(tob)) {
        ++pass_len;
    }

    ReplaySource looping(unpaced(true));
    for (size_t i = 0; i <= pass_len; ++i) {
        ASSERT_TRUE(looping.next(tob));
    }
    EXPECT_EQ(tob.session(), 2u);
}
