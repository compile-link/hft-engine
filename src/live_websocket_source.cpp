#include "live_websocket_source.hpp"
#include "log_utils.hpp"
#include <chrono>
#include <nlohmann/json.hpp>
#include <ostream>
#include <sstream>
#include <thread>

LiveWebSocketSource::LiveWebSocketSource(std::string symbol) : symbol_(std::move(symbol)),
                                                               // Individual Symbol Book Ticker Stream
                                                               url_(k_binance_ws_base_endpoint + symbol_ + k_stream) {
    curl_global_init(CURL_GLOBAL_DEFAULT); // Initialize libcurl globally
}

LiveWebSocketSource::~LiveWebSocketSource() {
    close();
    curl_global_cleanup(); // Global cleanup
}

bool LiveWebSocketSource::next(hft::TopOfBook& out) {
    while (true) {
        report_with_interval();

        if (!curl_ && (!connect() || !subscribe())) {
            reconnect_with_backoff();
            continue;
        }

        std::string msg;
        if (!read_message(msg)) {
            reconnect_with_backoff();
            continue;
        }
        stats_.recv++;

        if (!parse_to_tob(msg, out)) {
            log_utils::log_to_stream(std::cerr, "[live] parse error");
            stats_.parse_err++;
            continue;
        }
        stats_.parse_ok++;

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
    while (true) {
        size_t nrecv = 0;
        const struct curl_ws_frame* meta = nullptr;

        const CURLcode rc = curl_ws_recv(curl_, buf, sizeof(buf), &nrecv, &meta); // Receive WebSocket frame
        if (rc == CURLE_AGAIN) {
            // no data yet, wait
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (rc != CURLE_OK) {
            return false;
        }
        if (meta && (meta->flags & CURLWS_CLOSE)) {
            return false;
        }
        if (meta && (meta->flags & CURLWS_PING)) {
            // Ignore ping frames
            continue;
        }

        msg.assign(buf, nrecv);
        return !msg.empty();
    }
}

bool LiveWebSocketSource::parse_to_tob(const std::string& msg, hft::TopOfBook& out) {
    using nlohmann::json;
    json j;
    try {
        j = json::parse(msg);

        const std::string symbol = j.at("s").get<std::string>();        // symbol
        const double bid_px = std::stod(j.at("b").get<std::string>());  // best bid price
        const double bid_qty = std::stod(j.at("B").get<std::string>()); // best bid qty
        const double ask_px = std::stod(j.at("a").get<std::string>());  // best ask price
        const double ask_qty = std::stod(j.at("A").get<std::string>()); // best ask qty
        const uint64_t update_id = j.at("u").get<uint64_t>();           // order book updateId

        // Validate against corrupted data
        if (bid_px <= 0.0 || ask_px <= 0.0 || ask_px < bid_px) {
            return false;
        }

        out.set_symbol(symbol);
        out.set_bid_px(bid_px);
        out.set_bid_qty(bid_qty);
        out.set_ask_px(ask_px);
        out.set_ask_qty(ask_qty);
        out.set_exchange_ts_ns(0);
        out.set_recv_ts_ns(log_utils::now_ns());
        out.set_sequence(update_id);

        return true;

    } catch (...) {
        return false;
    }
}

void LiveWebSocketSource::reconnect_with_backoff() {
    stats_.reconnects++;
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

void LiveWebSocketSource::report_with_interval() {
    static constexpr auto report_period = std::chrono::seconds(5);
    const auto now = std::chrono::steady_clock::now();

    if (now - last_report_time_ < report_period) {
        return;
    }

    const auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report_time_).count();
    const uint64_t recv_delta = stats_.recv - last_report_stats_.recv;
    const uint64_t ok_delta = stats_.parse_ok - last_report_stats_.parse_ok;
    const uint64_t err_delta = stats_.parse_err - last_report_stats_.parse_err;
    const uint64_t reconn_delta = stats_.reconnects - last_report_stats_.reconnects;
    std::ostringstream oss;
    oss << "Diagnostics:"
        << " dt_ms=" << dt_ms
        << " recv=" << stats_.recv
        << " recv_rate[msg/s]=" << (dt_ms > 0 ? (recv_delta * 1000 / dt_ms) : 0)
        << " parse_ok=" << stats_.parse_ok
        << " d_parse_ok=" << ok_delta
        << " parse_err=" << stats_.parse_err
        << " d_parse_err=" << err_delta
        << " reconn=" << stats_.reconnects
        << " d_reconn=" << reconn_delta;
    log_utils::log_to_stream(std::cerr, oss.str());

    last_report_time_ = now;
    last_report_stats_ = stats_;
}
