#include "decimal_parse.hpp"
#include <cmath>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

using decimal_parse::parse_decimal;

// --- Accepted ---

TEST(DecimalParse, ParsesTypicalExchangeValues) {
    EXPECT_DOUBLE_EQ(parse_decimal("100.50000000"), 100.5);
    EXPECT_DOUBLE_EQ(parse_decimal("0.00071200"), 0.000712);
    EXPECT_DOUBLE_EQ(parse_decimal("42"), 42.0);
    EXPECT_DOUBLE_EQ(parse_decimal("0"), 0.0);
}

TEST(DecimalParse, ParsesNegativeAndExponent) {
    // Range checks belong to TobValidator, not the parser
    EXPECT_DOUBLE_EQ(parse_decimal("-1.5"), -1.5);
    EXPECT_DOUBLE_EQ(parse_decimal("1e-7"), 1e-7);
    EXPECT_DOUBLE_EQ(parse_decimal("2.5E3"), 2500.0);
}

TEST(DecimalParse, ParsesNanAndInfForValidatorToReject) {
    EXPECT_TRUE(std::isnan(parse_decimal("nan")));
    EXPECT_TRUE(std::isinf(parse_decimal("inf")));
    EXPECT_TRUE(std::isinf(parse_decimal("-inf")));
}

// --- Rejected ---

TEST(DecimalParse, RejectsEmpty) {
    EXPECT_THROW(parse_decimal(""), std::invalid_argument);
}

TEST(DecimalParse, RejectsTrailingGarbage) {
    EXPECT_THROW(parse_decimal("1.5abc"), std::invalid_argument);
    EXPECT_THROW(parse_decimal("1.5 "), std::invalid_argument);
    EXPECT_THROW(parse_decimal("1.5.2"), std::invalid_argument);
    EXPECT_THROW(parse_decimal(std::string("1.5\0", 4)), std::invalid_argument);
}

TEST(DecimalParse, RejectsLeadingWhitespaceAndPlus) {
    EXPECT_THROW(parse_decimal(" 1.5"), std::invalid_argument);
    EXPECT_THROW(parse_decimal("+1.5"), std::invalid_argument);
}

TEST(DecimalParse, RejectsNonNumeric) {
    EXPECT_THROW(parse_decimal("abc"), std::invalid_argument);
    EXPECT_THROW(parse_decimal("."), std::invalid_argument);
    EXPECT_THROW(parse_decimal("-"), std::invalid_argument);
}

TEST(DecimalParse, RejectsLocaleCommaSeparator) {
    EXPECT_THROW(parse_decimal("1,5"), std::invalid_argument);
}

TEST(DecimalParse, RejectsHexPrefix) {
    EXPECT_THROW(parse_decimal("0x10"), std::invalid_argument);
}

TEST(DecimalParse, RejectsOutOfRange) {
    EXPECT_THROW(parse_decimal("1e999"), std::invalid_argument);
}
