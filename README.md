# HFT Engine Prototype

Multithreaded market data pipeline in C++ with a Rust strategy module, Protobuf serialization across the FFI boundary, and latency instrumentation with percentile reporting.

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
./build/hft --bench
```

Runs against the replay source (stub ticks, no sleep between ticks). Rejects `--source` values other than `replay`. Phases:

- **Warmup**: 3 seconds, samples discarded
- **Measurement**: 20 seconds of samples collected per metric

Reports on completion:

```
Benchmark: measured_msgs=... measured_seconds=20 throughput_msgs_per_sec=... drops=...

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

```bash
cmake -S . -B build
cmake --build build -j
# or
scripts/dev.sh
```

**Dependencies:**
* CMake 3.10+
* C++17 (GCC/Clang)
* Protobuf
* libcurl
* Rust + Cargo
* nlohmann/json

## Run

```bash
./build/hft --help

# Live feed (default symbol: dashbtc)
./build/hft --source live --symbol dashbtc

# Replay (stub ticks, loops indefinitely)
./build/hft --source replay

# Custom spread threshold
./build/hft --threshold 0.01

# Benchmark mode
./build/hft --bench
```

Default thresholds if not specified: live `0.0000007`, replay `1.0`.

## Project Structure

```
src/                    Pipeline implementation: main, threading, ingest, benchmark
include/                Headers (ring buffer, sources, utilities)
include/runtime/        Process-scope RAII guards (CurlGlobalGuard)
proto/                  Protobuf schema: TopOfBook, StrategySignal, QuoteAction
strategy_engine/        Rust FFI module (lib.rs, build.rs for prost codegen)
scripts/                Build helpers and benchmark runner
```

## Status

Working pipeline: live WebSocket ingest, replay mode, benchmark mode with percentile reporting. No order/quote output - signals are logged only. Replay source uses hardcoded stub ticks (CSV loading planned). Planned work: order output stage, expanded strategy logic, deeper latency profiling.
