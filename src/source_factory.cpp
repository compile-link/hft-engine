#include "source_factory.hpp"
#include "live_websocket_source.hpp"
#include "replay_source.hpp"

namespace utils {

    std::unique_ptr<MarketDataSource> make_source(SourceType st, std::string symbol) {
        std::unique_ptr<MarketDataSource> source;
        if (st == SourceType::Replay) {
            source = std::make_unique<ReplaySource>();
        } else if (st == SourceType::Live) {
            source = std::make_unique<LiveWebSocketSource>(symbol);
        }
        return source;
    }

} // namespace utils
