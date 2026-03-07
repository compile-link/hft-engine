#pragma once

#include "market_data_source.hpp"

class LiveWebSocketSource : public MarketDataSource {
    bool next(hft::TopOfBook& out) override;
};
