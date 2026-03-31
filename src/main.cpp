#include "benchmark.hpp"
#include "log_utils.hpp"
#include "time_utils.hpp"
#include "market_data.pb.h"
#include "source_factory.hpp"
#include "tob_ring_buffer.hpp"
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
        std::string source = "live";
        std::string symbol = "dashbtc";
        std::optional<double> threshold = std::nullopt;

        bool bench = false;

        // Parse command-line arguments, supports --replay and --source
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
            if (arg == "--bench") {
                bench = true;
            }
            if (arg == "--help" || arg == "-h") {
                std::ostringstream oss;
                oss << "Usage: " << argv[0] << " [options]\n\n"
                    << "Options:\n"
                    << "  --source <live|replay>   Data feed [live]\n"
                    << "  --symbol <symbol>        Symbol for live feed [dashbtc]\n"
                    << "  --threshold <value>      Spread threshold (live:[0.0000007], replay:[1.0])\n"
                    << "  --bench                  Run benchmark mode\n"
                    << "  --help, -h               Print this help";

                log_utils::log_to_stream(std::cout, oss.str());
                google::protobuf::ShutdownProtobufLibrary();
                return 0;
            }
        }

        bool log_all = false;
        std::unique_ptr<MarketDataSource> mds;
        if (source == "replay") {
            mds = utils::make_source(utils::SourceType::Replay);
            log_utils::log_to_stream(std::cout, "Replay mode");
            if (!threshold) {
                threshold = 1.0; // Default for replay stub
            }
            log_all = true;
        } else if (source == "live") {
            mds = utils::make_source(utils::SourceType::Live, symbol);
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
                const BenchmarkResult r = Benchmark::run(*mds);
                r.print();
        } else {

            std::atomic<bool> done{false};
            std::atomic<uint64_t> drops{0};
            TobRingBuffer q;

            std::thread ingest([&] {
                hft::TopOfBook tob;
                while (mds->next(tob)) {
                    if (!q.push(tob)) { // Drop on full queue
                        // std::cerr << "Queue is full\n";
                        drops.fetch_add(1);
                    }
                }
                done.store(true);
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
                    tob.SerializeToString(&payload);

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
                    if (now - last_log >= std::chrono::seconds(1) || log_all) { // Limit logging rate
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
            process.join();
            std::ostringstream oss;
            oss << "drops=" << drops.load();
            log_utils::log_to_stream(std::cerr, oss.str());
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
