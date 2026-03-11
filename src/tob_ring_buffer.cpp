#include "tob_ring_buffer.hpp"

bool TobRingBuffer::push(const hft::TopOfBook& in) {
    const size_t head = head_.load();
    const size_t next = (head + 1) % k_capacity;
    if (next == tail_.load()) {
        return false;
    }
    buf_[head] = in;
    head_.store(next);
    return true;
}

bool TobRingBuffer::pop(hft::TopOfBook& out) {
    const size_t tail = tail_.load();
    if (tail == head_.load()) {
        return false;
    }
    out = buf_[tail];

    const size_t next = (tail + 1) % k_capacity;
    tail_.store(next);
    return true;
}
