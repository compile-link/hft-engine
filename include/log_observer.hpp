#pragma once

#include "pipeline.hpp"
#include <chrono>

// Pipeline reporting for normal runs: signals and latency to the log, rejects and feed-health
// stops to the error stream. Every stream write is rate limited to one per second.
//
// Each rate-limit clock is touched by a single thread (see PipelineObserver), so no locking.
class LogObserver final : public PipelineObserver {
  public:
    void on_message(const hft::StrategySignal& sig, const MessageSpans& spans) override;
    void on_validation_reject(const hft::TopOfBook& tob, TobReject reason, uint64_t total) override;
    void on_sequence_reject(const hft::TopOfBook& tob, SeqResult reason, uint64_t total) override;
    void on_session_start(const hft::TopOfBook& tob) override;
    void on_feed_stop(FeedStopReason reason, const FeedHealthSnapshot& snap) override;

  private:
    using clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds log_interval{1};

    bool due(clock::time_point& last) const;

    clock::time_point last_signal_log_{};  // process thread
    clock::time_point last_reject_log_{};  // ingest thread, shared by both reject kinds
    clock::time_point last_session_log_{}; // ingest thread
};
