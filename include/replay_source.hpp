#pragma once

#include "market_data.pb.h"
#include "market_data_source.hpp"
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct ReplayConfig {
    bool loop = true;
    std::chrono::milliseconds sleep_per_tick{10};
    std::optional<std::chrono::steady_clock::duration> max_runtime{std::chrono::seconds(100)};
};

class ReplaySource : public MarketDataSource {
  public:
    // TODO: Add path for csv data loading, helpful for tests
    explicit ReplaySource(ReplayConfig cfg);
    bool next(hft::TopOfBook& out) override;

  private:
    std::size_t idx_ = 0;
    std::optional<std::chrono::steady_clock::time_point> replay_end_;
    ReplayConfig cfg_;
};
