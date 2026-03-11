use prost::Message;
use std::sync::OnceLock;

pub mod pb {
    include!(concat!(env!("OUT_DIR"), "/hft.rs"));
}

static THRESHOLD: OnceLock<f64> = OnceLock::new();

#[no_mangle]
pub extern "C" fn rust_set_threshold(v: f64) {
    if v.is_finite() && v > 0.0 {
        let _ = THRESHOLD.set(v);
    }
}

fn get_threshold() -> f64 {
    *THRESHOLD.get().unwrap_or(&0.0000007)
}

#[no_mangle]
pub extern "C" fn rust_decide(top_ptr: *const u8, top_len: usize) -> i32 {
    if top_ptr.is_null() || top_len == 0 {
        return pb::QuoteAction::Hold as i32;
    }

    let bytes = unsafe { std::slice::from_raw_parts(top_ptr, top_len) };
    let msg = match pb::TopOfBook::decode(bytes) {
        Ok(m) => m,
        Err(_) => return pb::QuoteAction::Hold as i32,
    };

    if msg.bid_px <= 0.0 || msg.ask_px <= 0.0 || msg.ask_px < msg.bid_px {
        return pb::QuoteAction::CancelAll as i32;
    }

    let spread = msg.ask_px - msg.bid_px;
    let threshold = get_threshold();

    if spread >= threshold {
        pb::QuoteAction::QuoteBoth as i32
    } else {
        pb::QuoteAction::Hold as i32
    }
}
