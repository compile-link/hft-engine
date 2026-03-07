#pragma once

#include "market_data_source.hpp"
#include <memory>
#include <string>

namespace utils {
    enum class SourceType { Replay,
                            Live };
    std::unique_ptr<MarketDataSource> make_source(SourceType st);
} // namespace utils
