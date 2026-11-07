#include "live_websocket_source.hpp"
#include "book_ticker_parse.hpp"
#include "log_utils.hpp"
#include "time_utils.hpp"
#include <chrono>
#include <ostream>
#include <sstream>
#include <thread>

LiveWebSocketSource::LiveWebSocketSource(std::string symbol) : symbol_(std::move(symbol)),
                                                               // Individual Symbol Book Ticker Stream
                                                               url_(k_binance_ws_base_endpoint + symbol_ + k_stream) {}

LiveWebSocketSource::~LiveWebSocketSource() {
    close();
}

bool LiveWebSocketSource::next(hft::TopOfBook& out) {
    while (!stop_requested()) {
        report_with_interval();

        if (!curl_ && (!connect() || !subscribe())) {
            reconnect_with_backoff();
            continue;
        }

        std::string msg;
        if (!read_message(msg)) {
            if (stop_requested()) {
                break;
            }
            reconnect_with_backoff();
            continue;
        }
        stats_.recv++;

        if (!book_ticker::parse(msg, time_utils::now_ns(), out)) {
            log_utils::log_to_stream(std::cerr, "[live] parse error");
            stats_.parse_err++;
            activity_.last_parse_err_ns.store(time_utils::now_ns(), std::memory_order_relaxed);
            continue;
        }
        stats_.parse_ok++;
        out.set_session(session_);

        backoff_ms_ = std::chrono::milliseconds(1000);
        return true;
    }
    return false;
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
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);         // WebSocket connect-only mode
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, 5000L); // Bound connect time so stop requests are honored

    const CURLcode rc = curl_easy_perform(curl_); // Open WebSocket connection
    if (rc != CURLE_OK) {
        return false;
    }
    session_++; // New stream: sequence baseline is no longer comparable
    return true;
}

// Already subscribed by url (Binance)
bool LiveWebSocketSource::subscribe() {
    return true;
}

bool LiveWebSocketSource::read_message(std::string& msg) {
    msg.clear();
    msg.reserve(8192);
    static constexpr size_t k_max_msg = 256 * 1024;
    char buf[8192];

    while (true) {
        size_t nrecv = 0;
        const struct curl_ws_frame* meta = nullptr;
        const CURLcode rc = curl_ws_recv(curl_, buf, sizeof(buf), &nrecv, &meta); // Receive WebSocket frame

        if (rc == CURLE_AGAIN) {
            if (stop_requested()) {
                return false;
            }
            // no data yet, wait
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (rc != CURLE_OK) {
            return false;
        }
        if (!meta) {
            continue;
        }
        // Any frame (incl. ping/pong) proves the connection is alive
        activity_.last_frame_ns.store(time_utils::now_ns(), std::memory_order_relaxed);
        if (meta->flags & CURLWS_CLOSE) {
            return false;
        }
        if (meta->flags & CURLWS_PING) {
            continue;
        }
        if (meta->flags & CURLWS_PONG) {
            continue;
        }

        if (nrecv > 0) {
            if (msg.size() + nrecv > k_max_msg) {
                return false;
            }
            msg.append(buf, nrecv);
        }

        const bool frame_finished = (meta->bytesleft == 0);
        const bool msg_finished = frame_finished && !(meta->flags & CURLWS_CONT);

        if (msg_finished) {
            return !msg.empty();
        }
    }
}

void LiveWebSocketSource::reconnect_with_backoff() {
    stats_.reconnects++;
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    // Sleep in slices so a stop request is not delayed by the full backoff
    const auto wake = std::chrono::steady_clock::now() + backoff_ms_;
    while (!stop_requested() && std::chrono::steady_clock::now() < wake) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
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
