#pragma once

#include "market_data.pb.h"
#include <string_view>

// One strategy decision. reason is a short machine-readable tag that ends up in
// StrategySignal.reason; it must point at storage that outlives the call (a string literal
// or a member), so building it costs nothing on the hot path.
struct Decision {
    hft::QuoteAction action = hft::QUOTE_ACTION_UNSPECIFIED;
    std::string_view reason;
};

// Engine-side contract for a strategy: one decision per accepted market-data message.
// Called on the process thread only, inside the strat span, so implementations need no
// synchronisation and should not block.
class Strategy {
  public:
    virtual ~Strategy() = default;

    // payload: a serialized hft::TopOfBook
    virtual Decision decide(std::string_view payload) = 0;
};
