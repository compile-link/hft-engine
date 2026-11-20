#pragma once

#include "market_data.pb.h"
#include "strategy.hpp"
#include <string_view>

// Spread strategy: quotes both sides when ask - bid >= the threshold, holds below it, and
// cancels on any unusable market state. Implemented in Rust (strategy_engine, statically
// linked) behind the Strategy interface; the FFI call lives in spread_strategy.cpp.
class SpreadStrategy final : public Strategy {
  public:
    Decision decide(std::string_view payload) override;
};
