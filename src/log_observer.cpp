#include "log_observer.hpp"
#include "log_utils.hpp"
#include <sstream>

namespace {
    const char* action_to_str(int32_t a) {
        switch (a) {
        case hft::HOLD:
            return "HOLD";
        case hft::QUOTE_BOTH:
            return "QUOTE_BOTH";
        case hft::CANCEL_ALL:
            return "CANCEL_ALL";
        default:
            return "UNSPECIFIED";
        }
    }
} // namespace

bool LogObserver::due(clock::time_point& last) const {
    const auto now = clock::now();
    if (now - last < log_interval) {
        return false;
    }
    last = now;
    return true;
}

void LogObserver::on_message(const hft::StrategySignal& sig, const MessageSpans& spans) {
    if (!due(last_signal_log_)) {
        return;
    }
    std::ostringstream oss;
    oss << "Signal:"
        << " symbol=" << sig.symbol()
        << " action=" << action_to_str(sig.action())
        << " reason=" << sig.reason()
        << " ts_ns=" << sig.ts_ns();
    log_utils::log_to_stream(std::cout, oss.str());

    oss.str("");
    oss.clear();
    const auto e2e_ns = (spans.recv_ts_ns > 0) ? (spans.t1_ns - spans.recv_ts_ns) : 0;
    oss << "Latency:"
        << " e2e_ns=" << e2e_ns
        << " strat_ns=" << spans.strat_end_ns - spans.t0_ns;
    log_utils::log_to_stream(std::cerr, oss.str());
}

void LogObserver::on_validation_reject(const hft::TopOfBook& tob, TobReject reason, uint64_t total) {
    if (!due(last_reject_log_)) {
        return;
    }
    std::ostringstream oss;
    oss << "Rejected:"
        << " symbol=" << tob.symbol()
        << " reason=" << tob_reject_to_str(reason)
        << " seq=" << tob.sequence()
        << " total_rejected=" << total;
    log_utils::log_to_stream(std::cerr, oss.str());
}

void LogObserver::on_sequence_reject(const hft::TopOfBook& tob, SeqResult reason, uint64_t total) {
    if (!due(last_reject_log_)) { // shares the reject log rate limit
        return;
    }
    std::ostringstream oss;
    oss << "Sequence rejected:"
        << " symbol=" << tob.symbol()
        << " reason=" << seq_result_to_str(reason)
        << " seq=" << tob.sequence()
        << " session=" << tob.session()
        << " total_rejected=" << total;
    log_utils::log_to_stream(std::cerr, oss.str());
}

void LogObserver::on_session_start(const hft::TopOfBook& tob) {
    if (tob.session() <= 1 || !due(last_session_log_)) {
        return; // the first session is the normal start, not a reconnect or replay loop
    }
    std::ostringstream oss;
    oss << "Sequence: new session"
        << " symbol=" << tob.symbol()
        << " session=" << tob.session()
        << " seq=" << tob.sequence();
    log_utils::log_to_stream(std::cerr, oss.str());
}

void LogObserver::on_feed_stop(FeedStopReason reason, const FeedHealthSnapshot& snap) {
    std::ostringstream oss;
    oss << "Feed health: stopping"
        << " reason=" << feed_stop_reason_to_str(reason)
        << " " << describe_feed_health(snap);
    log_utils::log_to_stream(std::cerr, oss.str());
}
