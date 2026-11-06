#pragma once

#include <charconv>
#include <stdexcept>
#include <string>
#include <system_error>

namespace decimal_parse {
    // Strict, locale-independent decimal parse: the whole string must be consumed.
    // Accepts "nan"/"inf" as std::from_chars does; value checks are left to TobValidator.
    inline double parse_decimal(const std::string& s) {
        double v = 0.0;
        const char* end = s.data() + s.size();
        const auto [ptr, ec] = std::from_chars(s.data(), end, v);
        if (s.empty() || ec != std::errc() || ptr != end) {
            throw std::invalid_argument("invalid decimal: " + s);
        }
        return v;
    }
} // namespace decimal_parse
