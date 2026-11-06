#pragma once

#include "market_data.pb.h"
#include <atomic>
#include <cstdint>

// Transport-level activity, written by the ingest thread and read by the health watchdog.
// Timestamps are steady-clock ns; 0 means "never" (or not tracked by the source).
struct SourceActivity {
    std::atomic<uint64_t> last_frame_ns{0};     // any frame from the transport, including heartbeats
    std::atomic<uint64_t> last_parse_err_ns{0}; // last message that failed to parse
};

class MarketDataSource {
  public:
    virtual bool next(hft::TopOfBook& out) = 0;
    virtual ~MarketDataSource() = default;

    // Thread-safe: makes a blocked or future next() return false promptly
    void request_stop() { stop_.store(true, std::memory_order_relaxed); }
    const SourceActivity& activity() const { return activity_; }

  protected:
    bool stop_requested() const { return stop_.load(std::memory_order_relaxed); }
    SourceActivity activity_;

  private:
    std::atomic<bool> stop_{false};
};
