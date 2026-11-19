// rust_set_threshold through the C ABI.
//
// The threshold is a process-wide OnceLock: the first valid value wins for
// the life of the process. This file is its own test binary (process), so it
// cannot affect ffi_decide.rs. Within it, the steps below depend on each
// other's order, so they are one test: separate #[test] functions run in
// parallel in an unspecified order. The randomised steps therefore use an
// explicit TestRunner inside that one test instead of the proptest! macro.

mod common;

use common::{decide_bytes, encode};
use proptest::num::f64 as f64s;
use proptest::prelude::*;
use proptest::test_runner::{Config, TestRunner};
use strategy_engine::pb::{FeedStatus, QuoteAction};
use strategy_engine::rust_set_threshold;

const HOLD: i32 = QuoteAction::Hold as i32;
const QUOTE_BOTH: i32 = QuoteAction::QuoteBoth as i32;

fn decide_spread(bid: f64, ask: f64) -> i32 {
    decide_bytes(&encode(bid, ask, FeedStatus::FeedOk))
}

// The default (7e-7) is in effect: a spread of 1e-6 quotes, 5e-7 holds
fn threshold_is_default() -> bool {
    decide_spread(1.0, 1.0 + 1e-6) == QUOTE_BOTH && decide_spread(1.0, 1.0 + 5e-7) == HOLD
}

// The threshold is 1.0, to within one double: both spreads below are exact
// (2.0 - 1.0 == 1.0, and (2.0 - EPSILON) - 1.0 == 1.0 - EPSILON)
fn threshold_is_one() -> bool {
    decide_spread(1.0, 2.0) == QUOTE_BOTH && decide_spread(1.0, 2.0 - f64::EPSILON) == HOLD
}

// Everything rust_set_threshold must reject: every negative value (including
// -0.0 and -inf), +0.0, +inf, and NaN of either sign
fn invalid_threshold() -> impl Strategy<Value = f64> {
    prop_oneof![
        f64s::NEGATIVE | f64s::NORMAL | f64s::SUBNORMAL | f64s::ZERO | f64s::INFINITE,
        f64s::POSITIVE | f64s::ZERO | f64s::INFINITE,
        f64s::POSITIVE | f64s::NEGATIVE | f64s::QUIET_NAN,
    ]
}

// No regression file: there is no proptest! source location to key it on
fn runner() -> TestRunner {
    TestRunner::new(Config {
        failure_persistence: None,
        ..Config::default()
    })
}

#[test]
fn threshold_rejects_invalid_values_then_is_set_once() {
    // Before any call: the default applies
    assert!(threshold_is_default());

    // Invalid values are rejected and do not use up the one-time set
    runner()
        .run(&invalid_threshold(), |v| {
            prop_assert!(!(v.is_finite() && v > 0.0), "strategy produced {v:e}");
            prop_assert!(!rust_set_threshold(v), "accepted {v:e}");
            prop_assert!(threshold_is_default(), "{v:e} changed the threshold");
            Ok(())
        })
        .unwrap();

    // The first valid value is accepted and takes effect
    assert!(rust_set_threshold(1.0));
    assert!(threshold_is_one());

    // Any later value is rejected, valid or not, including the same value again
    assert!(!rust_set_threshold(1.0));
    runner()
        .run(&f64s::ANY, |v| {
            prop_assert!(
                !rust_set_threshold(v),
                "accepted {v:e} after the threshold was set"
            );
            prop_assert!(threshold_is_one(), "{v:e} changed the threshold");
            Ok(())
        })
        .unwrap();
}
