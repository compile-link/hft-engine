#include "tob_validator.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <string>

namespace {
    constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double k_inf = std::numeric_limits<double>::infinity();

    hft::TopOfBook make_tob(
        const std::string& symbol = "BTCUSDT",
        double bid_px = 100.0,
        double ask_px = 100.5,
        double bid_qty = 1.0,
        double ask_qty = 2.0) {
        hft::TopOfBook t;
        t.set_symbol(symbol);
        t.set_bid_px(bid_px);
        t.set_ask_px(ask_px);
        t.set_bid_qty(bid_qty);
        t.set_ask_qty(ask_qty);
        t.set_sequence(1);
        return t;
    }
} // namespace

// --- Accepted ---

TEST(TobValidator, AcceptsWellFormedTick) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob()), TobReject::None);
    EXPECT_EQ(v.accepted(), 1u);
    EXPECT_EQ(v.rejected_total(), 0u);
}

TEST(TobValidator, AcceptsMinimalPositiveSpread) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.00000001)), TobReject::None);
}

TEST(TobValidator, AcceptsAnySymbolWhenNoneExpected) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("ETHUSDT")), TobReject::None);
    EXPECT_EQ(v.validate(make_tob("dashbtc")), TobReject::None);
}

TEST(TobValidator, SymbolMatchIsCaseInsensitive) {
    TobValidator v(std::string("dashbtc"));
    EXPECT_EQ(v.validate(make_tob("DASHBTC")), TobReject::None);
    EXPECT_EQ(v.validate(make_tob("dashbtc")), TobReject::None);
    EXPECT_EQ(v.validate(make_tob("DashBtc")), TobReject::None);
}

// --- Symbol ---

TEST(TobValidator, RejectsEmptySymbol) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("")), TobReject::EmptySymbol);
}

TEST(TobValidator, RejectsEmptySymbolEvenWhenExpectedSet) {
    TobValidator v(std::string("btcusdt"));
    EXPECT_EQ(v.validate(make_tob("")), TobReject::EmptySymbol);
}

TEST(TobValidator, RejectsSymbolMismatch) {
    TobValidator v(std::string("btcusdt"));
    EXPECT_EQ(v.validate(make_tob("ETHUSDT")), TobReject::SymbolMismatch);
    EXPECT_EQ(v.validate(make_tob("BTCUSD")), TobReject::SymbolMismatch);  // prefix
    EXPECT_EQ(v.validate(make_tob("BTCUSDTX")), TobReject::SymbolMismatch); // longer
}

// --- Prices ---

TEST(TobValidator, RejectsNanPrice) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", k_nan, 100.5)), TobReject::NonFinitePrice);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, k_nan)), TobReject::NonFinitePrice);
}

TEST(TobValidator, RejectsInfinitePrice) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", k_inf, 100.5)), TobReject::NonFinitePrice);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", -k_inf, 100.5)), TobReject::NonFinitePrice);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, k_inf)), TobReject::NonFinitePrice);
}

TEST(TobValidator, RejectsZeroOrNegativePrice) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 0.0, 100.5)), TobReject::NonPositivePrice);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", -1.0, 100.5)), TobReject::NonPositivePrice);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 0.0)), TobReject::NonPositivePrice);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, -0.5)), TobReject::NonPositivePrice);
}

TEST(TobValidator, RejectsCrossedBook) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.5, 100.0)), TobReject::CrossedBook);
}

TEST(TobValidator, RejectsLockedBook) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.0)), TobReject::LockedBook);
    EXPECT_EQ(v.rejected(TobReject::LockedBook), 1u);
    EXPECT_EQ(v.rejected(TobReject::CrossedBook), 0u);
}

// --- Quantities ---

TEST(TobValidator, RejectsNanQty) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.5, k_nan, 1.0)), TobReject::NonFiniteQty);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.5, 1.0, k_nan)), TobReject::NonFiniteQty);
}

TEST(TobValidator, RejectsInfiniteQty) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.5, k_inf, 1.0)), TobReject::NonFiniteQty);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.5, 1.0, -k_inf)), TobReject::NonFiniteQty);
}

TEST(TobValidator, RejectsZeroOrNegativeQty) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.5, 0.0, 1.0)), TobReject::NonPositiveQty);
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.0, 100.5, 1.0, -1.0)), TobReject::NonPositiveQty);
}

// --- Check ordering: the first failing check wins ---

TEST(TobValidator, SymbolCheckedBeforePrices) {
    TobValidator v(std::string("btcusdt"));
    EXPECT_EQ(v.validate(make_tob("ETHUSDT", k_nan, 100.5)), TobReject::SymbolMismatch);
}

TEST(TobValidator, FinitePriceCheckedBeforeRange) {
    // NaN must not fall through to range checks (NaN fails every comparison)
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", k_nan, 0.0)), TobReject::NonFinitePrice);
}

TEST(TobValidator, PriceCheckedBeforeQty) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 0.0, 100.5, 0.0, 0.0)), TobReject::NonPositivePrice);
}

TEST(TobValidator, QtyCheckedBeforeCrossed) {
    TobValidator v;
    EXPECT_EQ(v.validate(make_tob("BTCUSDT", 100.5, 100.0, 0.0, 1.0)), TobReject::NonPositiveQty);
}

// --- Counters and summary ---

TEST(TobValidator, CountsPerReason) {
    TobValidator v(std::string("btcusdt"));
    v.validate(make_tob());
    v.validate(make_tob());
    v.validate(make_tob("ETHUSDT"));
    v.validate(make_tob("BTCUSDT", 100.5, 100.0));
    v.validate(make_tob("BTCUSDT", 100.5, 100.0));
    v.validate(make_tob("BTCUSDT", k_nan, 100.0));

    EXPECT_EQ(v.accepted(), 2u);
    EXPECT_EQ(v.rejected(TobReject::SymbolMismatch), 1u);
    EXPECT_EQ(v.rejected(TobReject::CrossedBook), 2u);
    EXPECT_EQ(v.rejected(TobReject::NonFinitePrice), 1u);
    EXPECT_EQ(v.rejected(TobReject::EmptySymbol), 0u);
    EXPECT_EQ(v.rejected(TobReject::NonPositivePrice), 0u);
    EXPECT_EQ(v.rejected(TobReject::NonFiniteQty), 0u);
    EXPECT_EQ(v.rejected(TobReject::NonPositiveQty), 0u);
    EXPECT_EQ(v.rejected(TobReject::LockedBook), 0u);
    EXPECT_EQ(v.rejected_total(), 4u);
}

TEST(TobValidator, SummaryListsEveryReason) {
    TobValidator v;
    v.validate(make_tob());
    v.validate(make_tob("BTCUSDT", 100.5, 100.0));

    const std::string s = v.summary();
    EXPECT_NE(s.find("accepted=1"), std::string::npos);
    EXPECT_NE(s.find("rejected=1"), std::string::npos);
    EXPECT_NE(s.find("crossed_book=1"), std::string::npos);
    for (std::size_t i = 1; i < static_cast<std::size_t>(TobReject::Count); ++i) {
        EXPECT_NE(s.find(tob_reject_to_str(static_cast<TobReject>(i))), std::string::npos)
            << "missing reason: " << tob_reject_to_str(static_cast<TobReject>(i));
    }
}

TEST(TobValidator, RejectToStrHasNameForEveryReason) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(TobReject::Count); ++i) {
        EXPECT_STRNE(tob_reject_to_str(static_cast<TobReject>(i)), "unknown");
    }
    EXPECT_STREQ(tob_reject_to_str(TobReject::None), "none");
    EXPECT_STREQ(tob_reject_to_str(TobReject::Count), "unknown");
}
