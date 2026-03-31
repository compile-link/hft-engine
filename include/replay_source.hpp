#pragma once

#include "market_data.pb.h"
#include "market_data_source.hpp"
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

class ReplaySource : public MarketDataSource {
  public:
    // TODO: Add path for csv data loading, helpful for tests
    // explicit ReplaySource(const std::string& path): path_(path) {};
    explicit ReplaySource() : k_runtime(k_default_runtime), replay_end(std::chrono::steady_clock::now() + k_runtime) {};
    bool next(hft::TopOfBook& out) override;

  private:
    const std::chrono::seconds k_runtime;
    static constexpr std::chrono::seconds k_default_runtime{100};
    std::size_t idx_ = 0;
    std::chrono::steady_clock::time_point replay_end;
};
