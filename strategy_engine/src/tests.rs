use super::*;
use proptest::prelude::*;

fn encode(bid: f64, ask: f64, status: pb::FeedStatus) -> Vec<u8> {
    encode_raw_status(bid, ask, status as i32)
}

// Any i32 status, including values outside the enum (proto3 enums are open)
fn encode_raw_status(bid: f64, ask: f64, status: i32) -> Vec<u8> {
    pb::TopOfBook {
        symbol: "BTCUSDT".into(),
        bid_px: bid,
        ask_px: ask,
        bid_qty: 1.0,
        ask_qty: 1.0,
        sequence: 1,
        session: 1,
        feed_status: status,
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
    let mut m = pb::TopOfBook::decode(&encode(100.0, 102.0, pb::FeedStatus::FeedOk)[..]).unwrap();
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

// --- Threshold boundary ---

#[test]
fn quotes_when_spread_equals_threshold() {
    // 101.0 - 100.0 is exactly 1.0 in binary floating point
    assert_eq!(
        decide(&encode(100.0, 101.0, pb::FeedStatus::FeedOk), 1.0),
        pb::QuoteAction::QuoteBoth
    );
}

#[test]
fn holds_when_spread_just_below_threshold() {
    // The smallest double above 1.0: the spread misses it by one ulp
    assert_eq!(
        decide(
            &encode(100.0, 101.0, pb::FeedStatus::FeedOk),
            1.0 + f64::EPSILON
        ),
        pb::QuoteAction::Hold
    );
}

// --- Non-positive prices with a tradable status ---
// cancels_when_halted returns at the status check, so these are the only
// tests that reach the price sign check.

#[test]
fn cancels_on_negative_bid() {
    assert_eq!(
        decide(&encode(-1.0, 102.0, pb::FeedStatus::FeedOk), 1.0),
        pb::QuoteAction::CancelAll
    );
}

#[test]
fn cancels_on_negative_bid_and_ask() {
    // Not crossed (ask > bid) and spread >= threshold: only the sign check catches it
    assert_eq!(
        decide(&encode(-102.0, -100.0, pb::FeedStatus::FeedOk), 1.0),
        pb::QuoteAction::CancelAll
    );
}

#[test]
fn cancels_on_zero_bid() {
    assert_eq!(
        decide(&encode(0.0, 102.0, pb::FeedStatus::FeedOk), 1.0),
        pb::QuoteAction::CancelAll
    );
}

#[test]
fn cancels_on_negative_zero_bid() {
    assert_eq!(
        decide(&encode(-0.0, 102.0, pb::FeedStatus::FeedOk), 1.0),
        pb::QuoteAction::CancelAll
    );
}

#[test]
fn cancels_on_non_positive_prices_at_session_start() {
    assert_eq!(
        decide(&encode(-1.0, 102.0, pb::FeedStatus::FeedSessionStart), 1.0),
        pb::QuoteAction::CancelAll
    );
}

// --- Remaining non-finite and book-shape cases ---

#[test]
fn cancels_on_nan_ask_or_negative_infinity_bid() {
    assert_eq!(
        decide(&encode(100.0, f64::NAN, pb::FeedStatus::FeedOk), 1.0),
        pb::QuoteAction::CancelAll
    );
    assert_eq!(
        decide(
            &encode(f64::NEG_INFINITY, 102.0, pb::FeedStatus::FeedOk),
            1.0
        ),
        pb::QuoteAction::CancelAll
    );
}

#[test]
fn cancels_on_crossed_book_at_session_start() {
    // A session start lets the status check pass; the book checks still apply
    assert_eq!(
        decide(&encode(101.0, 100.0, pb::FeedStatus::FeedSessionStart), 1.0),
        pb::QuoteAction::CancelAll
    );
}

// --- Malformed input ---

#[test]
fn cancels_on_empty_bytes() {
    // Zero bytes decode to an all-default message: status UNSPECIFIED.
    // rust_decide returns CancelAll for len 0 before reaching decide(), so the
    // two paths agree.
    assert_eq!(decide(&[], 1.0), pb::QuoteAction::CancelAll);
}

#[test]
fn cancels_on_truncated_message() {
    let mut bytes = encode(100.0, 102.0, pb::FeedStatus::FeedOk);
    bytes.pop(); // feed_status is last: its tag remains without a value
    assert_eq!(decide(&bytes, 1.0), pb::QuoteAction::CancelAll);
}

// --- Decision invariants ---
// Rules that must hold for any input, checked by check_invariants against
// two sources: every combination of the edge values below (exhaustive, so
// no edge pair depends on luck), and random books from proptest.

const PRICES: [f64; 15] = [
    f64::NAN,
    f64::NEG_INFINITY,
    f64::INFINITY,
    f64::MIN,
    -1.0,
    -0.0,
    0.0,
    5e-324, // smallest subnormal
    f64::MIN_POSITIVE,
    1.0,
    100.0,
    100.5,
    101.0,
    102.0,
    f64::MAX,
];

// Every non-OK enum value, plus values outside the enum
const STATUSES: [i32; 7] = [0, 1, 2, 3, 4, 99, -1];

// Ascending; all are values rust_set_threshold accepts (finite, > 0)
const THRESHOLDS: [f64; 7] = [5e-324, 7e-7, 0.5, 1.0, 1.5, 2.0, f64::MAX];

fn tradable(status: i32) -> bool {
    status == pb::FeedStatus::FeedOk as i32 || status == pb::FeedStatus::FeedSessionStart as i32
}

// `thresholds` must be ascending, finite and positive
fn check_invariants(
    bid: f64,
    ask: f64,
    status: i32,
    thresholds: &[f64],
) -> Result<(), TestCaseError> {
    use pb::QuoteAction::{CancelAll, Hold, QuoteBoth};

    let bytes = encode_raw_status(bid, ask, status);
    let results: Vec<_> = thresholds.iter().map(|&t| decide(&bytes, t)).collect();
    let ctx = format!("bid={bid:e} ask={ask:e} status={status} t={thresholds:?} -> {results:?}");

    // Only three possible answers
    prop_assert!(
        results
            .iter()
            .all(|r| matches!(r, QuoteBoth | Hold | CancelAll)),
        "{}",
        ctx
    );

    // CancelAll exactly when the status is not tradable or the book is not
    // sane, whatever the threshold
    let sane = bid.is_finite() && ask.is_finite() && bid > 0.0 && ask > bid;
    if !tradable(status) || !sane {
        prop_assert!(results.iter().all(|r| *r == CancelAll), "{}", ctx);
        return Ok(());
    }
    prop_assert!(!results.contains(&CancelAll), "{}", ctx);

    // Monotonic in the threshold: once it holds, a higher threshold never
    // quotes again
    if let Some(i) = results.iter().position(|r| *r == Hold) {
        prop_assert!(results[i..].iter().all(|r| *r == Hold), "{}", ctx);
    }
    Ok(())
}

#[test]
fn decision_invariants_hold_for_every_edge_value_combination() {
    for bid in PRICES {
        for ask in PRICES {
            for status in STATUSES {
                check_invariants(bid, ask, status, &THRESHOLDS).unwrap();
            }
        }
    }
}

// Random prices: edge values, any double (NaN, infinities, subnormals,
// both signs), and ordinary prices, so all three outcomes are common
fn price() -> impl Strategy<Value = f64> {
    prop_oneof![
        3 => prop::sample::select(PRICES.to_vec()),
        2 => prop::num::f64::ANY,
        5 => 0.0..200.0f64,
    ]
}

// Independent bid and ask, or a sane book whose spread lands near typical
// thresholds, so the Hold/QuoteBoth boundary is exercised
fn book() -> impl Strategy<Value = (f64, f64)> {
    prop_oneof![
        (price(), price()),
        (1e-3..1e5f64, 0.0..4.0f64).prop_map(|(bid, spread)| (bid, bid + spread)),
    ]
}

fn status() -> impl Strategy<Value = i32> {
    prop_oneof![prop::sample::select(STATUSES.to_vec()), any::<i32>()]
}

// Values rust_set_threshold accepts: finite and positive
fn threshold() -> impl Strategy<Value = f64> {
    prop_oneof![
        prop::sample::select(THRESHOLDS.to_vec()),
        prop::num::f64::POSITIVE | prop::num::f64::NORMAL | prop::num::f64::SUBNORMAL,
        1e-9..4.0f64,
    ]
}

fn ascending_thresholds() -> impl Strategy<Value = Vec<f64>> {
    prop::collection::vec(threshold(), 1..8).prop_map(|mut v| {
        v.sort_by(f64::total_cmp);
        v
    })
}

proptest! {
    // 4x the default so the cheap decide() loop digs deeper; scales with
    // PROPTEST_CASES (the nightly job raises it), which with_cases would not
    #![proptest_config(ProptestConfig {
        cases: 4 * ProptestConfig::default().cases,
        ..ProptestConfig::default()
    })]

    #[test]
    fn decision_invariants_hold_for_random_books(
        (bid, ask) in book(),
        status in status(),
        thresholds in ascending_thresholds(),
    ) {
        check_invariants(bid, ask, status, &thresholds)?;
    }

    // Only prices and status drive the decision: randomising every other
    // field (symbol, quantities, timestamps, sequence, session) changes nothing
    #[test]
    fn decision_ignores_fields_other_than_prices_and_status(
        symbol in ".{0,16}",
        (bid, ask) in book(),
        (bid_qty, ask_qty) in (prop::num::f64::ANY, prop::num::f64::ANY),
        (exchange_ts_ns, recv_ts_ns, sequence, session) in any::<(u64, u64, u64, u64)>(),
        feed_status in status(),
        threshold in threshold(),
    ) {
        let bytes = pb::TopOfBook {
            symbol, bid_px: bid, bid_qty, ask_px: ask, ask_qty,
            exchange_ts_ns, recv_ts_ns, sequence, session, feed_status,
        }
        .encode_to_vec();
        prop_assert_eq!(
            decide(&bytes, threshold),
            decide(&encode_raw_status(bid, ask, feed_status), threshold)
        );
    }
}
