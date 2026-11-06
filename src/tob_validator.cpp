#include "tob_validator.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace {
    bool iequals(const std::string& a, const std::string& b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }
} // namespace

const char* tob_reject_to_str(TobReject r) {
    switch (r) {
    case TobReject::None:
        return "none";
    case TobReject::EmptySymbol:
        return "empty_symbol";
    case TobReject::SymbolMismatch:
        return "symbol_mismatch";
    case TobReject::NonFinitePrice:
        return "non_finite_price";
    case TobReject::NonPositivePrice:
        return "non_positive_price";
    case TobReject::NonFiniteQty:
        return "non_finite_qty";
    case TobReject::NonPositiveQty:
        return "non_positive_qty";
    case TobReject::CrossedBook:
        return "crossed_book";
    case TobReject::LockedBook:
        return "locked_book";
    default:
        return "unknown";
    }
}

TobValidator::TobValidator(std::optional<std::string> expected_symbol) : expected_symbol_(std::move(expected_symbol)) {}

TobReject TobValidator::validate(const hft::TopOfBook& tob) {
    const TobReject r = check(tob);
    if (r == TobReject::None) {
        accepted_++;
    } else {
        rejected_[static_cast<std::size_t>(r)]++;
    }
    return r;
}

TobReject TobValidator::check(const hft::TopOfBook& tob) const {
    if (tob.symbol().empty()) {
        return TobReject::EmptySymbol;
    }
    if (expected_symbol_ && !iequals(tob.symbol(), *expected_symbol_)) {
        return TobReject::SymbolMismatch;
    }
    // Finiteness first: NaN fails every comparison and would slip past the range checks
    if (!std::isfinite(tob.bid_px()) || !std::isfinite(tob.ask_px())) {
        return TobReject::NonFinitePrice;
    }
    if (tob.bid_px() <= 0.0 || tob.ask_px() <= 0.0) {
        return TobReject::NonPositivePrice;
    }
    if (!std::isfinite(tob.bid_qty()) || !std::isfinite(tob.ask_qty())) {
        return TobReject::NonFiniteQty;
    }
    if (tob.bid_qty() <= 0.0 || tob.ask_qty() <= 0.0) {
        return TobReject::NonPositiveQty;
    }
    if (tob.ask_px() < tob.bid_px()) {
        return TobReject::CrossedBook;
    }
    if (tob.ask_px() == tob.bid_px()) {
        return TobReject::LockedBook;
    }
    return TobReject::None;
}

uint64_t TobValidator::rejected_total() const {
    uint64_t total = 0;
    for (const uint64_t n : rejected_) {
        total += n;
    }
    return total;
}

std::string TobValidator::summary() const {
    std::ostringstream oss;
    oss << "Validation:"
        << " accepted=" << accepted_
        << " rejected=" << rejected_total();
    for (std::size_t i = 1; i < rejected_.size(); ++i) {
        oss << " " << tob_reject_to_str(static_cast<TobReject>(i)) << "=" << rejected_[i];
    }
    return oss.str();
}
