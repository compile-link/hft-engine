#pragma once

#include <cstdint>

// Shared C ABI struct
struct TobRaw {
    double bid_px;
    double bid_qty;
    double ask_px;
    double ask_qty;
    uint64_t recv_ts_ns;
};

extern "C" int32_t rust_decide_raw(const TobRaw* in);
extern "C" void rust_set_threshold(double thres);
