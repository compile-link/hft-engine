#pragma once

#include "market_data.pb.h"
#include "strategy.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

// Spread strategy: quotes both sides when ask - bid >= the threshold, holds below it, and
// cancels on any unusable market state. Implemented in Rust (strategy_engine, statically
// linked) behind the Strategy interface.
// Keeps the raw FFI call and its pointer cast in one place.
class SpreadStrategy final : public Strategy {
  public:
    hft::QuoteAction decide(std::string_view payload) override {
        return static_cast<hft::QuoteAction>(
            rust_decide(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
    }
};
