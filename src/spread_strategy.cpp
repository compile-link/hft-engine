#include "spread_strategy.hpp"
#include <cstddef>
#include <cstdint>

// Rust entry point (strategy_engine/src/lib.rs). The pointer/length contract is documented there:
// a null pointer or zero length yields CancelAll.
extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

hft::QuoteAction SpreadStrategy::decide(std::string_view payload) {
    return static_cast<hft::QuoteAction>(
        rust_decide(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
}
