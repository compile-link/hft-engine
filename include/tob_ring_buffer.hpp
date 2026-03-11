#pragma once

#include "market_data.pb.h"
#include <atomic>
#include <cstddef>
#include <array>

class TobRingBuffer {
  public:
    bool push(const hft::TopOfBook& in);
    bool pop(hft::TopOfBook& out);

  private:
    static constexpr size_t k_capacity = 1024;
    std::array<hft::TopOfBook, k_capacity> buf_{};
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};
