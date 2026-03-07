#include "live_websocket_source.hpp"

bool LiveWebSocketSource::next(hft::TopOfBook& out) {
    if (idx_ >= 5) {
        return false;
    }

    ++idx_;
    out = hft::TopOfBook();
    return true;
}
