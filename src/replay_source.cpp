#include "replay_source.hpp"
#include "time_utils.hpp"
#include <thread>

namespace {
    hft::TopOfBook make_tob(
        const std::string& symbol,
        double bid_px,
        double ask_px,
        double bid_qty,
        double ask_qty,
        uint64_t exch_ts,
        uint64_t recv_ts,
        uint64_t seq) {
        hft::TopOfBook t;
        t.set_symbol(symbol);
        t.set_bid_px(bid_px);
        t.set_ask_px(ask_px);
        t.set_bid_qty(bid_qty);
        t.set_ask_qty(ask_qty);
        t.set_exchange_ts_ns(exch_ts);
        t.set_recv_ts_ns(recv_ts);
        t.set_sequence(seq);
        return t;
    }

    std::vector<hft::TopOfBook> k_mock_ticks = {
        make_tob("BTCUSDT", 100.00, 100.80, 1.0, 1.0, 1'000'000, 1'000'100, 1),
        make_tob("BTCUSDT", 100.00, 101.00, 1.0, 1.0, 1'000'200, 1'000'300, 2),
        make_tob("BTCUSDT", 100.20, 101.40, 1.2, 0.9, 1'000'400, 1'000'500, 3),
        make_tob("BTCUSDT", 100.50, 101.49, 1.1, 1.1, 1'000'600, 1'000'700, 4),
        make_tob("BTCUSDT", 100.75, 101.75, 0.8, 1.3, 1'000'800, 1'000'900, 5)};

#ifdef HFT_PROFILE
    static std::vector<hft::TopOfBook> k_ticks_expanded = [] {
        std::vector<hft::TopOfBook> v;
        v.reserve(k_mock_ticks.size() * 100);
        for (int i = 0; i < 10; ++i) {
            v.insert(v.end(), k_mock_ticks.begin(), k_mock_ticks.end());
        }
        return v;
    }();
#endif
} // namespace

bool ReplaySource::next(hft::TopOfBook& out) {
#ifdef HFT_PROFILE
    static auto k_mock_ticks = k_ticks_expanded;
#endif
    if (idx_ >= k_mock_ticks.size()) {
        return false;
    }

    out = k_mock_ticks[idx_++];
    const auto t_ns = time_utils::now_ns();
    out.set_recv_ts_ns(t_ns);

#ifdef HFT_PROFILE
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif

    return true;
}
