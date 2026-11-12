#include "replay_source.hpp"
#include "book_ticker_parse.hpp"
#include "time_utils.hpp"
#include <cstdio>
#include <thread>

namespace {
    struct StubTick {
        double bid_px, ask_px, bid_qty, ask_qty;
    };

    constexpr StubTick k_base_ticks[] = {
        {100.00, 100.80, 1.0, 1.0},
        {100.00, 101.00, 1.0, 1.0},
        {100.20, 101.40, 1.2, 0.9},
        {100.50, 101.49, 1.1, 1.1},
        {100.75, 101.75, 0.8, 1.3},
    };

#ifdef HFT_PROFILE
    constexpr int k_repeats = 10;
#else
    constexpr int k_repeats = 1;
#endif

    // Raw Binance bookTicker frame, the same shape the live /ws/<symbol>@bookTicker stream sends:
    // 8-decimal price/qty strings and an exchange-sized update ID.
    std::string make_frame(const StubTick& t, uint64_t update_id) {
        char buf[192];
        const int n = std::snprintf(
            buf, sizeof(buf),
            R"({"u":%llu,"s":"%s","b":"%.8f","B":"%.8f","a":"%.8f","A":"%.8f"})",
            static_cast<unsigned long long>(update_id), ReplaySource::k_symbol, t.bid_px, t.bid_qty, t.ask_px, t.ask_qty);
        return std::string(buf, static_cast<size_t>(n));
    }

    // Built on first use rather than at static initialization, where an
    // exception (e.g. bad_alloc) could not be caught.
    // Update IDs increase across the whole pass, so repeats are not duplicates to the SequenceTracker.
    const std::vector<std::string>& mock_frames() {
        static const std::vector<std::string> frames = [] {
            std::vector<std::string> v;
            uint64_t update_id = 400'900'217;
            for (int r = 0; r < k_repeats; ++r) {
                for (const StubTick& t : k_base_ticks) {
                    v.push_back(make_frame(t, update_id++));
                }
            }
            return v;
        }();
        return frames;
    }
} // namespace

ReplaySource::ReplaySource(ReplayConfig cfg) : cfg_(cfg) {
    if (cfg_.max_runtime) {
        replay_end_ = std::chrono::steady_clock::now() + *cfg_.max_runtime;
    }
}

bool ReplaySource::next(hft::TopOfBook& out) {
    const auto& frames = mock_frames();
    while (!stop_requested() && !frames.empty()) {
        if (cfg_.loop && idx_ >= frames.size()) {
            idx_ = 0;
            session_++;
        } else if (idx_ >= frames.size()) {
            return false;
        }

        if (replay_end_ && std::chrono::steady_clock::now() > *replay_end_) {
            return false;
        }

        // Pacing models the gap before the next frame arrives; it is not engine latency
        std::this_thread::sleep_for(cfg_.sleep_per_tick);

        // From here on, mirror LiveWebSocketSource::next: frame activity, a fresh message buffer,
        // then parse with recv_ts_ns stamped right before it
        activity_.last_frame_ns.store(time_utils::now_ns(), std::memory_order_relaxed);
        const std::string msg = frames[idx_++];
        if (!book_ticker::parse(msg, time_utils::now_ns(), out)) {
            activity_.last_parse_err_ns.store(time_utils::now_ns(), std::memory_order_relaxed);
            continue;
        }
        out.set_session(session_);
        return true;
    }
    return false;
}
