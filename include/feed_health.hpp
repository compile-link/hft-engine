#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

enum class FeedStopReason {
    None = 0,
    DeadFeed,    // no transport frames at all within the window: disconnected, reconnect loop, or dead source
    InvalidData, // messages arrive, but every one is rejected or fails to parse
    StaleFeed,   // connection alive (heartbeats), but no market data within the window
};

const char* feed_stop_reason_to_str(FeedStopReason r);

struct FeedHealthConfig {
    std::chrono::milliseconds max_no_valid{10000};   // stop if no valid tick for this long
    std::chrono::milliseconds startup_grace{15000};  // window for the first valid tick (connect + subscribe)
};

// Point-in-time view of feed activity; all timestamps steady-clock ns, 0 = never
struct FeedHealthSnapshot {
    uint64_t now_ns = 0;
    uint64_t start_ns = 0;
    uint64_t last_valid_ns = 0;
    uint64_t last_rejected_ns = 0;
    uint64_t last_frame_ns = 0;
    uint64_t last_parse_err_ns = 0;
};

// Single stop rule: no valid tick within max_no_valid (startup_grace before the first one).
// When it trips, the reason is classified from what did arrive inside the window.
FeedStopReason evaluate_feed_health(const FeedHealthConfig& cfg, const FeedHealthSnapshot& s);

std::string describe_feed_health(const FeedHealthSnapshot& s);

// Thread-safe recorder: ingest thread reports ticks, watchdog thread reads.
class FeedHealthMonitor {
  public:
    FeedHealthMonitor(FeedHealthConfig cfg, uint64_t start_ns) : cfg_(cfg), start_ns_(start_ns) {}

    void on_valid(uint64_t ns) { last_valid_ns_.store(ns, std::memory_order_relaxed); }
    void on_rejected(uint64_t ns) { last_rejected_ns_.store(ns, std::memory_order_relaxed); }

    FeedHealthSnapshot snapshot(uint64_t now_ns, uint64_t last_frame_ns, uint64_t last_parse_err_ns) const;
    FeedStopReason evaluate(const FeedHealthSnapshot& s) const { return evaluate_feed_health(cfg_, s); }
    const FeedHealthConfig& config() const { return cfg_; }

  private:
    FeedHealthConfig cfg_;
    uint64_t start_ns_;
    std::atomic<uint64_t> last_valid_ns_{0};
    std::atomic<uint64_t> last_rejected_ns_{0};
};
