# HFT Engine Prototype

Prototype for an HFT engine, currently built around a multithreaded market-data pipeline in C++ with a Rust strategy module, Protobuf serialization across the FFI boundary, and latency instrumentation with percentile reporting.

Built to explore the design of a low-latency processing pipeline: decoupled producer/consumer threads, a lock-free data transfer mechanism, and measurable per-message latency across each stage.

## Problem

Two pipeline stages - data ingestion and strategy processing - need to run concurrently without blocking each other. A mutex-guarded queue would work, but it introduces contention: the ingest thread holds the lock while writing, the process thread holds it while reading, and they serialize each other unnecessarily.

The secondary consideration is the language boundary. The strategy module is written in Rust - a deliberate choice to work with C++/Rust FFI in practice. Once that boundary exists, the question is how to cross it safely. Passing a raw C++ struct across FFI requires both sides to agree on memory layout - field sizes, padding, byte order. That agreement is fragile: a compiler update or a flag change can silently break it. This is the ABI stability problem. A better approach keeps the boundary explicit and independent of either side's memory layout.

## Solution

The ingest and process threads are connected by a single-producer/single-consumer ring buffer. The ingest thread writes; the process thread reads. Each atomic index is exclusively written by one thread - `head_` by the producer, `tail_` by the consumer - so there is no contention on the data path. A full buffer drops the incoming frame and increments a counter - no blocking.

Data crosses the C++/Rust boundary as a serialized Protobuf message. The C++ side serializes the `TopOfBook` struct; the Rust side decodes it using the same `.proto` schema compiled independently via `prost`. Neither side needs to know how the other lays out memory in the process.

## Architecture

```
MarketDataSource -> [SPSC Ring Buffer] -> Strategy Engine -> StrategySignal -> Log
  (ingest thread)                         (process thread)
```

**Ingest thread** - calls `MarketDataSource::next()` in a loop. On live mode, this blocks on `curl_ws_recv` until a WebSocket frame arrives, then parses the JSON payload into a `TopOfBook` and stamps `recv_ts_ns` immediately. On replay mode, it cycles through a fixed set of stub ticks with configurable sleep between them. Pushes to the ring buffer; increments a drop counter on full.

**Process thread** - pops `TopOfBook` from the ring buffer, serializes it to Protobuf bytes, calls `rust_decide()` via FFI, and logs the returned action as a `StrategySignal`. On empty buffer, yields and retries; exits when the ingest thread signals done.

**Ring buffer (`TobRingBuffer`)** - fixed capacity (1024 slots), `std::atomic` head and tail. `push` checks `next == tail` before writing; `pop` checks `tail == head` before reading. Returns `false` on full/empty rather than blocking.

**Strategy module (Rust, `rust_decide`)** - receives serialized `TopOfBook` bytes, decodes with `prost`, evaluates spread against a configurable threshold, returns one of: `QuoteBoth` (spread >= threshold), `Hold` (spread < threshold), or `CancelAll` (invalid book: non-positive prices or ask < bid). Threshold is set once from C++ via `rust_set_threshold` before the pipeline starts.

**Source abstraction** - `MarketDataSource` is a pure virtual interface. `LiveWebSocketSource` and `ReplaySource` both implement `next(TopOfBook&)`. The factory (`source_factory.cpp`) constructs the appropriate source based on the `--source` flag. Adding a new source (e.g., CSV file, different exchange) does not touch the pipeline.

## Latency Instrumentation

`recv_ts_ns` is stamped in the ingest thread immediately after a message is parsed from the wire. In the process thread, `t0` is stamped before the FFI call and `t1` after. This gives two independent measurements per message:

- `e2e_ns = t1 - recv_ts_ns` - full pipeline latency: parse -> ring buffer transit -> serialize -> FFI call
- `strat_ns = t1 - t0` - cost of the `rust_decide` FFI call only
In normal mode these are logged at a rate-limited 1s interval. In benchmark mode they are collected across the full measurement window and reported as percentiles.

## Benchmark Mode

```bash
./build/debug/hft --bench
```

Runs against the replay source (stub ticks, no sleep between ticks). Rejects `--source` values other than `replay`.

By default the benchmark ingest path skips `TobValidator`, so results stay comparable with earlier baselines. Pass `--bench-validate` to run the validator in the ingest thread as the live path does; its cost then lands in `e2e_ns`. The output records `validate=on|off`. Phases:

- **Warmup**: 3 seconds, samples discarded
- **Measurement**: 20 seconds of samples collected per metric

Reports on completion:

```
Benchmark: measured_msgs=... measured_seconds=20 throughput_msgs_per_sec=... drops=... validate=off rejected=0

Benchmark Latency: e2e_p50_ns=... e2e_p99_ns=... e2e_p999_ns=... e2e_max_ns=... strat_p50_ns=... strat_p99_ns=... strat_p999_ns=... strat_max_ns=...
```

The benchmark is designed to establish a reproducible baseline. The intent is to use this baseline to track the effect of future pipeline changes.

## Sample Output (Live Mode)

```
Live mode
Diagnostics: dt_ms=8972 recv=1 recv_rate[msg/s]=0 parse_ok=1 d_parse_ok=1 parse_err=0 d_parse_err=0 reconn=0 d_reconn=0
Signal: symbol=DASHBTC action=HOLD reason=spread_cond ts_ns=231418184649719
Latency: e2e_ns=58400 strat_ns=2800
Signal: symbol=DASHBTC action=QUOTE_BOTH reason=spread_cond ts_ns=231423000418448
```

Diagnostics are logged every 5 seconds (cumulative and delta). Signals and latency are logged at most once per second.

## Build

All configurations live in `CMakePresets.json` — presets are the single source of
truth shared by the dev script, CI and the IDE. Run `cmake --list-presets` to see them.

**Pinned dependencies (recommended).** C++ dependencies are resolved from the
`vcpkg.json` manifest, locked to a vcpkg baseline commit, so every machine builds
the same versions:

```bash
export VCPKG_ROOT=/path/to/vcpkg      # https://github.com/microsoft/vcpkg
cmake --preset debug
cmake --build --preset debug -j
# or
scripts/dev.sh debug
```

**System-provided dependencies.** If you would rather use your distro's packages,
use the `sys-*` presets, which skip vcpkg entirely:

```bash
scripts/dev.sh sys-debug
```

Build output goes to `build/<preset>/`. `scripts/dev.sh` additionally symlinks the
active `compile_commands.json` into the repo root for clang-tidy and editors.

**Toolchain requirements:**
* CMake 3.25+
* C++17 (GCC/Clang)
* Rust + Cargo — version pinned by `rust-toolchain.toml`; `rustup` picks it up
  automatically. Crate versions are pinned by the tracked `strategy_engine/Cargo.lock`
  and the build runs `cargo build --locked`.

**Libraries** (pinned via `vcpkg.json`, or install yourself for the `sys-*` presets):
Protobuf, abseil, libcurl (with WebSocket support), nlohmann/json, and GoogleTest
(tests only; disable with `-DHFT_BUILD_TESTS=OFF`).

## Test

```bash
ctest --preset debug
# or build and test in one step
scripts/dev.sh debug --test
```

Rust strategy tests: `cd strategy_engine && cargo test`.

Unit tests live in `tests/` and use GoogleTest via CTest. The test binary links only the Protobuf library and the code under test, not the Rust module or libcurl.

### Sanitizers

```bash
scripts/dev.sh asan-ubsan --test   # AddressSanitizer + UndefinedBehaviorSanitizer
scripts/dev.sh tsan --test         # ThreadSanitizer
```

The unit tests are single-threaded and never touch the SPSC ring buffer, so a
sanitizer run should also exercise the real ingest/process thread pair:

```bash
./build/tsan/hft --bench
```

Use the vcpkg presets for this. Against a distro-provided libprotobuf,
AddressSanitizer reports a spurious crash inside the uninstrumented shared
library during generated-message destruction.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main` and every pull request:

| Job | What it checks |
| --- | --- |
| `format` | `clang-format` (pinned version, via `scripts/format.sh --check`) |
| `rust` | `cargo fmt --check`, `cargo clippy -D warnings`, `cargo test` (against the vcpkg-pinned `protoc`) |
| `tidy` | `clang-tidy` on lines changed by the PR (`scripts/tidy.sh`) |
| `docker` | Builds the image end to end and smoke-tests the binary |
| `build-test` | Builds and runs CTest for the `debug` and `release` presets |

`.github/workflows/nightly.yml` runs the `asan-ubsan` and `tsan` presets
nightly, covering both the unit tests and a benchmark-mode pipeline run.
Suppression files live in `.github/sanitizers/`.

### Branch protection

`main` is protected by a repository ruleset whose definition is kept in
`.github/rulesets/main.json`: changes land only through a squash-merged pull
request with every `ci.yml` job green against the current `main`, all review
threads resolved, and no force-pushes or branch deletion. The nightly workflow
is not required, since it does not run on most pull requests. There are no
bypass actors, so the rules also apply to the repository owner. The file is
not applied automatically; after editing it, push it to GitHub with:

```bash
id=$(gh api repos/{owner}/{repo}/rulesets --jq '.[] | select(.name=="main") | .id')
gh api -X PUT "repos/{owner}/{repo}/rulesets/$id" --input .github/rulesets/main.json
```

### Local equivalents

```bash
scripts/format.sh --check        # same check as the format job
scripts/format.sh                # reformat in place
scripts/tidy.sh                  # changed lines vs origin/main
scripts/tidy.sh --all            # whole tree (not enforced by CI)
```

`clang-tidy` runs against changed lines only. The existing code has not been
audited, so a full-tree gate would fail from the start and end up ignored;
checking the lines a change touches keeps the gate actionable. Both scripts
pin their tool version, because diagnostics differ between releases.

### Dependency updates

Dependabot (`.github/dependabot.yml`) proposes weekly updates for Cargo
crates, GitHub Actions and the Docker base image. The vcpkg baseline has no
Dependabot support and is bumped with:

```bash
scripts/update-vcpkg-baseline.sh            # latest vcpkg release
scripts/update-vcpkg-baseline.sh 2026.10.16 # a specific release
```

That script updates `vcpkg.json` and the `Dockerfile` together; the Docker
build fails if the two ever disagree.

## Run

```bash
./build/debug/hft --help

# Live feed (default symbol: btcusdt)
./build/debug/hft --source live --symbol btcusdt

# Replay (stub ticks, loops indefinitely)
./build/debug/hft --source replay

# Custom spread threshold
./build/debug/hft --threshold 0.01

# Benchmark mode
./build/debug/hft --bench

# Feed health limits (stop conditions)
./build/debug/hft --max-no-valid-ms 10000 --startup-grace-ms 15000
```

**Feed health.** Outside benchmark mode, a watchdog stops the pipeline if no valid tick arrives within `--max-no-valid-ms` (default 10s), or within `--startup-grace-ms` (default 15s) before the first one. The stop is logged with a reason and exits with code `2`:

- `invalid_data` - messages arrived but were all rejected or failed to parse (e.g. a persistent `symbol_mismatch`)
- `stale_feed` - connection alive (heartbeat frames) but no market data
- `dead_feed` - no frames at all: disconnected, stuck reconnecting, or a silent source

**Ingest checks.** Every tick passes `TobValidator` (symbol, finite/positive prices and quantities, no crossed or locked book) and then `SequenceTracker`. Update IDs must increase within a source session; duplicates, backwards IDs and missing IDs are dropped. Each (re)connect or replay loop starts a new `session`, which resets the baseline. Rejections are counted per reason and summarized at shutdown.

**Feed status to the strategy.** Each `TopOfBook` carries `feed_status`: `FEED_OK`, `FEED_SESSION_START` (first tick after a (re)connect) or `FEED_HALTED` (sent once when the watchdog stops the feed). The strategy returns `CANCEL_ALL` for any status other than OK/SESSION_START, and for undecodable messages and non-finite, crossed or locked books.

Default thresholds if not specified: live `0.0000007`, replay `1.0`.

## Project Structure

```
src/                    Pipeline implementation: main, threading, ingest, benchmark
include/                Headers (ring buffer, sources, utilities)
include/runtime/        Process-scope RAII guards (CurlGlobalGuard)
proto/                  Protobuf schema: TopOfBook, StrategySignal, QuoteAction
strategy_engine/        Rust FFI module (lib.rs, build.rs for prost codegen)
scripts/                Build helpers and benchmark runner
tests/                  GoogleTest unit tests (CTest)
```

## Status

Working pipeline: live WebSocket ingest, replay mode, and benchmark mode with percentile reporting. No order/quote output - signals are logged only. Replay source uses hardcoded stub ticks.

## Roadmap

The project is intended to develop into a full HFT engine. Planned capabilities and engineering work are tracked in [ROADMAP.md](ROADMAP.md).
