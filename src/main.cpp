#include "benchmark.hpp"
#include "feed_health.hpp"
#include "log_utils.hpp"
#include "market_data.pb.h"
#include "replay_source.hpp"
#include "runtime/curl_global_guard.hpp"
#include "source_factory.hpp"
#include "time_utils.hpp"
#include "tob_ring_buffer.hpp"
#include "sequence_tracker.hpp"
#include "tob_validator.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);
extern "C" void rust_set_threshold(double thres);

static const char* action_to_str(int32_t a) {
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

int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    int exit_code = 0;
    try {
        CurlGlobalGuard curl_guard; // process-scope, init and cleanup
        std::optional<std::string> source = std::nullopt;
        std::string symbol = "dashbtc";
        std::optional<double> threshold = std::nullopt;

        bool bench = false;
        bool bench_validate = false;
        ReplayConfig replayConfig;
        FeedHealthConfig healthConfig;

        // Parse command-line arguments
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--source" && i + 1 < argc) {
                source = argv[++i];
            }
            if (arg == "--symbol" && i + 1 < argc) {
                symbol = argv[++i];
            }
            if (arg == "--threshold" && i + 1 < argc) {
                threshold = std::stod(argv[++i]);
            }
            if (arg == "--max-no-valid-ms" && i + 1 < argc) {
                healthConfig.max_no_valid = std::chrono::milliseconds(std::stoll(argv[++i]));
            }
            if (arg == "--startup-grace-ms" && i + 1 < argc) {
                healthConfig.startup_grace = std::chrono::milliseconds(std::stoll(argv[++i]));
            }
            if (arg == "--bench") {
                bench = true;
                replayConfig.loop = true;
                replayConfig.sleep_per_tick = std::chrono::milliseconds(0);
                replayConfig.max_runtime = std::nullopt;
            }
            if (arg == "--bench-validate") {
                bench_validate = true;
            }
            if (arg == "--help" || arg == "-h") {
                std::ostringstream oss;
                oss << "Usage: " << argv[0] << " [options]\n\n"
                    << "Options:\n"
                    << "  --source <live|replay>   Data feed [live]\n"
                    << "  --symbol <symbol>        Symbol for live feed [dashbtc]\n"
                    << "  --threshold <value>      Spread threshold (live:[0.0000007], replay:[1.0])\n"
                    << "  --max-no-valid-ms <ms>   Stop if no valid tick for this long [10000]\n"
                    << "  --startup-grace-ms <ms>  Time allowed for the first valid tick [15000]\n"
                    << "  --bench                  Run benchmark mode (replay-only)\n"
                    << "  --bench-validate         Run TobValidator in the benchmark ingest path [off]\n"
                    << "  --help, -h               Print this help";

                log_utils::log_to_stream(std::cout, oss.str());
                google::protobuf::ShutdownProtobufLibrary();
                return 0;
            }
        }
        if (bench_validate && !bench) {
            throw std::invalid_argument("--bench-validate requires --bench");
        }
        if (bench) {
            if (source && *source != "replay") {
                throw std::invalid_argument("Benchmark available only in replay mode");
            }
            source = "replay";
        }
        if (!source) {
            source = "live";
        }

        std::unique_ptr<MarketDataSource> mds;
        std::optional<std::string> expected_symbol = std::nullopt;
        if (source == "replay") {
            mds = utils::make_replay_source(replayConfig);
            log_utils::log_to_stream(std::cout, "Replay mode");
            if (!threshold) {
                threshold = 1.0; // Default for replay stub
            }
        } else if (source == "live") {
            mds = utils::make_live_source(symbol);
            expected_symbol = symbol;
            log_utils::log_to_stream(std::cout, "Live mode");
            if (!threshold) {
                threshold = 0.0000007; // Default for live feed
            }
        }

        if (!mds) {
            throw std::invalid_argument("Invalid source");
        }

        if (threshold) {
            rust_set_threshold(*threshold);
        }

        if (bench) {
            const BenchmarkResult r = Benchmark::run(*mds, bench_validate);
            r.print();
        } else {

            std::atomic<bool> done{false};
            std::atomic<uint64_t> drops{0};
            uint64_t serialize_errs = 0;
            TobRingBuffer q;
            TobValidator validator(expected_symbol); // ingest thread only
            SequenceTracker seq_tracker;             // ingest thread only
            FeedHealthMonitor health(healthConfig, time_utils::now_ns());
            std::atomic<FeedStopReason> stop_reason{FeedStopReason::None};

            std::thread ingest([&] {
                hft::TopOfBook tob;
                auto last_reject_log = std::chrono::steady_clock::time_point{};
                auto last_session_log = std::chrono::steady_clock::time_point{};
                std::string last_symbol = expected_symbol.value_or("");
                while (mds->next(tob)) {
                    const TobReject rej = validator.validate(tob);
                    if (rej != TobReject::None) {
                        health.on_rejected(time_utils::now_ns());
                        const auto now = std::chrono::steady_clock::now();
                        if (now - last_reject_log >= std::chrono::seconds(1)) { // Limit logging rate
                            std::ostringstream oss;
                            oss << "Rejected:"
                                << " symbol=" << tob.symbol()
                                << " reason=" << tob_reject_to_str(rej)
                                << " seq=" << tob.sequence()
                                << " total_rejected=" << validator.rejected_total();
                            log_utils::log_to_stream(std::cerr, oss.str());
                            last_reject_log = now;
                        }
                        continue;
                    }
                    const SeqResult sr = seq_tracker.check(tob);
                    if (!seq_accepted(sr)) {
                        health.on_rejected(time_utils::now_ns());
                        const auto now = std::chrono::steady_clock::now();
                        if (now - last_reject_log >= std::chrono::seconds(1)) { // Shares the reject log rate limit
                            std::ostringstream oss;
                            oss << "Sequence rejected:"
                                << " symbol=" << tob.symbol()
                                << " reason=" << seq_result_to_str(sr)
                                << " seq=" << tob.sequence()
                                << " session=" << tob.session()
                                << " total_rejected=" << seq_tracker.rejected_total();
                            log_utils::log_to_stream(std::cerr, oss.str());
                            last_reject_log = now;
                        }
                        continue;
                    }
                    if (sr == SeqResult::SessionStart && tob.session() > 1 &&
                        std::chrono::steady_clock::now() - last_session_log >= std::chrono::seconds(1)) {
                        last_session_log = std::chrono::steady_clock::now();
                        std::ostringstream oss;
                        oss << "Sequence: new session"
                            << " symbol=" << tob.symbol()
                            << " session=" << tob.session()
                            << " seq=" << tob.sequence();
                        log_utils::log_to_stream(std::cerr, oss.str());
                    }
                    health.on_valid(time_utils::now_ns());
                    tob.set_feed_status(sr == SeqResult::SessionStart ? hft::FEED_SESSION_START : hft::FEED_OK);
                    last_symbol = tob.symbol();
                    if (!q.push(tob)) { // Drop on full queue
                        // std::cerr << "Queue is full\n";
                        drops.fetch_add(1);
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

            // Watchdog: next() may block indefinitely (reconnect loop, silent socket),
            // so health is evaluated off the ingest thread and the source is stopped from here
            std::thread watchdog([&] {
                while (!done.load()) {
                    const SourceActivity& act = mds->activity();
                    const FeedHealthSnapshot snap = health.snapshot(
                        time_utils::now_ns(),
                        act.last_frame_ns.load(std::memory_order_relaxed),
                        act.last_parse_err_ns.load(std::memory_order_relaxed));
                    const FeedStopReason r = health.evaluate(snap);
                    if (r != FeedStopReason::None) {
                        stop_reason.store(r);
                        std::ostringstream oss;
                        oss << "Feed health: stopping"
                            << " reason=" << feed_stop_reason_to_str(r)
                            << " " << describe_feed_health(snap);
                        log_utils::log_to_stream(std::cerr, oss.str());
                        mds->request_stop();
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });

            std::thread process([&] {
                std::string payload;
                hft::TopOfBook tob;
                auto last_log = std::chrono::steady_clock::now();
                while (true) {
                    if (!q.pop(tob)) {
                        // std::cerr << "Queue is empty\n";
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

                    auto t0 = time_utils::now_ns();
                    int32_t action = rust_decide(
                        reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

                    auto t1 = time_utils::now_ns();
                    hft::StrategySignal sig;
                    sig.set_symbol(tob.symbol());
                    sig.set_action(static_cast<hft::QuoteAction>(action));
                    sig.set_reason("spread_cond");
                    sig.set_ts_ns(t1);

                    auto now = std::chrono::steady_clock::now();
                    if (now - last_log >= std::chrono::seconds(1)) { // Limit logging rate
                        std::ostringstream oss;
                        oss << "Signal:"
                            << " symbol=" << sig.symbol()
                            << " action=" << action_to_str(action)
                            << " reason=" << sig.reason()
                            << " ts_ns=" << sig.ts_ns();
                        log_utils::log_to_stream(std::cout, oss.str());
                        oss.str("");
                        oss.clear();
                        const auto recv_ns = tob.recv_ts_ns();
                        const auto e2e_ns = (recv_ns > 0) ? (t1 - recv_ns) : 0;
                        oss << "Latency:"
                            << " e2e_ns=" << e2e_ns
                            << " strat_ns=" << t1 - t0;
                        log_utils::log_to_stream(std::cerr, oss.str());
                        last_log = now;
                    }
                }
            });

            ingest.join();
            watchdog.join();
            process.join();
            std::ostringstream oss;
            oss << "drops=" << drops.load()
                << " serialize_errs=" << serialize_errs;
            log_utils::log_to_stream(std::cerr, oss.str());
            log_utils::log_to_stream(std::cerr, validator.summary());
            log_utils::log_to_stream(std::cerr, seq_tracker.summary());
            if (stop_reason.load() != FeedStopReason::None) {
                exit_code = 2; // Distinguish feed-health stop from normal end and errors
            }
        };
    } catch (const std::exception& e) {
        log_utils::log_to_stream(std::cerr, "Error: " + std::string(e.what()));
        exit_code = 1;
    } catch (...) {
        log_utils::log_to_stream(std::cerr, "Error occurred");
        exit_code = 1;
    }

    google::protobuf::ShutdownProtobufLibrary();

    return exit_code;
}
