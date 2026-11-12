#pragma once

#include "feed_health.hpp"
#include "market_data_source.hpp"
#include <cstdint>
#include <optional>
#include <string>

// Engine checks on the ingest path. Each is a separate switch; callers may set them together.
struct PipelineConfig {
    bool validate = true; // TobValidator + SequenceTracker
    bool health = true;   // FeedHealthMonitor updates per message
    bool watchdog = true; // watchdog thread evaluating health; requires health
    std::optional<std::string> expected_symbol = std::nullopt;
    FeedHealthConfig health_cfg{};

    static PipelineConfig checks(bool on) {
        PipelineConfig c;
        c.validate = c.health = c.watchdog = on;
        return c;
    }
};

// Span timestamps for one message, steady-clock ns. See README "Latency Instrumentation".
struct MessageSpans {
    uint64_t recv_ts_ns = 0;   // e2e start (0 if the source did not stamp it)
    uint64_t t0_ns = 0;        // strat start
    uint64_t strat_end_ns = 0; // strat stop
    uint64_t t1_ns = 0;        // e2e stop: signal built
};

// Measurement hooks. Calls happen outside both spans, so they do not affect the measured latency.
class PipelineObserver {
  public:
    virtual ~PipelineObserver() = default;
    virtual void on_message(const MessageSpans& spans) = 0; // process thread
    virtual void on_drop(uint64_t now_ns) { (void)now_ns; } // ingest thread, ring buffer full
};

struct PipelineStats {
    uint64_t drops = 0;
    uint64_t serialize_errs = 0;
    uint64_t rejected = 0;
    FeedStopReason stop_reason = FeedStopReason::None; // set if the watchdog stopped the source
};

// One implementation of ingest -> checks -> ring -> serialize -> decide -> signal.
// Modes differ only in what is plugged in (source, config, observer), never in the path itself.
class Pipeline final {
  public:
    explicit Pipeline(PipelineConfig cfg, PipelineObserver* observer = nullptr);

    // Blocks until the source ends or is stopped (request_stop() or the watchdog)
    PipelineStats run(MarketDataSource& src);

  private:
    PipelineConfig cfg_;
    PipelineObserver* observer_;
};
