#pragma once

#include "market_data.pb.h"
#include "market_data_source.hpp"
#include <chrono>
#include <cstddef>
#include <curl/curl.h>
#include <string>

class LiveWebSocketSource : public MarketDataSource {
  public:
    explicit LiveWebSocketSource(std::string symbol = k_default_symbol);
    ~LiveWebSocketSource() override;
    bool next(hft::TopOfBook& out) override;

  private:
    bool connect();
    bool subscribe();
    bool read_message(std::string& msg);
    bool parse_to_tob(const std::string& msg, hft::TopOfBook& out);
    void reconnect_with_backoff();
    void close();

    static constexpr const char* k_default_symbol = "BTCUSDT";
    static constexpr const char* k_binance_ws_base_endpoint = "wss://stream.binance.com:9443/ws/";
    static constexpr const char* k_stream = "@bookTicker";
    std::string symbol_;
    std::string url_;
    CURL* curl_ = nullptr;
    uint64_t seq_ = 0;
    std::chrono::milliseconds backoff_ms_{1000};
};
