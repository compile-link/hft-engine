#pragma once

#include "market_data.pb.h"
#include "market_data_source.hpp"
#include <string>
#include <vector>
#include <cstddef>

class ReplaySource : public MarketDataSource {
  public:
    // TODO: Add path for csv data loading, helpful for tests
    // explicit ReplaySource(const std::string& path): path_(path) {};
    explicit ReplaySource() {};
    bool next(hft::TopOfBook& out) override;

  private:
    std::size_t idx_ = 0;
};
