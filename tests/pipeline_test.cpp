#include "pipeline.hpp"
#include "replay_source.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

namespace {
    class RecordingObserver final : public PipelineObserver {
      public:
        void on_message(const MessageSpans& s) override { spans.push_back(s); }
        std::vector<MessageSpans> spans;
    };

    // One pass over the replay stub ticks, no pacing, then the source ends
    ReplayConfig single_pass() {
        ReplayConfig cfg;
        cfg.loop = false;
        cfg.sleep_per_tick = std::chrono::milliseconds(0);
        cfg.max_runtime = std::nullopt;
        return cfg;
    }
} // namespace

TEST(Pipeline, DeliversEveryTickWithOrderedSpans) {
    for (const bool checks : {false, true}) {
        SCOPED_TRACE(checks ? "checks on" : "checks off");
        ReplaySource src(single_pass());
        RecordingObserver obs;
        PipelineConfig cfg = PipelineConfig::checks(checks);

        const PipelineStats stats = Pipeline(cfg, &obs).run(src);

        EXPECT_EQ(stats.rejected, 0u);
        EXPECT_EQ(stats.drops, 0u);
        EXPECT_EQ(stats.stop_reason, FeedStopReason::None);
        ASSERT_FALSE(obs.spans.empty());
        for (const MessageSpans& s : obs.spans) {
            EXPECT_GT(s.recv_ts_ns, 0u);
            EXPECT_LE(s.recv_ts_ns, s.t0_ns);
            EXPECT_LE(s.t0_ns, s.strat_end_ns);
            EXPECT_LE(s.strat_end_ns, s.t1_ns);
        }
    }
}

TEST(Pipeline, RejectsWatchdogWithoutHealth) {
    PipelineConfig cfg = PipelineConfig::checks(false);
    cfg.watchdog = true;
    EXPECT_THROW(Pipeline(cfg, nullptr), std::invalid_argument);
}
