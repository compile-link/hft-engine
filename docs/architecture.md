# Pipeline Architecture

How market data moves through the engine, the components that make up the pipeline, and how live runs and the benchmark share one implementation of it.

## Overview

Every mode (live, replay, benchmark) runs the same `Pipeline`. Modes differ only in the three things plugged into it:

```
        MarketDataSource          PipelineConfig            PipelineObserver
        (where data comes from)   (which checks run)        (what is done with results)
                 │                        │                          │
                 └────────────────────────┼──────────────────────────┘
                                          ▼
                                 ┌──────────────────┐
                                 │  Pipeline final  │  one implementation of the measured path
                                 └──────────────────┘
```

| Mode | Source | Config | Observer |
|---|---|---|---|
| Live | `LiveWebSocketSource` | all checks on, expected symbol from `--symbol` | `LogObserver` |
| Replay | `ReplaySource` (paced) | all checks on | `LogObserver` |
| Benchmark, pipeline | `ReplaySource` (unpaced, looping) | all checks off | sampling observer |
| Benchmark, live-like (`--bench-live-checks`) | `ReplaySource` (unpaced, looping) | all checks on, expected symbol `BTCUSDT` | sampling observer |

The rule behind this split: **the pipeline itself is never specialised.** It is `final`, and anything that varies between modes is a component passed in. A mode-specific copy of the pipeline, or a subclass overriding a stage, would let the benchmark measure code that live never runs.

## Dataflow

```
 INGEST THREAD                                                     PROCESS THREAD
 ─────────────                                                     ──────────────
 MarketDataSource::next()                                          pop
   frame arrives (live: WebSocket, replay: stub frame)               │
   recv_ts_ns stamped ◄─────────────────────── e2e start             ▼
   book_ticker::parse  JSON → TopOfBook                            SerializeToString
   │                                                                 │
   ▼                                                                 ▼  ◄──────────── strat start (t0)
 TobValidator::validate ──reject──► on_validation_reject           rust_decide (FFI → Rust)
   │                                                                 │  ◄──────────── strat stop (strat_end)
   ▼                                                                 ▼
 SequenceTracker::check ──reject──► on_sequence_reject             build StrategySignal
   │          └─ SessionStart ───► on_session_start                  │  ◄──────────── e2e stop (t1)
   ▼                                                                 ▼
 FeedHealthMonitor::on_valid                                       on_message(signal, spans)
   │
   ▼
 set feed_status
   │
   ▼
 push ──────────► [ TobRingBuffer, SPSC, 1024 slots ] ──────────► pop
   └─full──► on_drop

 WATCHDOG THREAD
 ───────────────
 every 100 ms: FeedHealthMonitor::snapshot + evaluate
   └─ unhealthy ──► on_feed_stop ──► MarketDataSource::request_stop()
```

The checks marked as rejects, the health update and the watchdog only run when `PipelineConfig` enables them. With checks off, every parsed tick goes straight to the ring buffer with `feed_status = FEED_OK`.

Span definitions (start and stop points, and what each covers) are in the README's [Latency Instrumentation](../README.md#latency-instrumentation) section.

## Components

### `Pipeline` (`include/pipeline.hpp`)

```cpp
Pipeline(PipelineConfig cfg, PipelineObserver* observer = nullptr);
PipelineStats run(MarketDataSource& src);   // blocks until the source ends or is stopped
```

Owns everything with per-run state: the ring buffer, `TobValidator`, `SequenceTracker`, `FeedHealthMonitor`, and the three threads. `run()` starts the threads, joins them, and returns `PipelineStats`.

The constructor rejects a config with the watchdog on and health off, because the watchdog has nothing to evaluate without it.

### `PipelineConfig`

```cpp
struct PipelineConfig {
    bool validate = true;   // TobValidator + SequenceTracker
    bool health   = true;   // FeedHealthMonitor updated per message
    bool watchdog = true;   // watchdog thread; requires health
    std::optional<std::string> expected_symbol;
    FeedHealthConfig health_cfg;   // --max-no-valid-ms, --startup-grace-ms

    static PipelineConfig checks(bool on);   // all three switches together
};
```

Each check is a separate switch. The CLI sets them together for now (`--bench-live-checks` for the benchmark; normal runs always have all of them on).

Config holds only settings a real deployment might choose. Measurement concerns, such as warmup or sample collection, don't belong here; they go in an observer.

### `MarketDataSource` (`include/market_data_source.hpp`)

```cpp
virtual bool next(hft::TopOfBook& out) = 0;   // false: source ended or was stopped
void request_stop();                          // thread-safe; next() returns false promptly
const SourceActivity& activity() const;       // last_frame_ns, last_parse_err_ns
```

| Implementation | Input | Stops when |
|---|---|---|
| `LiveWebSocketSource` | Binance `/ws/<symbol>@bookTicker` frames; reconnects with backoff | `request_stop()` |
| `ReplaySource` | Stub bookTicker frames in the live format, parsed with the same `book_ticker::parse`; loops, starting a new session each pass | end of a non-looping pass, `max_runtime`, or `request_stop()` |

Both stamp `recv_ts_ns` right before parsing and update `SourceActivity`, which the watchdog reads to tell a dead connection from a live one sending bad data.

### `PipelineObserver` and its events

Observers receive events from the pipeline and decide what to do with them. Every call happens outside both latency spans, so an observer can't affect the measured latency.

| Event | Thread | Fired when | Arguments |
|---|---|---|---|
| `on_message` | process | an accepted tick has been decided and its signal built | signal, span timestamps |
| `on_drop` | ingest | the ring buffer is full and the tick is discarded | time |
| `on_validation_reject` | ingest | `TobValidator` rejects a tick | tick, reason, running total |
| `on_sequence_reject` | ingest | `SequenceTracker` rejects a tick (duplicate, out of order, …) | tick, reason, running total |
| `on_session_start` | ingest | first tick of a new session (connect, reconnect, replay loop) | tick |
| `on_feed_stop` | watchdog | health tripped; the source is stopped right after it returns | reason, health snapshot |

Only `on_message` is required; the others default to doing nothing.

Each event is always called from the same thread. An observer whose state is split per event, as both current observers' is, needs no locking.

| Observer | Used by | Does |
|---|---|---|
| `LogObserver` (`include/log_observer.hpp`) | live, replay | Logs `Signal:`/`Latency:` per message, plus rejects, new sessions and feed stops, each rate-limited to one line per second |
| Sampling observer (`src/benchmark.cpp`) | benchmark | Keeps span samples whose `t1` falls inside the measurement window, counts drops in the window |

### `TobRingBuffer` (`include/tob_ring_buffer.hpp`)

Fixed capacity (1024 slots), `std::atomic` head and tail. `push` checks `next == tail` before writing; `pop` checks `tail == head` before reading. Both return `false` on full/empty rather than blocking. Only the ingest thread writes `head_` and only the process thread writes `tail_`.

### Strategy (Rust, `rust_decide`)

Receives the serialized `TopOfBook` bytes, decodes them with `prost`, and returns one of:
- `QuoteBoth`: spread >= threshold
- `Hold`: spread < threshold
- `CancelAll`: an invalid book (non-positive prices, ask < bid, non-finite values) or a feed status other than OK or session start

The threshold is set once from C++ via `rust_set_threshold` before the pipeline starts. Data crosses the FFI boundary as Protobuf bytes, so neither side depends on the other's memory layout.

### `PipelineStats`

Returned by `run()`: total drops, serialize errors, rejects, the validator and sequence summaries printed at the end of a run, and the watchdog's stop reason (`None` unless it stopped the source). `main` exits with code `2` when the stop reason is set.

## Threads

| Thread | Started | Runs | Ends |
|---|---|---|---|
| **Ingest** | always | `next()` → checks → push | `next()` returns false; then sets `done` |
| **Process** | always | pop → serialize → decide → signal → `on_message` | queue empty and `done` set |
| **Watchdog** | `cfg.watchdog` | health snapshot every 100 ms | `done` set, or after stopping an unhealthy source |

The benchmark adds a fourth, outside the pipeline: a timer that calls `request_stop()` when the measurement window closes.

The ingest and process threads share only the ring buffer and the `done` flag. Each ring-buffer index is written by one thread only, so the data path needs no locks.

### Shutdown

```
normal end:     next() → false ─► ingest sets done ─► process drains queue, exits ─► watchdog exits
watchdog stop:  on_feed_stop ─► request_stop() ─► next() → false
                ─► ingest pushes a FEED_HALTED tick (retrying until it fits) ─► sets done
                ─► process delivers the halt tick (strategy answers CANCEL_ALL) ─► exits
benchmark:      timer ─► request_stop() ─► same as normal end
```

The halt tick lets the strategy invalidate its state before the run ends, so it doesn't keep acting on the last book.

> **Known issue:** the process thread exits when a `pop` fails *and* `done` is set, checked in that order. If ingest pushes its last tick and sets `done` between those two steps, that tick is never processed. On a watchdog stop this can lose the halt tick. This behaviour predates the shared pipeline: the old live loop had the same check.

## Extending

| To add | Implement | Pipeline changes |
|---|---|---|
| A new exchange or file replay | a `MarketDataSource` | none |
| New reporting or metrics | a `PipelineObserver` | none |
| A new engine check | the check, plus a `PipelineConfig` switch | yes: add it to the ingest path |
| A single-stage microbenchmark (e.g. `rust_decide` in a loop) | a separate harness | none; it shouldn't pretend to be the pipeline |
