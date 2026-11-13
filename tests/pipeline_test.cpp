#include "pipeline.hpp"
#include "replay_source.hpp"
#include "time_utils.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
    class RecordingObserver : public PipelineObserver {
      public:
        void on_message(const hft::StrategySignal& sig, const MessageSpans& s) override {
            spans.push_back(s);
            signals.push_back(sig.action());
        }
        std::vector<MessageSpans> spans;
        std::vector<hft::QuoteAction> signals;
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
        if (checks) {
            cfg.expected_symbol = ReplaySource::k_symbol;
        }

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

namespace {
    // Keeps producing ticks the validator rejects, so no valid tick ever reaches the queue
    class RejectedTickSource final : public MarketDataSource {
      public:
        bool next(hft::TopOfBook& out) override {
            while (!stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                activity_.last_frame_ns.store(time_utils::now_ns(), std::memory_order_relaxed);
                out.Clear();
                out.set_symbol(""); // empty_symbol: rejected by TobValidator
                out.set_recv_ts_ns(time_utils::now_ns());
                return true;
            }
            return false;
        }
    };

    class StopRecordingObserver final : public RecordingObserver {
      public:
        void on_feed_stop(FeedStopReason reason, const FeedHealthSnapshot&) override { stop_reason = reason; }
        FeedStopReason stop_reason = FeedStopReason::None;
    };
} // namespace

// The watchdog stops an unhealthy feed, and the strategy gets a halt tick so it stops quoting
TEST(Pipeline, WatchdogStopsUnhealthyFeedAndEmitsHaltTick) {
    RejectedTickSource src;
    StopRecordingObserver obs;
    PipelineConfig cfg;
    cfg.expected_symbol = "BTCUSDT";
    cfg.health_cfg.startup_grace = std::chrono::milliseconds(200);
    cfg.health_cfg.max_no_valid = std::chrono::milliseconds(200);

    const PipelineStats stats = Pipeline(cfg, &obs).run(src);

    EXPECT_EQ(stats.stop_reason, FeedStopReason::InvalidData);
    EXPECT_EQ(obs.stop_reason, FeedStopReason::InvalidData);
    EXPECT_GT(stats.rejected, 0u);
    ASSERT_EQ(obs.signals.size(), 1u); // only the halt tick is queued
    EXPECT_EQ(obs.signals.front(), hft::CANCEL_ALL);
}
