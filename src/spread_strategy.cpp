#include "spread_strategy.hpp"
#include <cstddef>
#include <cstdint>

// Rust entry point (strategy_engine/src/lib.rs). The pointer/length contract is documented there:
// a null pointer or zero length yields CancelAll.
extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

Decision SpreadStrategy::decide(std::string_view payload) {
    const auto action = static_cast<hft::QuoteAction>(
        rust_decide(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
    // Every outcome, including CancelAll, comes from the spread rule and its sanity checks
    return Decision{action, "spread_cond"};
}
