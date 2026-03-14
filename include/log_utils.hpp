#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <ostream>
#include <string_view>

namespace log_utils {
    // One shared definition
    inline uint64_t now_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    inline void log_to_stream(std::ostream& os, std::string_view s) {
        static std::mutex mu;
        std::lock_guard<std::mutex> lock(mu);
        os << s
           << "\n\n";
    }
} // namespace log_utils
