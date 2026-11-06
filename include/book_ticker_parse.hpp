#pragma once

#include "market_data.pb.h"
#include <cstdint>
#include <string>

namespace book_ticker {
    // Parse a Binance bookTicker message into `out`.
    // Format only: required fields present with the right types, prices/quantities as strict
    // decimal strings, update ID as an unsigned integer. Value checks are left to TobValidator.
    // On success `out` is cleared and fully rewritten (no fields carried over from reuse);
    // on failure `out` is left unchanged.
    bool parse(const std::string& msg, uint64_t recv_ts_ns, hft::TopOfBook& out);
} // namespace book_ticker
