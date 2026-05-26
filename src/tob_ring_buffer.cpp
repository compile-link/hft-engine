#include "tob_ring_buffer.hpp"

bool TobRingBuffer::push(const hft::TopOfBook& in) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next = (head + 1) % k_capacity;
    if (next == tail_.load(std::memory_order_acquire)) {
        return false;
    }
    buf_[head] = in;
    head_.store(next, std::memory_order_release);
    return true;
}

bool TobRingBuffer::pop(hft::TopOfBook& out) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
        return false;
    }
    out = buf_[tail];

    const size_t next = (tail + 1) % k_capacity;
    tail_.store(next, std::memory_order_release);
    return true;
}
