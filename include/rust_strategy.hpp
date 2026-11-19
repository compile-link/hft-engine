#pragma once

#include "market_data.pb.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

// Thin wrapper over the statically linked Rust strategy's C ABI entry point.
// Keeps the raw FFI call and its pointer cast in one place; behavior is unchanged.
class RustStrategy final {
  public:
    // payload: a serialized hft::TopOfBook
    hft::QuoteAction decide(std::string_view payload) const {
        return static_cast<hft::QuoteAction>(
            rust_decide(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
    }
};
