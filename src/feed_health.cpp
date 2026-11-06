#include "feed_health.hpp"
#include <algorithm>
#include <sstream>

namespace {
    uint64_t to_ns(std::chrono::milliseconds ms) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(ms).count());
    }

    // True if ts is set and no older than window_ns
    bool within(uint64_t now_ns, uint64_t ts, uint64_t window_ns) {
        return ts != 0 && now_ns >= ts && now_ns - ts <= window_ns;
    }

    // Age in ms for logging; -1 = never
    long long age_ms(uint64_t now_ns, uint64_t ts) {
        if (ts == 0 || now_ns < ts) {
            return -1;
        }
        return static_cast<long long>((now_ns - ts) / 1'000'000);
    }
} // namespace

const char* feed_stop_reason_to_str(FeedStopReason r) {
    switch (r) {
    case FeedStopReason::None:
        return "none";
    case FeedStopReason::DeadFeed:
        return "dead_feed";
    case FeedStopReason::InvalidData:
        return "invalid_data";
    case FeedStopReason::StaleFeed:
        return "stale_feed";
    default:
        return "unknown";
    }
}

FeedStopReason evaluate_feed_health(const FeedHealthConfig& cfg, const FeedHealthSnapshot& s) {
    const bool started = s.last_valid_ns != 0;
    const uint64_t window_ns = to_ns(started ? cfg.max_no_valid : cfg.startup_grace);
    const uint64_t ref_ns = started ? s.last_valid_ns : s.start_ns;

    if (s.now_ns < ref_ns || s.now_ns - ref_ns <= window_ns) {
        return FeedStopReason::None;
    }

    // Classify by what arrived during the window, most specific first
    if (within(s.now_ns, s.last_rejected_ns, window_ns) || within(s.now_ns, s.last_parse_err_ns, window_ns)) {
        return FeedStopReason::InvalidData;
    }
    if (within(s.now_ns, s.last_frame_ns, window_ns)) {
        return FeedStopReason::StaleFeed;
    }
    return FeedStopReason::DeadFeed;
}

std::string describe_feed_health(const FeedHealthSnapshot& s) {
    std::ostringstream oss;
    oss << "valid_age_ms=" << age_ms(s.now_ns, s.last_valid_ns)
        << " reject_age_ms=" << age_ms(s.now_ns, s.last_rejected_ns)
        << " parse_err_age_ms=" << age_ms(s.now_ns, s.last_parse_err_ns)
        << " frame_age_ms=" << age_ms(s.now_ns, s.last_frame_ns)
        << " uptime_ms=" << age_ms(s.now_ns, s.start_ns);
    return oss.str();
}

FeedHealthSnapshot FeedHealthMonitor::snapshot(uint64_t now_ns, uint64_t last_frame_ns, uint64_t last_parse_err_ns) const {
    FeedHealthSnapshot s;
    s.now_ns = now_ns;
    s.start_ns = start_ns_;
    s.last_valid_ns = last_valid_ns_.load(std::memory_order_relaxed);
    s.last_rejected_ns = last_rejected_ns_.load(std::memory_order_relaxed);
    s.last_frame_ns = last_frame_ns;
    s.last_parse_err_ns = last_parse_err_ns;
    return s;
}
