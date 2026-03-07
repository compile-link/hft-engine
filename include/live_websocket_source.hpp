#pragma once

#include "market_data.pb.h"
#include "market_data_source.hpp"
#include <cstddef>

class LiveWebSocketSource : public MarketDataSource {
  public:
    bool next(hft::TopOfBook& out) override;

  private:
    std::size_t idx_ = 0;
};
