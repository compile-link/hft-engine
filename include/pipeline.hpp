#pragma once

#include "feed_health.hpp"
#include "market_data.pb.h"
#include "market_data_source.hpp"
#include "sequence_tracker.hpp"
#include "tob_validator.hpp"
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

// Hooks for measurement and reporting. Calls happen outside both spans, so they do not affect
// the measured latency. Each hook documents the thread it is called on; an implementation that
// only touches per-hook state needs no synchronisation.
class PipelineObserver {
  public:
    virtual ~PipelineObserver() = default;

    // Process thread: one accepted message reached a decision
    virtual void on_message(const hft::StrategySignal& sig, const MessageSpans& spans) = 0;

    // Ingest thread
    virtual void on_drop(uint64_t now_ns) { (void)now_ns; } // ring buffer full
    virtual void on_validation_reject(const hft::TopOfBook& tob, TobReject reason, uint64_t total) {
        (void)tob;
        (void)reason;
        (void)total;
    }
    virtual void on_sequence_reject(const hft::TopOfBook& tob, SeqResult reason, uint64_t total) {
        (void)tob;
        (void)reason;
        (void)total;
    }
    virtual void on_session_start(const hft::TopOfBook& tob) { (void)tob; }

    // Watchdog thread: health tripped; the source is stopped right after this returns
    virtual void on_feed_stop(FeedStopReason reason, const FeedHealthSnapshot& snap) {
        (void)reason;
        (void)snap;
    }
};

struct PipelineStats {
    uint64_t drops = 0;
    uint64_t serialize_errs = 0;
    uint64_t rejected = 0;
    std::string validator_summary;
    std::string sequence_summary;
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
