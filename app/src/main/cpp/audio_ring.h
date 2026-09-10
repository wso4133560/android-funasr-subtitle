#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

// Single producer (AudioRecord/JNI), single consumer (native VAD worker).
template <size_t Capacity>
class AudioRing {
    std::vector<float> data_ = std::vector<float>(Capacity, 0.0f);
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};

public:
    bool push(const float* data, size_t count) {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        if (count > Capacity - (write - read)) return false;
        for (size_t i = 0; i < count; ++i) data_[(write + i) % Capacity] = data[i];
        write_.store(write + count, std::memory_order_release);
        return true;
    }

    size_t pop(float* data, size_t count) {
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        count = std::min(count, write - read);
        for (size_t i = 0; i < count; ++i) data[i] = data_[(read + i) % Capacity];
        read_.store(read + count, std::memory_order_release);
        return count;
    }

    bool empty() const {
        return write_.load(std::memory_order_acquire) == read_.load(std::memory_order_relaxed);
    }
};
