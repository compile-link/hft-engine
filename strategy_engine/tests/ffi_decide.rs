// rust_decide through the C ABI, as the C++ engine calls it.
//
// Nothing in this binary calls rust_set_threshold, so every test sees the
// default threshold (7e-7). Threshold tests live in ffi_threshold.rs, a
// separate process, because the threshold is a process-wide set-once value.

mod common;

use common::{decide_bytes, encode};
use proptest::prelude::*;
use proptest::test_runner::FileFailurePersistence;
use prost::encoding::{encode_key, encode_varint, WireType};
use prost::Message;
use std::ptr;
use strategy_engine::pb::{FeedStatus, QuoteAction, TopOfBook};
use strategy_engine::rust_decide;

const HOLD: i32 = QuoteAction::Hold as i32;
const QUOTE_BOTH: i32 = QuoteAction::QuoteBoth as i32;
const CANCEL_ALL: i32 = QuoteAction::CancelAll as i32;

// --- Pointer and length contract ---
// Documented: a null pointer or a zero length is accepted and yields CancelAll.
// The engine never sends either; these pin the fail-safe fallback.

#[test]
fn null_pointer_with_zero_length_cancels() {
    // SAFETY: a null pointer is part of the documented contract.
    assert_eq!(unsafe { rust_decide(ptr::null(), 0) }, CANCEL_ALL);
}

#[test]
fn null_pointer_with_nonzero_length_cancels_without_reading() {
    // SAFETY: the null check runs before the length is used, so nothing is read.
    assert_eq!(unsafe { rust_decide(ptr::null(), 64) }, CANCEL_ALL);
}

#[test]
fn valid_pointer_with_zero_length_cancels() {
    // Even when the buffer behind the pointer holds a quotable book
    let bytes = encode(100.0, 102.0, FeedStatus::FeedOk);
    // SAFETY: valid pointer; a zero length reads nothing.
    assert_eq!(unsafe { rust_decide(bytes.as_ptr(), 0) }, CANCEL_ALL);
}

#[test]
fn empty_slice_cancels() {
    // An empty slice has a non-null (dangling) pointer and length 0: the same
    // shape as an empty std::string's data() and size() on the C++ side.
    assert_eq!(decide_bytes(&[]), CANCEL_ALL);
}

#[test]
fn only_the_given_length_is_read() {
    // A prefix of a valid message is a truncated message, even though the
    // buffer behind the pointer is longer.
    let bytes = encode(100.0, 102.0, FeedStatus::FeedOk);
    // SAFETY: the length is within the buffer.
    assert_eq!(
        unsafe { rust_decide(bytes.as_ptr(), bytes.len() - 1) },
        CANCEL_ALL
    );
}

// --- Return values ---

#[test]
fn returns_proto_enum_values() {
    // C++ casts the i32 straight to hft::QuoteAction; these are the proto numbers.
    assert_eq!(HOLD, 1);
    assert_eq!(QUOTE_BOTH, 2);
    assert_eq!(CANCEL_ALL, 3);
}

// --- Decisions with the default threshold (7e-7) ---

#[test]
fn quotes_on_wide_spread() {
    assert_eq!(
        decide_bytes(&encode(100.0, 102.0, FeedStatus::FeedOk)),
        QUOTE_BOTH
    );
}

#[test]
fn default_threshold_separates_one_micro_from_half_micro() {
    assert_eq!(
        decide_bytes(&encode(1.0, 1.0 + 1e-6, FeedStatus::FeedOk)),
        QUOTE_BOTH
    );
    assert_eq!(
        decide_bytes(&encode(1.0, 1.0 + 5e-7, FeedStatus::FeedOk)),
        HOLD
    );
}

#[test]
fn session_start_uses_normal_decision() {
    assert_eq!(
        decide_bytes(&encode(100.0, 102.0, FeedStatus::FeedSessionStart)),
        QUOTE_BOTH
    );
}

#[test]
fn cancels_on_halt_tick() {
    // The C++ halt tick carries a symbol and status only, no prices
    let halt = strategy_engine::pb::TopOfBook {
        symbol: "BTCUSDT".into(),
        feed_status: FeedStatus::FeedHalted as i32,
        ..Default::default()
    };
    assert_eq!(
        decide_bytes(&prost::Message::encode_to_vec(&halt)),
        CANCEL_ALL
    );
}

#[test]
fn cancels_on_unspecified_status() {
    assert_eq!(
        decide_bytes(&encode(100.0, 102.0, FeedStatus::Unspecified)),
        CANCEL_ALL
    );
}

#[test]
fn cancels_on_negative_prices() {
    assert_eq!(
        decide_bytes(&encode(-1.0, 102.0, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
    assert_eq!(
        decide_bytes(&encode(-102.0, -100.0, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
}

#[test]
fn cancels_on_zero_price() {
    assert_eq!(
        decide_bytes(&encode(0.0, 102.0, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
    assert_eq!(
        decide_bytes(&encode(100.0, 0.0, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
}

#[test]
fn cancels_on_non_finite_prices() {
    assert_eq!(
        decide_bytes(&encode(f64::NAN, 102.0, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
    assert_eq!(
        decide_bytes(&encode(100.0, f64::INFINITY, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
}

#[test]
fn cancels_on_crossed_or_locked_book() {
    assert_eq!(
        decide_bytes(&encode(101.0, 100.0, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
    assert_eq!(
        decide_bytes(&encode(100.0, 100.0, FeedStatus::FeedOk)),
        CANCEL_ALL
    );
}

#[test]
fn cancels_on_garbage_bytes() {
    assert_eq!(decide_bytes(&[0xff, 0xff, 0xff]), CANCEL_ALL);
}

// --- Schema evolution ---

// Protobuf field keys: (field_number << 3) | wire_type, as a varint
const UNKNOWN_VARINT_FIELD_99: [u8; 3] = [0x98, 0x06, 0x01]; // field 99, varint, value 1
const UNKNOWN_BYTES_FIELD_100: [u8; 6] = [0xa2, 0x06, 0x03, b'n', b'e', b'w']; // field 100, len 3

#[test]
fn unknown_fields_are_ignored() {
    // A newer engine may add fields before the strategy is rebuilt: the
    // decision must not change.
    for (status, expected) in [
        (FeedStatus::FeedOk, QUOTE_BOTH),
        (FeedStatus::FeedHalted, CANCEL_ALL),
    ] {
        let mut bytes = encode(100.0, 102.0, status);
        bytes.extend_from_slice(&UNKNOWN_VARINT_FIELD_99);
        bytes.extend_from_slice(&UNKNOWN_BYTES_FIELD_100);
        assert_eq!(decide_bytes(&bytes), expected, "status={status:?}");
    }
}

// Failing seeds go to tests/ffi_decide.proptest-regressions, next to this file.
// Commit that file if it appears: the seeds re-run first on every later run.
// (The default location looks for a lib.rs above the test file, which an
// integration test does not have, and warns before falling back to this.)
fn config() -> ProptestConfig {
    ProptestConfig {
        failure_persistence: Some(Box::new(FileFailurePersistence::WithSource(
            "proptest-regressions",
        ))),
        ..ProptestConfig::default()
    }
}

// A message with random prices and status: decodes cleanly, any decision
fn message() -> impl Strategy<Value = Vec<u8>> {
    (
        prop_oneof![prop::num::f64::ANY, 0.0..200.0f64],
        prop_oneof![prop::num::f64::ANY, 0.0..200.0f64],
        prop_oneof![0..=4i32, any::<i32>()],
    )
        .prop_map(|(bid, ask, status)| {
            strategy_engine::pb::TopOfBook {
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
        })
}

// One field this schema does not define (TopOfBook uses 1-10), of any wire
// type the proto3 encoding produces, with a payload of matching shape
fn unknown_field() -> impl Strategy<Value = Vec<u8>> {
    let number = 11u32..=prost::encoding::MAX_TAG;
    let payload = prop_oneof![
        any::<u64>().prop_map(|v| (WireType::Varint, v.to_le_bytes().to_vec())),
        any::<[u8; 8]>().prop_map(|b| (WireType::SixtyFourBit, b.to_vec())),
        any::<[u8; 4]>().prop_map(|b| (WireType::ThirtyTwoBit, b.to_vec())),
        prop::collection::vec(any::<u8>(), 0..64).prop_map(|b| (WireType::LengthDelimited, b)),
    ];
    (number, payload).prop_map(|(number, (wire_type, data))| {
        let mut out = Vec::new();
        encode_key(number, wire_type, &mut out);
        match wire_type {
            WireType::Varint => {
                encode_varint(u64::from_le_bytes(data.try_into().unwrap()), &mut out)
            }
            WireType::LengthDelimited => {
                encode_varint(data.len() as u64, &mut out);
                out.extend_from_slice(&data);
            }
            _ => out.extend_from_slice(&data),
        }
        out
    })
}

proptest! {
    #![proptest_config(config())]

    // Fields can appear in any order on the wire, so unknown ones are put
    // both before and after the known fields
    #[test]
    fn random_unknown_fields_never_change_the_decision(
        msg in message(),
        before in prop::collection::vec(unknown_field(), 0..4),
        after in prop::collection::vec(unknown_field(), 0..4),
    ) {
        let mut extended: Vec<u8> = before.concat();
        extended.extend_from_slice(&msg);
        extended.extend_from_slice(&after.concat());
        prop_assert_eq!(decide_bytes(&extended), decide_bytes(&msg));
    }
}

// --- Duplicated fields ---
// Protobuf lets a field appear more than once in one message; the decoder keeps
// the last value. Appending an encoded field to a message therefore overrides
// it, and concatenating two messages merges them: the second one's fields that
// are on the wire win. The engine never sends either, but the strategy must
// decide on the value that wins, not an earlier one. Each test checks the
// standard message first, then the same message with a duplicated field.

// TopOfBook field numbers (proto/market_data.proto)
const BID_PX: u32 = 2;
const ASK_PX: u32 = 4;
const FEED_STATUS: u32 = 10;

// One encoded double field, to append to a standard message
fn price_field(number: u32, value: f64) -> Vec<u8> {
    let mut out = Vec::new();
    prost::encoding::double::encode(number, &value, &mut out);
    out
}

// One encoded feed_status field (enums are encoded as int32 varints)
fn status_field(status: FeedStatus) -> Vec<u8> {
    let mut out = Vec::new();
    prost::encoding::int32::encode(FEED_STATUS, &(status as i32), &mut out);
    out
}

fn with_field(standard: &[u8], field: Vec<u8>) -> Vec<u8> {
    [standard, &field[..]].concat()
}

#[test]
fn duplicated_fields_have_the_expected_wire_form() {
    // A field key is (number << 3) | wire type: 0x50 = field 10 varint,
    // 0x11 = field 2 64-bit, 0x21 = field 4 64-bit
    assert_eq!(status_field(FeedStatus::FeedHalted), [0x50, 0x03]);
    assert_eq!(price_field(BID_PX, 103.0)[0], 0x11);
    assert_eq!(price_field(ASK_PX, 103.0)[0], 0x21);
    // The standard message already ends with its one feed_status = FEED_OK
    assert!(encode(100.0, 102.0, FeedStatus::FeedOk).ends_with(&[0x50, 0x01]));
}

#[test]
fn later_halted_status_overrides_ok() {
    let standard = encode(100.0, 102.0, FeedStatus::FeedOk);
    assert_eq!(decide_bytes(&standard), QUOTE_BOTH);

    let duplicated = with_field(&standard, status_field(FeedStatus::FeedHalted));
    assert_eq!(decide_bytes(&duplicated), CANCEL_ALL);
}

#[test]
fn later_ok_status_overrides_halted() {
    // Last one wins in both directions: an earlier HALTED is not sticky.
    // Pinned so that changing it (e.g. "any HALTED cancels") is deliberate.
    let standard = encode(100.0, 102.0, FeedStatus::FeedHalted);
    assert_eq!(decide_bytes(&standard), CANCEL_ALL);

    let duplicated = with_field(&standard, status_field(FeedStatus::FeedOk));
    assert_eq!(decide_bytes(&duplicated), QUOTE_BOTH);
}

#[test]
fn later_bid_that_crosses_the_book_cancels() {
    let standard = encode(100.0, 102.0, FeedStatus::FeedOk);
    assert_eq!(decide_bytes(&standard), QUOTE_BOTH);

    let duplicated = with_field(&standard, price_field(BID_PX, 103.0));
    assert_eq!(decide_bytes(&duplicated), CANCEL_ALL);
}

#[test]
fn later_ask_that_narrows_the_spread_holds() {
    // Default threshold 7e-7: a spread of 1e-6 quotes, 5e-7 holds
    let standard = encode(1.0, 1.0 + 1e-6, FeedStatus::FeedOk);
    assert_eq!(decide_bytes(&standard), QUOTE_BOTH);

    let duplicated = with_field(&standard, price_field(ASK_PX, 1.0 + 5e-7));
    assert_eq!(decide_bytes(&duplicated), HOLD);
}

#[test]
fn later_non_finite_price_cancels() {
    let standard = encode(100.0, 102.0, FeedStatus::FeedOk);
    assert_eq!(decide_bytes(&standard), QUOTE_BOTH);

    for bad in [f64::NAN, f64::INFINITY, -1.0] {
        let duplicated = with_field(&standard, price_field(ASK_PX, bad));
        assert_eq!(decide_bytes(&duplicated), CANCEL_ALL, "ask={bad}");
    }
}

#[test]
fn concatenated_messages_merge() {
    // Two whole messages in one buffer. proto3 leaves default values off the
    // wire, so the halt message (status only, no prices) overrides just the
    // status and keeps the first message's prices.
    let book = encode(100.0, 102.0, FeedStatus::FeedOk);
    let halt = TopOfBook {
        symbol: "BTCUSDT".into(),
        feed_status: FeedStatus::FeedHalted as i32,
        ..Default::default()
    }
    .encode_to_vec();
    assert_eq!(decide_bytes(&book), QUOTE_BOTH);
    assert_eq!(decide_bytes(&halt), CANCEL_ALL);

    assert_eq!(decide_bytes(&[&book[..], &halt[..]].concat()), CANCEL_ALL);
    assert_eq!(decide_bytes(&[&halt[..], &book[..]].concat()), QUOTE_BOTH);
}

// What a concatenation should decode to: each decision field takes the second
// message's value if that value is on the wire. prost writes a proto3 double
// only when it is != 0.0 (so -0.0 is left off too) and an enum only when != 0.
// The other fields do not affect the decision (tested in lib.rs), so they are
// taken from the first message.
fn expected_merge(first: &TopOfBook, second: &TopOfBook) -> TopOfBook {
    let price = |a: f64, b: f64| if b != 0.0 { b } else { a };
    TopOfBook {
        bid_px: price(first.bid_px, second.bid_px),
        ask_px: price(first.ask_px, second.ask_px),
        feed_status: if second.feed_status != 0 {
            second.feed_status
        } else {
            first.feed_status
        },
        ..first.clone()
    }
}

proptest! {
    #![proptest_config(config())]

    #[test]
    fn concatenated_random_messages_decide_like_their_merge(a in message(), b in message()) {
        let first = TopOfBook::decode(&a[..]).unwrap();
        let second = TopOfBook::decode(&b[..]).unwrap();
        prop_assert_eq!(
            decide_bytes(&[&a[..], &b[..]].concat()),
            decide_bytes(&expected_merge(&first, &second).encode_to_vec())
        );
    }
}

// --- Arbitrary input ---
// A panic cannot unwind out of an extern "C" function: it aborts the process,
// which would take the engine down. So for any input rust_decide must return
// one of the three actions. A panic here shows up as the test binary aborting.
// Exhaustive where the input space is small enough to enumerate (short
// strings, single-byte damage); random beyond it.

fn assert_valid_action(bytes: &[u8]) {
    let r = decide_bytes(bytes);
    assert!(
        r == HOLD || r == QUOTE_BOTH || r == CANCEL_ALL,
        "returned {r} for {bytes:02x?}"
    );
}

#[test]
fn every_byte_string_up_to_two_bytes_returns_a_valid_action() {
    assert_valid_action(&[]);
    for a in 0..=u8::MAX {
        assert_valid_action(&[a]);
        for b in 0..=u8::MAX {
            assert_valid_action(&[a, b]);
        }
    }
}

#[test]
fn every_truncation_of_a_valid_message_returns_a_valid_action() {
    let bytes = encode(100.0, 102.0, FeedStatus::FeedOk);
    for len in 0..bytes.len() {
        assert_valid_action(&bytes[..len]);
    }
}

#[test]
fn every_single_byte_corruption_returns_a_valid_action() {
    // Hits every field key, length prefix and value byte with all 256 values
    let valid = encode(100.0, 102.0, FeedStatus::FeedOk);
    for pos in 0..valid.len() {
        for v in 0..=u8::MAX {
            let mut bytes = valid.clone();
            bytes[pos] = v;
            assert_valid_action(&bytes);
        }
    }
}

proptest! {
    #![proptest_config(config())]

    #[test]
    fn random_bytes_return_a_valid_action(bytes in prop::collection::vec(any::<u8>(), 0..1024)) {
        assert_valid_action(&bytes);
    }

    // Several overwrites, then optionally a cut or random bytes appended:
    // damage that keeps most of a real message's structure, which reaches
    // deeper into the decoder than random bytes do
    #[test]
    fn random_damage_to_a_valid_message_returns_a_valid_action(
        msg in message(),
        overwrites in prop::collection::vec((any::<prop::sample::Index>(), any::<u8>()), 1..8),
        cut in prop::option::of(any::<prop::sample::Index>()),
        tail in prop::collection::vec(any::<u8>(), 0..16),
    ) {
        let mut bytes = msg;
        for (i, v) in overwrites {
            let pos = i.index(bytes.len());
            bytes[pos] = v;
        }
        if let Some(c) = cut {
            bytes.truncate(c.index(bytes.len()));
        }
        bytes.extend_from_slice(&tail);
        assert_valid_action(&bytes);
    }
}
