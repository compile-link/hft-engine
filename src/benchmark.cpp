#include "benchmark.hpp"
#include "log_utils.hpp"
#include "time_utils.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace {
    // Keeps samples whose e2e stop falls inside [window_start, window_end).
    // Window membership is decided from the span timestamps, so the pipeline needs no window logic.
    class SamplingObserver final : public PipelineObserver {
      public:
        SamplingObserver(uint64_t window_start_ns, uint64_t window_end_ns)
            : start_(window_start_ns), end_(window_end_ns) {
            e2e_.reserve(250000);
            strat_.reserve(250000);
        }

        void on_message(const MessageSpans& s) override {
            if (s.t1_ns < start_ || s.t1_ns >= end_) {
                return;
            }
            ++msgs_;
            if (s.recv_ts_ns > 0 && s.t1_ns >= s.recv_ts_ns) {
                e2e_.push_back(s.t1_ns - s.recv_ts_ns);
            }
            strat_.push_back(s.strat_end_ns - s.t0_ns);
        }

        void on_drop(uint64_t now_ns) override {
            if (now_ns >= start_ && now_ns < end_) {
                drops_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Read only after Pipeline::run returns (threads joined)
        uint64_t msgs() const { return msgs_; }
        uint64_t drops() const { return drops_.load(); }
        std::vector<uint64_t>& e2e() { return e2e_; }
        std::vector<uint64_t>& strat() { return strat_; }

      private:
        uint64_t start_, end_;
        uint64_t msgs_ = 0;                 // process thread
        std::atomic<uint64_t> drops_{0};    // ingest thread
        std::vector<uint64_t> e2e_, strat_; // process thread
    };

    struct Percentiles {
        uint64_t p50 = 0, p99 = 0, p999 = 0, max = 0;
    };

    Percentiles percentiles(std::vector<uint64_t>& v) {
        Percentiles p;
        if (v.empty()) {
            return p;
        }
        std::sort(v.begin(), v.end());
        const size_t n = v.size();
        p.p50 = v[(n - 1) * 500u / 1000u];
        p.p99 = v[(n - 1) * 990u / 1000u];
        p.p999 = v[(n - 1) * 999u / 1000u];
        p.max = v.back();
        return p;
    }
} // namespace

void BenchmarkResult::print() const {
    std::ostringstream oss;
    oss << "Benchmark:"
        << " measured_msgs=" << measured_msgs
        << " measured_seconds=" << measured_seconds
        << " throughput_msgs_per_sec=" << throughput_msgs_per_sec
        << " drops=" << drops
        << " validate=" << (validate ? "on" : "off")
        << " health=" << (health ? "on" : "off")
        << " watchdog=" << (watchdog ? "on" : "off")
        << " rejected=" << rejected;
    if (watchdog_stop != FeedStopReason::None) {
        oss << " watchdog_stop=" << feed_stop_reason_to_str(watchdog_stop);
    }
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

BenchmarkResult Benchmark::run(MarketDataSource& src, const PipelineConfig& cfg) {
    using std::chrono::duration_cast;
    using std::chrono::nanoseconds;
    const uint64_t start_ns = time_utils::now_ns();
    const uint64_t window_start_ns = start_ns + static_cast<uint64_t>(duration_cast<nanoseconds>(warmup_seconds).count());
    const uint64_t window_end_ns = window_start_ns + static_cast<uint64_t>(duration_cast<nanoseconds>(measure_seconds).count());

    SamplingObserver observer(window_start_ns, window_end_ns);
    Pipeline pipeline(cfg, &observer);

    // Stops the source once the window has closed; wakes early if the pipeline ends first
    std::mutex m;
    std::condition_variable cv;
    bool finished = false;
    std::thread timer([&] {
        std::unique_lock<std::mutex> lk(m);
        const auto deadline = std::chrono::steady_clock::now() + warmup_seconds + measure_seconds;
        if (!cv.wait_until(lk, deadline, [&] { return finished; })) {
            src.request_stop();
        }
    });

    const PipelineStats stats = pipeline.run(src);
    {
        std::lock_guard<std::mutex> lk(m);
        finished = true;
    }
    cv.notify_one();
    timer.join();

    BenchmarkResult r{};
    r.validate = cfg.validate;
    r.health = cfg.health;
    r.watchdog = cfg.watchdog;
    r.watchdog_stop = stats.stop_reason;
    r.rejected = stats.rejected;
    r.serialize_errs = stats.serialize_errs;
    r.drops = observer.drops();
    r.measured_msgs = observer.msgs();
    r.measured_seconds = std::chrono::duration<double>(measure_seconds).count();
    r.throughput_msgs_per_sec = static_cast<double>(r.measured_msgs) / r.measured_seconds;

    const Percentiles e2e = percentiles(observer.e2e());
    r.e2e_p50_ns = e2e.p50;
    r.e2e_p99_ns = e2e.p99;
    r.e2e_p999_ns = e2e.p999;
    r.e2e_max_ns = e2e.max;
    const Percentiles strat = percentiles(observer.strat());
    r.strat_p50_ns = strat.p50;
    r.strat_p99_ns = strat.p99;
    r.strat_p999_ns = strat.p999;
    r.strat_max_ns = strat.max;
    return r;
}
