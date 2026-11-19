#include "market_data.pb.h"
#include "spread_strategy.hpp"
#include "strategy.hpp"
#include <gtest/gtest.h>
#include <string>

// SpreadStrategy through the Strategy interface. The threshold is never set in this binary,
// so the Rust default (0.0000007) applies; the decision rules themselves are covered in Rust.
namespace {
    std::string serialize(double bid, double ask, hft::FeedStatus status) {
        hft::TopOfBook tob;
        tob.set_symbol("BTCUSDT");
        tob.set_bid_px(bid);
        tob.set_bid_qty(1.0);
        tob.set_ask_px(ask);
        tob.set_ask_qty(1.0);
        tob.set_feed_status(status);
        return tob.SerializeAsString();
    }
} // namespace

TEST(SpreadStrategy, DecidesThroughInterface) {
    SpreadStrategy impl;
    Strategy& s = impl;
    EXPECT_EQ(s.decide(serialize(100.0, 101.0, hft::FEED_OK)), hft::QUOTE_BOTH);
    EXPECT_EQ(s.decide(serialize(100.0, 101.0, hft::FEED_HALTED)), hft::CANCEL_ALL);
    EXPECT_EQ(s.decide(serialize(101.0, 100.0, hft::FEED_OK)), hft::CANCEL_ALL); // crossed
}

TEST(SpreadStrategy, EmptyPayloadCancels) {
    SpreadStrategy impl;
    Strategy& s = impl;
    EXPECT_EQ(s.decide(std::string_view{}), hft::CANCEL_ALL);
}
