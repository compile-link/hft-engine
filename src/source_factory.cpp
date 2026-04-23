#include "source_factory.hpp"
#include "live_websocket_source.hpp"

namespace utils {

    std::unique_ptr<MarketDataSource> make_replay_source(ReplayConfig cfg) {
        std::unique_ptr<MarketDataSource> source = std::make_unique<ReplaySource>(cfg);
        return source;
    }

    std::unique_ptr<MarketDataSource> make_live_source(std::string symbol) {
        std::unique_ptr<MarketDataSource> source = std::make_unique<LiveWebSocketSource>(symbol);
        return source;
    }

} // namespace utils
