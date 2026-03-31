#pragma once

#include "market_data_source.hpp"
#include <chrono>
#include <cstdint>

struct BenchmarkResult {
    uint64_t measured_msgs = 0;
    double measured_seconds = 0.0;
    double throughput_msgs_per_sec = 0.0;
    uint64_t drops = 0;
    uint64_t e2e_p50_ns = 0, e2e_p99_ns = 0, e2e_p999_ns = 0, e2e_max_ns = 0;
    uint64_t strat_p50_ns = 0, strat_p99_ns = 0, strat_p999_ns = 0, strat_max_ns = 0;

    void print() const;
};

class Benchmark {
  public:
    static BenchmarkResult run(MarketDataSource& src);

  private:
    static constexpr std::chrono::seconds warmup_seconds{3};
    static constexpr std::chrono::seconds measure_seconds{20};
};
