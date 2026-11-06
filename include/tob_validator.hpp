#pragma once

#include "market_data.pb.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

enum class TobReject : std::size_t {
    None = 0,
    EmptySymbol,
    SymbolMismatch,
    NonFinitePrice,
    NonPositivePrice,
    NonFiniteQty,
    NonPositiveQty,
    CrossedBook,
    LockedBook,
    Count
};

const char* tob_reject_to_str(TobReject r);

// Source-independent sanity checks applied to every TopOfBook before it enters the pipeline.
// Both crossed (ask < bid) and locked (ask == bid) books are rejected: on a continuous
// matching engine a locked top of book can only be a feed artifact or corrupt message.
class TobValidator {
  public:
    // expected_symbol: if set, tick symbol must match (case-insensitive)
    explicit TobValidator(std::optional<std::string> expected_symbol = std::nullopt);

    TobReject validate(const hft::TopOfBook& tob);

    uint64_t accepted() const { return accepted_; }
    uint64_t rejected(TobReject r) const { return rejected_[static_cast<std::size_t>(r)]; }
    uint64_t rejected_total() const;
    std::string summary() const;

  private:
    TobReject check(const hft::TopOfBook& tob) const;

    std::optional<std::string> expected_symbol_;
    uint64_t accepted_ = 0;
    std::array<uint64_t, static_cast<std::size_t>(TobReject::Count)> rejected_{};
};
