#pragma once

#include "market_data.pb.h"

class MarketDataSource {
  public:
    virtual bool next(hft::TopOfBook& out) = 0;
    virtual ~MarketDataSource() = default;
};
