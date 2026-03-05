#include <cstdint>
#include <iostream>
#include <string>
#include "market_data.pb.h"

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

static const char* action_to_str(int32_t a) {
  switch (a) {
    case hft::HOLD: return "HOLD";
    case hft::QUOTE_BID: return "QUOTE_BID";
    case hft::QUOTE_ASK: return "QUOTE_ASK";
    case hft::QUOTE_BOTH: return "QUOTE_BOTH";
    case hft::CANCEL_ALL: return "CANCEL_ALL";
    default: return "UNSPECIFIED";
  }
}

int main() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  hft::TopOfBook tob;
  tob.set_symbol("BTCUSDT");
  tob.set_bid_px(100.0);
  tob.set_bid_qty(1.2);
  tob.set_ask_px(101.2);
  tob.set_ask_qty(1.1);
  tob.set_exchange_ts_ns(1);
  tob.set_recv_ts_ns(2);
  tob.set_sequence(42);

  std::string payload;
  tob.SerializeToString(&payload);

  int32_t action = rust_decide(
      reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

  std::cout << "Strategy action: " << action_to_str(action) << " (" << action << ")\n";

  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}