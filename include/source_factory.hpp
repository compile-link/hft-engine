#pragma once

#include "replay_source.hpp"
#include "market_data_source.hpp"
#include <memory>
#include <string>

namespace utils {
    enum class SourceType { Replay,
                            Live };
    std::unique_ptr<MarketDataSource> make_replay_source(ReplayConfig cfg);
    std::unique_ptr<MarketDataSource> make_live_source(std::string symbol);
} // namespace utils
