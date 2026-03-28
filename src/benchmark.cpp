#include "benchmark.hpp"
#include "log_utils.hpp"
#include "market_data.pb.h"
#include "tob_ring_buffer.hpp"
#include <atomic>
#include <chrono>
#include <thread>

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

void BenchmarkResult::print() const {
    std::ostringstream oss;
    oss << "Benchmark:"
        << " measured_msgs=" << measured_msgs
        << " measured_seconds=" << measured_seconds
        << " throughput_msgs_per_sec=" << throughput_msgs_per_sec
        << " drops=" << drops;
    log_utils::log_to_stream(std::cout, oss.str());

    std::ostringstream lat;
    lat << "Benchmark Latency:"
        << " e2e_p50_ns=" << e2e_p50_ns
        << " e2e_p99_ns=" << e2e_p99_ns
        << " e2e_p999_ns=" << e2e_p999_ns
        << " e2e_max_ns=" << e2e_max_ns
        << " strat_p50_ns=" << strat_p50_ns
        << " strat_p99_ns=" << strat_p99_ns
        << " strat_p999_ns=" << strat_p999_ns
        << " strat_max_ns=" << strat_max_ns;
    log_utils::log_to_stream(std::cout, lat.str());
}

BenchmarkResult Benchmark::run(MarketDataSource& src) {
    BenchmarkResult r{};
    const auto start = std::chrono::steady_clock::now();
    const auto warmup_end = start + warmup_seconds;
    const auto measure_end = warmup_end + measure_seconds;
    std::chrono::steady_clock::time_point measure_start;
    uint64_t drops = 0;
    uint64_t measured_msgs = 0;

    std::atomic<bool> measure_started{false};
    std::atomic<bool> measure_done{false};
    std::atomic<bool> done{false};
    TobRingBuffer q;

    std::thread ingest([&] {
        hft::TopOfBook tob;
        while (src.next(tob)) {
            auto now = std::chrono::steady_clock::now();
            if (!measure_started.load() && now >= warmup_end) {
                measure_start = now;
                measure_started.store(true);
            }
            if (!measure_done.load()) {
                if (std::chrono::steady_clock::now() > measure_end) {
                    measure_done.store(true);

                    break;
                }
            }

            if (!q.push(tob)) {
                if (measure_started.load()) {
                    ++drops;
                }
            }
        }
        done.store(true);
    });

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
            tob.SerializeToString(&payload);

            int32_t action = rust_decide(
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

            const auto now = std::chrono::steady_clock::now();
            const auto t_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            hft::StrategySignal sig;
            sig.set_symbol(tob.symbol());
            sig.set_action(static_cast<hft::QuoteAction>(action));
            sig.set_reason("spread_cond");
            sig.set_ts_ns(static_cast<uint64_t>(t_ns));
            if (measure_started.load() && !measure_done.load()) {
                ++measured_msgs;
            }
        }
    });

    ingest.join();
    process.join();

    if (measure_started.load()) {
        r.measured_seconds = std::chrono::duration<double>(measure_end - measure_start).count();
        r.measured_msgs = measured_msgs;
        r.throughput_msgs_per_sec = (r.measured_seconds > 0) ? (static_cast<double>(r.measured_msgs) / r.measured_seconds) : 0.0;
        r.drops = drops;
    }

    return r;
}
