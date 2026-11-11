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
