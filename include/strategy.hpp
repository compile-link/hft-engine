#pragma once

#include "market_data.pb.h"
#include <string_view>

// Engine-side contract for a strategy: one decision per accepted market-data message.
// Called on the process thread only, inside the strat span, so implementations need no
// synchronisation and should not block.
class Strategy {
  public:
    virtual ~Strategy() = default;

    // payload: a serialized hft::TopOfBook
    virtual hft::QuoteAction decide(std::string_view payload) = 0;
};
