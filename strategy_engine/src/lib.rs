use prost::Message;
use std::sync::OnceLock;

pub mod pb {
    include!(concat!(env!("OUT_DIR"), "/hft.rs"));
}

static THRESHOLD: OnceLock<f64> = OnceLock::new();

/// Sets the spread threshold for the life of the process.
///
/// Returns `true` if `v` is now the threshold. Returns `false`, leaving the
/// threshold unchanged, if `v` is not finite and positive or if a threshold
/// was already set. `bool` has the same ABI as C/C++ `bool`.
#[no_mangle]
pub extern "C" fn rust_set_threshold(v: f64) -> bool {
    v.is_finite() && v > 0.0 && THRESHOLD.set(v).is_ok()
}

fn get_threshold() -> f64 {
    *THRESHOLD.get().unwrap_or(&0.0000007)
}

/// Decodes a serialized `TopOfBook` and returns a `QuoteAction` as `i32`.
///
/// # Safety
///
/// If `top_ptr` is non-null, it must point to `top_len` initialized bytes that
/// stay valid and unmodified for the duration of the call. A null pointer or a
/// zero length is accepted and yields `CancelAll`, like any other message that
/// carries no usable market state.
#[no_mangle]
pub unsafe extern "C" fn rust_decide(top_ptr: *const u8, top_len: usize) -> i32 {
    // Fallback only: the engine always passes a populated buffer (std::string data
    // is never null, and every message it sends sets feed_status, so it is never
    // empty). Reaching this means a broken caller, so fail safe.
    // The null check must stay ahead of from_raw_parts, which is UB for null even at len 0.
    if top_ptr.is_null() || top_len == 0 {
        return pb::QuoteAction::CancelAll as i32;
    }

    // SAFETY: non-null and non-zero length checked above; validity of the
    // buffer is the caller's obligation per the contract documented above.
    let bytes = unsafe { std::slice::from_raw_parts(top_ptr, top_len) };
    decide(bytes, get_threshold()) as i32
}

// Pure decision logic, separated from the FFI entry point for testing.
// Any doubt about market state results in CancelAll: an undecodable message, a non-OK feed
// status, or a book that fails the sanity checks (NaN fails every comparison, so check finiteness).
fn decide(bytes: &[u8], threshold: f64) -> pb::QuoteAction {
    let msg = match pb::TopOfBook::decode(bytes) {
        Ok(m) => m,
        Err(_) => return pb::QuoteAction::CancelAll,
    };

    match pb::FeedStatus::try_from(msg.feed_status) {
        Ok(pb::FeedStatus::FeedOk) | Ok(pb::FeedStatus::FeedSessionStart) => {}
        _ => return pb::QuoteAction::CancelAll,
    }

    if !msg.bid_px.is_finite()
        || !msg.ask_px.is_finite()
        || msg.bid_px <= 0.0
        || msg.ask_px <= 0.0
        || msg.ask_px <= msg.bid_px
    {
        return pb::QuoteAction::CancelAll;
    }

    let spread = msg.ask_px - msg.bid_px;

    if spread >= threshold {
        pb::QuoteAction::QuoteBoth
    } else {
        pb::QuoteAction::Hold
    }
}

// Unit tests of the private decide(); FFI tests are in tests/
#[cfg(test)]
mod tests;
