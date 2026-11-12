#include "pipeline.hpp"
#include "market_data.pb.h"
#include "sequence_tracker.hpp"
#include "time_utils.hpp"
#include "tob_ring_buffer.hpp"
#include "tob_validator.hpp"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

Pipeline::Pipeline(PipelineConfig cfg, PipelineObserver* observer) : cfg_(std::move(cfg)), observer_(observer) {
    if (cfg_.watchdog && !cfg_.health) {
        throw std::invalid_argument("pipeline watchdog requires the health monitor");
    }
}

PipelineStats Pipeline::run(MarketDataSource& src) {
    std::atomic<bool> done{false};
    uint64_t drops = 0;          // ingest thread only
    uint64_t serialize_errs = 0; // process thread only
    TobRingBuffer q;
    TobValidator validator(cfg_.expected_symbol); // ingest thread only
    SequenceTracker seq_tracker;                  // ingest thread only
    std::optional<FeedHealthMonitor> health;
    if (cfg_.health) {
        health.emplace(cfg_.health_cfg, time_utils::now_ns());
    }
    std::atomic<FeedStopReason> stop_reason{FeedStopReason::None};

    std::thread ingest([&] {
        hft::TopOfBook tob;
        std::string last_symbol = cfg_.expected_symbol.value_or("");
        while (src.next(tob)) {
            auto status = hft::FEED_OK;
            if (cfg_.validate) {
                const bool valid = validator.validate(tob) == TobReject::None;
                const SeqResult sr = valid ? seq_tracker.check(tob) : SeqResult::Count;
                if (!valid || !seq_accepted(sr)) {
                    if (health) {
                        health->on_rejected(time_utils::now_ns());
                    }
                    continue;
                }
                if (sr == SeqResult::SessionStart) {
                    status = hft::FEED_SESSION_START;
                }
            }
            if (health) {
                health->on_valid(time_utils::now_ns());
            }
            tob.set_feed_status(status);
            last_symbol = tob.symbol();
            if (!q.push(tob)) {
                ++drops;
                if (observer_) {
                    observer_->on_drop(time_utils::now_ns());
                }
            }
        }
        if (stop_reason.load() != FeedStopReason::None) {
            // Final status tick so the strategy invalidates its state rather than acting on the last book.
            // Must not be dropped: retry until the consumer frees a slot.
            hft::TopOfBook halt;
            halt.set_symbol(last_symbol);
            halt.set_feed_status(hft::FEED_HALTED);
            halt.set_recv_ts_ns(time_utils::now_ns());
            while (!q.push(halt)) {
                std::this_thread::yield();
            }
        }
        done.store(true);
    });

    // Watchdog: next() may block indefinitely, so health is evaluated off the ingest thread
    std::thread watchdog;
    if (cfg_.watchdog) {
        watchdog = std::thread([&] {
            while (!done.load()) {
                const SourceActivity& act = src.activity();
                const FeedHealthSnapshot snap = health->snapshot(
                    time_utils::now_ns(),
                    act.last_frame_ns.load(std::memory_order_relaxed),
                    act.last_parse_err_ns.load(std::memory_order_relaxed));
                const FeedStopReason r = health->evaluate(snap);
                if (r != FeedStopReason::None) {
                    stop_reason.store(r);
                    src.request_stop();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    std::thread process([&] {
        std::string payload;
        hft::TopOfBook tob;
        while (true) {
            if (!q.pop(tob)) {
                if (done.load()) {
                    break;
                }
                std::this_thread::yield();
                continue;
            }
            if (!tob.SerializeToString(&payload)) {
                ++serialize_errs;
                continue;
            }

            // strat span: the strategy entry point only
            MessageSpans spans;
            spans.recv_ts_ns = tob.recv_ts_ns();
            spans.t0_ns = time_utils::now_ns();
            int32_t action = rust_decide(
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
            spans.strat_end_ns = time_utils::now_ns();

            hft::StrategySignal sig;
            sig.set_symbol(tob.symbol());
            sig.set_action(static_cast<hft::QuoteAction>(action));
            sig.set_reason("spread_cond");
            sig.set_ts_ns(spans.strat_end_ns);  // decision time
            spans.t1_ns = time_utils::now_ns(); // e2e stop: signal complete

            if (observer_) {
                observer_->on_message(spans);
            }
        }
    });

    ingest.join();
    if (watchdog.joinable()) {
        watchdog.join();
    }
    process.join();

    PipelineStats stats;
    stats.drops = drops;
    stats.serialize_errs = serialize_errs;
    stats.rejected = validator.rejected_total() + seq_tracker.rejected_total();
    stats.stop_reason = stop_reason.load();
    return stats;
}
