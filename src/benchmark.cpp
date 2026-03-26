#include "benchmark.hpp"
#include "market_data.pb.h"
#include "tob_ring_buffer.hpp"
#include <chrono>
#include <thread>
#include <atomic>

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

BenchmarkResult Benchmark::run(MarketDataSource& src) {
    BenchmarkResult r{};
    const auto start = std::chrono::steady_clock::now();
    const auto warmup_end = start + warmup_seconds;
    const auto measure_end = warmup_end + measure_seconds;

    TobRingBuffer q;

    std::thread ingest([&] {
        hft::TopOfBook tob;
        std::atomic<bool> ready{false};
        while (src.next(tob)) {
            if (!ready.load() && std::chrono::steady_clock::now() >= warmup_end) {
                ready.store(true);
            }

            if (!q.push(tob)) {
                if (ready.load()) {
                    ++r.drops;
                }
            }
        }
    });

    std::thread process([&] {
        std::string payload;
        hft::TopOfBook tob;
        while (true) {
            if (!q.pop(tob)) {
                std::this_thread::yield();
                continue;
            }
            tob.SerializeToString(&payload);

            int32_t action = rust_decide(
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

            const auto t1 = std::chrono::steady_clock::now();
            const auto t1_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1.time_since_epoch()).count();
            hft::StrategySignal sig;
            sig.set_symbol(tob.symbol());
            sig.set_action(static_cast<hft::QuoteAction>(action));
            sig.set_reason("spread_cond");
            sig.set_ts_ns(static_cast<uint64_t>(t1_ns));
        }
    });

    ingest.join();
    process.join();

    return r;
}
