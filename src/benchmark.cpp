#include "benchmark.hpp"
#include "log_utils.hpp"
#include "market_data.pb.h"
#include "time_utils.hpp"
#include "tob_ring_buffer.hpp"
#include <algorithm>
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
    std::vector<uint64_t> e2e_samples;
    e2e_samples.reserve(250000);
    std::vector<uint64_t> strat_samples;
    strat_samples.reserve(250000);
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

            const auto t0_ns = time_utils::now_ns();
            int32_t action = rust_decide(
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

            const auto t1_ns = time_utils::now_ns();
            hft::StrategySignal sig;
            sig.set_symbol(tob.symbol());
            sig.set_action(static_cast<hft::QuoteAction>(action));
            sig.set_reason("spread_cond");
            sig.set_ts_ns(static_cast<uint64_t>(t1_ns));
            if (measure_started.load() && !measure_done.load()) {
                ++measured_msgs;
                const uint64_t recv_ts_ns = tob.recv_ts_ns() > 0 ? tob.recv_ts_ns() : 0;
                if (recv_ts_ns > 0 && t1_ns >= recv_ts_ns) {
                    e2e_samples.push_back(t1_ns - recv_ts_ns);
                }
                strat_samples.push_back(t1_ns - t0_ns);
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

        std::sort(e2e_samples.begin(), e2e_samples.end());
        std::sort(strat_samples.begin(), strat_samples.end());

        size_t n;
        size_t idx50;  // index of 50 percentile
        size_t idx99;  // index of 99 percentile
        size_t idx999; // index of 99.9 percentile

        // E2E latency metrics
        if (e2e_samples.empty()) {
            r.e2e_p50_ns = 0;
            r.e2e_p99_ns = 0;
            r.e2e_p999_ns = 0;
            r.e2e_max_ns = 0;
        } else {
            n = e2e_samples.size();
            idx50 = static_cast<size_t>(std::floor(0.5 * (n - 1)));
            r.e2e_p50_ns = e2e_samples[idx50];
            idx99 = static_cast<size_t>(std::floor(0.99 * (n - 1)));
            r.e2e_p99_ns = e2e_samples[idx99];
            idx999 = static_cast<size_t>(std::floor(0.999 * (n - 1)));
            r.e2e_p999_ns = e2e_samples[idx999];
            r.e2e_max_ns = e2e_samples.back();
        }

        // Strategy latency metrics
        if (strat_samples.empty()) {
            r.strat_p50_ns = 0;
            r.strat_p99_ns = 0;
            r.strat_p999_ns = 0;
            r.strat_max_ns = 0;
        } else {
            n = strat_samples.size();
            idx50 = static_cast<size_t>(std::floor(0.5 * (n - 1)));
            r.strat_p50_ns = strat_samples[idx50];
            idx99 = static_cast<size_t>(std::floor(0.99 * (n - 1)));
            r.strat_p99_ns = strat_samples[idx99];
            idx999 = static_cast<size_t>(std::floor(0.999 * (n - 1)));
            r.strat_p999_ns = strat_samples[idx999];
            r.strat_max_ns = strat_samples.back();
        }
    }

    return r;
}
