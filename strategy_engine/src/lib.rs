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

/// Decodes a serialized `TopOfBook` and returns a `QuoteAction` as `i32`.
///
/// # Safety
///
/// If `top_ptr` is non-null, it must point to `top_len` initialized bytes that
/// stay valid and unmodified for the duration of the call. A null pointer or a
/// zero length is accepted and yields `Hold`.
#[no_mangle]
pub unsafe extern "C" fn rust_decide(top_ptr: *const u8, top_len: usize) -> i32 {
    if top_ptr.is_null() || top_len == 0 {
        return pb::QuoteAction::Hold as i32;
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

#[cfg(test)]
mod tests {
    use super::*;

    fn encode(bid: f64, ask: f64, status: pb::FeedStatus) -> Vec<u8> {
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

    #[test]
    fn quotes_on_wide_spread_when_ok() {
        assert_eq!(
            decide(&encode(100.0, 102.0, pb::FeedStatus::FeedOk), 1.0),
            pb::QuoteAction::QuoteBoth
        );
    }

    #[test]
    fn holds_on_narrow_spread() {
        assert_eq!(
            decide(&encode(100.0, 100.5, pb::FeedStatus::FeedOk), 1.0),
            pb::QuoteAction::Hold
        );
    }

    #[test]
    fn session_start_uses_normal_decision() {
        assert_eq!(
            decide(&encode(100.0, 102.0, pb::FeedStatus::FeedSessionStart), 1.0),
            pb::QuoteAction::QuoteBoth
        );
    }

    #[test]
    fn cancels_when_halted() {
        assert_eq!(
            decide(&encode(0.0, 0.0, pb::FeedStatus::FeedHalted), 1.0),
            pb::QuoteAction::CancelAll
        );
    }

    #[test]
    fn cancels_when_status_unspecified() {
        assert_eq!(
            decide(&encode(100.0, 102.0, pb::FeedStatus::Unspecified), 1.0),
            pb::QuoteAction::CancelAll
        );
    }

    #[test]
    fn cancels_on_unknown_status_value() {
        let mut m =
            pb::TopOfBook::decode(&encode(100.0, 102.0, pb::FeedStatus::FeedOk)[..]).unwrap();
        m.feed_status = 99;
        assert_eq!(decide(&m.encode_to_vec(), 1.0), pb::QuoteAction::CancelAll);
    }

    #[test]
    fn cancels_on_nan_or_inf() {
        assert_eq!(
            decide(&encode(f64::NAN, 102.0, pb::FeedStatus::FeedOk), 1.0),
            pb::QuoteAction::CancelAll
        );
        assert_eq!(
            decide(&encode(100.0, f64::INFINITY, pb::FeedStatus::FeedOk), 1.0),
            pb::QuoteAction::CancelAll
        );
    }

    #[test]
    fn cancels_on_crossed_or_locked() {
        assert_eq!(
            decide(&encode(101.0, 100.0, pb::FeedStatus::FeedOk), 1.0),
            pb::QuoteAction::CancelAll
        );
        assert_eq!(
            decide(&encode(100.0, 100.0, pb::FeedStatus::FeedOk), 1.0),
            pb::QuoteAction::CancelAll
        );
    }

    #[test]
    fn cancels_on_undecodable_bytes() {
        assert_eq!(decide(&[0xff, 0xff, 0xff], 1.0), pb::QuoteAction::CancelAll);
    }
}
