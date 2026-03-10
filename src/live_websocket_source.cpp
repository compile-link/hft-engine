#include "live_websocket_source.hpp"
#include <thread>

LiveWebSocketSource::LiveWebSocketSource(std::string symbol) : symbol_(std::move(symbol)),
                                                               // Individual Symbol Book Ticker Stream
                                                               url_("wss://stream.binance.com:9443/ws/" + symbol_ + "@bookTicker") {
    curl_global_init(CURL_GLOBAL_DEFAULT); // Initialize libcurl globally
}

LiveWebSocketSource::~LiveWebSocketSource() {
    close();
    curl_global_cleanup(); // Global cleanup
}

bool LiveWebSocketSource::next(hft::TopOfBook& out) {
    while (true) {
        if (!curl_ && (!connect() || !subscribe())) {
            reconnect_with_backoff();
            continue;
        }

        std::string msg;
        if (!read_message(msg)) {
            reconnect_with_backoff();
            continue;
        }

        if (!parse_to_tob(msg, out)) {
            std::cerr << "[live] parse error\n";
            continue;
        }

        backoff_ms_ = std::chrono::milliseconds(1000);
        return true;
    }
}

bool LiveWebSocketSource::connect() {
    if (curl_) {
        curl_easy_cleanup(curl_); // Destroy session handle
    }

    curl_ = curl_easy_init(); // Create CURL* handle (session)
    if (!curl_) {
        return false;
    }

    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L); // WebSocket connect-only mode

    const CURLcode rc = curl_easy_perform(curl_); // Open WebSocket connection
    return rc == CURLE_OK;
}

// Already subscribed by url (Binance)
bool LiveWebSocketSource::subscribe() {
    return true;
}

bool LiveWebSocketSource::read_message(std::string& msg) {
    msg.clear();
    char buf[8192];
    size_t nrecv = 0;
    const struct curl_ws_frame* meta = nullptr;

    const CURLcode rc = curl_ws_recv(curl_, buf, sizeof(buf), &nrecv, &meta); // Receive WebSocket frame
    if (rc != CURLE_OK) {
        return false;
    }
    if (meta && (meta->flags & CURLWS_CLOSE)) {
        return false;
    }
    if (meta && (meta->flags & CURLWS_PING)) {
        return true;
    }

    msg.assign(buf, nrecv);
    return !msg.empty();
}

bool LiveWebSocketSource::parse_to_tob(const std::string& msg, hft::TopOfBook& out) {
    // JSON parser / mapping
    // Stream Name: <symbol>@bookTicker

    // Update Speed: Real-time

    // Payload:

    // {
    //     "u": 400900217,         // order book updateId
    //     "s": "BNBUSDT",         // symbol
    //     "b": "25.35190000",     // best bid price
    //     "B": "31.21000000",     // best bid qty
    //     "a": "25.36520000",     // best ask price
    //     "A": "40.66000000"      // best ask qty
    // }
    (void)msg;
    (void)out;
    return false;
}
void LiveWebSocketSource::reconnect_with_backoff() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms_));
    backoff_ms_ = std::min(backoff_ms_ * 2, std::chrono::milliseconds(5000));
}

void LiveWebSocketSource::close() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}
