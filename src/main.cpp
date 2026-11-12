#include "benchmark.hpp"
#include "feed_health.hpp"
#include "log_observer.hpp"
#include "log_utils.hpp"
#include "market_data.pb.h"
#include "pipeline.hpp"
#include "replay_source.hpp"
#include "runtime/curl_global_guard.hpp"
#include "source_factory.hpp"
#include <chrono>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

extern "C" void rust_set_threshold(double thres);

int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    int exit_code = 0;
    try {
        CurlGlobalGuard curl_guard; // process-scope, init and cleanup
        std::optional<std::string> source = std::nullopt;
        std::string symbol = "btcusdt";
        std::optional<double> threshold = std::nullopt;

        bool bench = false;
        bool bench_live_checks = false;
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
            if (arg == "--bench-live-checks") {
                bench_live_checks = true;
            }
            if (arg == "--help" || arg == "-h") {
                std::ostringstream oss;
                oss << "Usage: " << argv[0] << " [options]\n\n"
                    << "Options:\n"
                    << "  --source <live|replay>   Data feed [live]\n"
                    << "  --symbol <symbol>        Symbol for live feed [btcusdt]\n"
                    << "  --threshold <value>      Spread threshold (live:[0.0000007], replay:[1.0])\n"
                    << "  --max-no-valid-ms <ms>   Stop if no valid tick for this long [10000]\n"
                    << "  --startup-grace-ms <ms>  Time allowed for the first valid tick [15000]\n"
                    << "  --bench                  Run benchmark mode (replay-only)\n"
                    << "  --bench-live-checks      Include live checks in the benchmark: validator,\n"
                    << "                           health monitor and watchdog [off]\n"
                    << "  --help, -h               Print this help";

                log_utils::log_to_stream(std::cout, oss.str());
                google::protobuf::ShutdownProtobufLibrary();
                return 0;
            }
        }
        if (bench_live_checks && !bench) {
            throw std::invalid_argument("--bench-live-checks requires --bench");
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
            const BenchmarkResult r = Benchmark::run(*mds, [&] {
                PipelineConfig c = PipelineConfig::checks(bench_live_checks);
                c.health_cfg = healthConfig;
                if (bench_live_checks) {
                    c.expected_symbol = ReplaySource::k_symbol; // live always checks the symbol
                }
                return c;
            }());
            r.print();
        } else {
            PipelineConfig cfg; // all checks on
            cfg.expected_symbol = expected_symbol;
            cfg.health_cfg = healthConfig;
            LogObserver observer;

            const PipelineStats stats = Pipeline(cfg, &observer).run(*mds);

            std::ostringstream oss;
            oss << "drops=" << stats.drops
                << " serialize_errs=" << stats.serialize_errs;
            log_utils::log_to_stream(std::cerr, oss.str());
            log_utils::log_to_stream(std::cerr, stats.validator_summary);
            log_utils::log_to_stream(std::cerr, stats.sequence_summary);
            if (stats.stop_reason != FeedStopReason::None) {
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
