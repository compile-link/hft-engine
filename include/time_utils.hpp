#pragma once

#include <chrono>
#include <cstdint>

namespace time_utils {
    // One shared definition
    inline uint64_t now_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }
} // namespace time_utils
