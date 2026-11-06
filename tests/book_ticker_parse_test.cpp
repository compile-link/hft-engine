#include "book_ticker_parse.hpp"
#include <cmath>
#include <gtest/gtest.h>
#include <string>

using book_ticker::parse;

namespace {
    const std::string k_valid =
        R"({"u":400900217,"s":"BNBUSDT","b":"25.35190000","B":"31.21000000","a":"25.36520000","A":"40.66000000"})";

    // Build a message from the valid one with a single field's raw JSON value replaced
    std::string with(const std::string& key, const std::string& raw_value) {
        const std::string tag = "\"" + key + "\":";
        std::string m = k_valid;
        const auto start = m.find(tag) + tag.size();
        const auto end = m.find_first_of(",}", start);
        return m.replace(start, end - start, raw_value);
    }

    std::string without(const std::string& key) {
        const std::string tag = "\"" + key + "\":";
        std::string m = k_valid;
        const auto start = m.find(tag);
        auto end = m.find_first_of(",}", start);
        if (m[end] == ',') {
            ++end;
        }
        return m.erase(start, end - start);
    }
} // namespace

// --- Accepted ---

TEST(BookTickerParse, ParsesAllFields) {
    hft::TopOfBook t;
    ASSERT_TRUE(parse(k_valid, 12345, t));
    EXPECT_EQ(t.symbol(), "BNBUSDT");
    EXPECT_DOUBLE_EQ(t.bid_px(), 25.3519);
    EXPECT_DOUBLE_EQ(t.bid_qty(), 31.21);
    EXPECT_DOUBLE_EQ(t.ask_px(), 25.3652);
    EXPECT_DOUBLE_EQ(t.ask_qty(), 40.66);
    EXPECT_EQ(t.sequence(), 400900217u);
    EXPECT_EQ(t.recv_ts_ns(), 12345u);
    EXPECT_EQ(t.exchange_ts_ns(), 0u);
}

TEST(BookTickerParse, IgnoresUnknownFields) {
    hft::TopOfBook t;
    EXPECT_TRUE(parse(with("A", R"("40.66000000","E":1700000000000,"x":null)"), 1, t));
}

TEST(BookTickerParse, ClearsFieldsFromReusedMessage) {
    hft::TopOfBook t;
    t.set_session(7);
    t.set_feed_status(hft::FEED_HALTED);
    ASSERT_TRUE(parse(k_valid, 1, t));
    EXPECT_EQ(t.session(), 0u);
    EXPECT_EQ(t.feed_status(), hft::FEED_STATUS_UNSPECIFIED);
}

TEST(BookTickerParse, LeavesValueChecksToValidator) {
    hft::TopOfBook t;
    EXPECT_TRUE(parse(with("b", R"("0")"), 1, t));      // non-positive
    EXPECT_TRUE(parse(with("b", R"("nan")"), 1, t));    // non-finite
    EXPECT_TRUE(std::isnan(t.bid_px()));
    EXPECT_TRUE(parse(with("a", R"("1.0")"), 1, t));    // crossed
}

// --- Rejected: structure ---

TEST(BookTickerParse, RejectsMalformedJson) {
    hft::TopOfBook t;
    EXPECT_FALSE(parse("", 1, t));
    EXPECT_FALSE(parse("{", 1, t));
    EXPECT_FALSE(parse(k_valid.substr(0, k_valid.size() - 1), 1, t));
    EXPECT_FALSE(parse("not json", 1, t));
}

TEST(BookTickerParse, RejectsNonObject) {
    hft::TopOfBook t;
    EXPECT_FALSE(parse("[]", 1, t));
    EXPECT_FALSE(parse("42", 1, t));
    EXPECT_FALSE(parse("null", 1, t));
}

TEST(BookTickerParse, RejectsEachMissingField) {
    for (const char* key : {"u", "s", "b", "B", "a", "A"}) {
        hft::TopOfBook t;
        EXPECT_FALSE(parse(without(key), 1, t)) << "missing " << key;
    }
}

TEST(BookTickerParse, RejectsSubscriptionAck) {
    hft::TopOfBook t;
    EXPECT_FALSE(parse(R"({"result":null,"id":1})", 1, t));
}

// --- Rejected: types ---

TEST(BookTickerParse, RejectsNumericPriceInsteadOfString) {
    hft::TopOfBook t;
    EXPECT_FALSE(parse(with("b", "25.35"), 1, t));
    EXPECT_FALSE(parse(with("A", "40"), 1, t));
}

TEST(BookTickerParse, RejectsNonStringSymbol) {
    hft::TopOfBook t;
    EXPECT_FALSE(parse(with("s", "123"), 1, t));
    EXPECT_FALSE(parse(with("s", "null"), 1, t));
}

TEST(BookTickerParse, RejectsBadDecimalStrings) {
    hft::TopOfBook t;
    EXPECT_FALSE(parse(with("b", R"("25.35abc")"), 1, t));
    EXPECT_FALSE(parse(with("B", R"("")"), 1, t));
    EXPECT_FALSE(parse(with("a", R"("25,36")"), 1, t));
}

TEST(BookTickerParse, RejectsNonUnsignedUpdateId) {
    hft::TopOfBook t;
    EXPECT_FALSE(parse(with("u", "-1"), 1, t));
    EXPECT_FALSE(parse(with("u", "1.5"), 1, t));
    EXPECT_FALSE(parse(with("u", R"("400900217")"), 1, t));
    EXPECT_FALSE(parse(with("u", "null"), 1, t));
}

TEST(BookTickerParse, LeavesOutputUnchangedOnFailure) {
    hft::TopOfBook t;
    ASSERT_TRUE(parse(k_valid, 99, t));
    EXPECT_FALSE(parse(with("a", R"("bad")"), 1, t));
    EXPECT_EQ(t.symbol(), "BNBUSDT");
    EXPECT_DOUBLE_EQ(t.ask_px(), 25.3652);
    EXPECT_EQ(t.recv_ts_ns(), 99u);
}
