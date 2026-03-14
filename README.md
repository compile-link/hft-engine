# HFT Engine Prototype

Low-latency market data processor with a C++ pipeline and Rust strategy module

## Features
- Live Binance data ingest (WebSocket via libcurl)
- Replay mode (stub ticks)
- Multithreaded ingest and strategy pipeline using SPSC ring buffer
- Rust strategy module accessed by FFI
- Log system for StrategySignal (output) and diagnostics, rate-limited, synchronized across threads with mutex guard
- Latency metrics (end-to-end and strategy compute time)


## Sample Output
```
Live mode
Diagnostics: dt_ms=8972 recv=1 recv_rate[msg/s]=0 parse_ok=1 d_parse_ok=1 parse_err=0 d_parse_err=0 reconn=0 d_reconn=0
Signal: symbol=DASHBTC action=HOLD reason=spread_cond ts_ns=231418184649719
Latency: e2e_ns=58400 strat_ns=2800
Signal: symbol=DASHBTC action=QUOTE_BOTH reason=spread_cond ts_ns=231423000418448
```

## Requirements
- CMake 3.10+
- C++20 compiler (GCC/Clang)
- Protobuf
- libcurl
- Rust + Cargo
- nlohmann/json

## Build

```bash
cmake -S . -B build
cmake --build build -j

# or

scripts/dev.sh
```

## Run

```bash
./build/hft
./build/hft --help

# Live (default)
./build/hft --source live --symbol dashbtc

# Replay
./build/hft --source replay

# Custom threshold
./build/hft --threshold 0.01
```

## Project Structure

- src/ implementation files (core runtime, threading, ring buffer)
- include/ headers
- proto/ protobuf schema
- strategy_engine/ Rust module strategy (FFI)
- CMakeLists.txt build config

## Architecture

### Data Flow
MarketDataSource -> Ring Buffer -> Strategy Engine -> StrategySignal -> Log

### Threads
- Ingest: Pulls data from live exchange and pushes parsed data (TopOfBook) to ring buffer
- Process: Pops parsed data (TopOfBook) from ring buffer, serializes (C++ pipeline) and deserializes (Rust module), processes the strategy and logs StrategySignal

### Ring Buffer
Single-producer/single-consumer ring buffer with fixed capacity (1024), drops frames when full (drops counted)

### Strategy Engine (Rust)
`rust_decide` takes serialized TopOfBook protobuf and returns QuoteAction which value is determined by the spread condition, checking it against spread threshold (configurable via CLI argument). Possible outputs:
- QuoteBoth - when spread is greater or equal to threshold
- Hold - when spread is lower than threshold
- CancelAll - invalid book

### Logging
- All logs are routed through `log_utils::log_to_stream` for thread-safe output
- Logs are rate-limited, signal 1s, diagnostics 5s

## Notes

- Replay mode uses stub ticks
- Replay default threshold = 1.0; live default = 0.0000007 (customizable)
- WebSocket uses individual symbol bookTicker stream
- Received messages are parsed with nlohmann json parser library
- Signal timestamp uses steady_clock (monotonic)
- Latency metrics: e2e_ns = now - recv_ts_ns, strat_ns = rust_decide duration
