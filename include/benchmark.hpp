#pragma once

#include "feed_health.hpp"
#include "market_data_source.hpp"
#include "pipeline.hpp"
#include <chrono>
#include <cstdint>

struct BenchmarkResult {
    bool validate = false;
    bool health = false;
    bool watchdog = false;
    FeedStopReason watchdog_stop = FeedStopReason::None; // set if the watchdog ended the run early
    uint64_t rejected = 0;
    uint64_t measured_msgs = 0;
    double measured_seconds = 0.0;
    double throughput_msgs_per_sec = 0.0;
    uint64_t drops = 0;
    uint64_t serialize_errs = 0;
    uint64_t e2e_p50_ns = 0, e2e_p99_ns = 0, e2e_p999_ns = 0, e2e_max_ns = 0;
    uint64_t strat_p50_ns = 0, strat_p99_ns = 0, strat_p999_ns = 0, strat_max_ns = 0;

    void print() const;
};

// Drives the shared Pipeline over a source and samples spans inside a fixed measurement window
class Benchmark {
  public:
    static BenchmarkResult run(MarketDataSource& src, const PipelineConfig& cfg);

  private:
    static constexpr std::chrono::seconds warmup_seconds{3};
    static constexpr std::chrono::seconds measure_seconds{20};
};
