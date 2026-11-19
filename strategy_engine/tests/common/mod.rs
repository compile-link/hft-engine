// Shared helpers for the integration tests. Each file in tests/ is its own
// binary (and process), so every helper here must be used by every test file
// or it trips dead_code under clippy -D warnings.

use prost::Message;
use strategy_engine::pb;

/// Serializes a top-of-book the way the C++ engine does before calling rust_decide.
pub fn encode(bid: f64, ask: f64, status: pb::FeedStatus) -> Vec<u8> {
    pb::TopOfBook {
        symbol: "BTCUSDT".into(),
        bid_px: bid,
        ask_px: ask,
        bid_qty: 1.0,
        ask_qty: 1.0,
        sequence: 1,
        session: 1,
        feed_status: status as i32,
        ..Default::default()
    }
    .encode_to_vec()
}

/// Calls the C ABI entry point with a valid buffer, as the C++ engine does.
pub fn decide_bytes(bytes: &[u8]) -> i32 {
    // SAFETY: the pointer and length come from a live slice that is not
    // modified during the call.
    unsafe { strategy_engine::rust_decide(bytes.as_ptr(), bytes.len()) }
}
