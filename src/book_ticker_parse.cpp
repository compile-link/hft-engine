#include "book_ticker_parse.hpp"
#include "decimal_parse.hpp"
#include <nlohmann/json.hpp>

namespace book_ticker {
    bool parse(const std::string& msg, uint64_t recv_ts_ns, hft::TopOfBook& out) {
        using nlohmann::json;
        try {
            const json j = json::parse(msg);
            if (!j.is_object()) {
                return false;
            }
            // get<uint64_t> would silently convert negative or fractional numbers
            const json& u = j.at("u");
            if (!u.is_number_unsigned()) {
                return false;
            }

            const std::string symbol = j.at("s").get<std::string>();                           // symbol
            const double bid_px = decimal_parse::parse_decimal(j.at("b").get<std::string>());  // best bid price
            const double bid_qty = decimal_parse::parse_decimal(j.at("B").get<std::string>()); // best bid qty
            const double ask_px = decimal_parse::parse_decimal(j.at("a").get<std::string>());  // best ask price
            const double ask_qty = decimal_parse::parse_decimal(j.at("A").get<std::string>()); // best ask qty
            const uint64_t update_id = u.get<uint64_t>();                                      // order book updateId

            out.Clear();
            out.set_symbol(symbol);
            out.set_bid_px(bid_px);
            out.set_bid_qty(bid_qty);
            out.set_ask_px(ask_px);
            out.set_ask_qty(ask_qty);
            out.set_exchange_ts_ns(0); // bookTicker carries no event time
            out.set_recv_ts_ns(recv_ts_ns);
            out.set_sequence(update_id);
            return true;
        } catch (...) {
            return false;
        }
    }
} // namespace book_ticker
