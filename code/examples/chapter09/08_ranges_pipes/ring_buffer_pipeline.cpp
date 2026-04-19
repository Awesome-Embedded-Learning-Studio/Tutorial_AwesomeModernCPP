// Ring Buffer Pipeline
// Demonstrates processing ring buffer data with pipeline operators

#include <iostream>
#include <ranges>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <numeric>

// Ring buffer for embedded data collection
template<typename T, size_t N>
class RingBuffer {
public:
    void push(T value) {
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (size_ < N) {
            size_++;
        }
    }

    // Copy contents to a vector for range processing
    std::vector<T> to_vector() const {
        std::vector<T> result;
        result.reserve(size_);
        for (size_t i = 0; i < size_; ++i) {
            size_t pos = (head_ + N - size_ + i) % N;
            result.push_back(data_[pos]);
        }
        return result;
    }

    // Direct element access for range-based for
    T operator[](size_t i) const {
        size_t pos = (head_ + N - size_ + i) % N;
        return data_[pos];
    }

    size_t size() const { return size_; }

private:
    std::array<T, N> data_{};
    size_t head_ = 0;
    size_t size_ = 0;
};

void demo_basic_pipeline() {
    std::cout << "=== Ring Buffer Pipeline ===" << std::endl;

    RingBuffer<int, 10> buffer;

    // Add elements
    for (int i = 0; i < 8; ++i) {
        buffer.push(i);
    }

    // Copy to vector, then process with pipeline
    auto data = buffer.to_vector();
    auto result = data
        | std::views::filter([](int x) { return x % 2 == 0; })
        | std::views::transform([](int x) { return x * x; });

    std::cout << "Even squares: ";
    for (int x : result) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_overwrite_behavior() {
    std::cout << "\n=== Overwrite Behavior ===" << std::endl;

    RingBuffer<int, 5> buffer;

    std::cout << "Filling buffer of size 5..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        buffer.push(i);
    }

    std::cout << "Initial: ";
    for (size_t i = 0; i < buffer.size(); ++i) {
        std::cout << buffer[i] << " ";
    }
    std::cout << std::endl;

    // Add more (should overwrite oldest)
    std::cout << "Adding 3 more..." << std::endl;
    for (int i = 5; i < 8; ++i) {
        buffer.push(i);
    }

    std::cout << "After overwrite: ";
    for (size_t i = 0; i < buffer.size(); ++i) {
        std::cout << buffer[i] << " ";
    }
    std::cout << std::endl;
}

void demo_sensor_buffer() {
    std::cout << "\n=== Sensor Data Buffer ===" << std::endl;

    RingBuffer<uint16_t, 8> adc_buffer;

    // Simulate adding ADC readings
    for (uint16_t v : {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000}) {
        adc_buffer.push(v);
        if (adc_buffer.size() > 5) {
            auto data = adc_buffer.to_vector();
            auto avg_view = data
                | std::views::transform([](uint16_t v) { return v * 3.3f / 4095.0f; })
                | std::views::take(5);

            float sum = 0;
            for (float v : avg_view) {
                sum += v;
            }
            std::cout << "Average (last 5): " << sum / 5.0f << " V" << std::endl;
        }
    }
}

void demo_filter_overflow() {
    std::cout << "\n=== Filter Overflow Values ===" << std::endl;

    RingBuffer<uint16_t, 6> buffer;

    // Add some values with one out-of-range
    for (uint16_t v : {100, 4500, 200, 300, 400, 500}) {
        buffer.push(v);
    }

    // Filter out invalid ADC values
    auto data = buffer.to_vector();
    auto valid = data
        | std::views::filter([](uint16_t v) { return v >= 100 && v <= 4095; });

    std::cout << "Valid readings: ";
    for (uint16_t v : valid) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
}

int main() {
    demo_basic_pipeline();
    demo_overwrite_behavior();
    demo_sensor_buffer();
    demo_filter_overflow();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- Ring buffers can be converted to vectors for range processing" << std::endl;
    std::cout << "- This avoids iterator lifetime issues with views" << std::endl;
    std::cout << "- Ring buffers are common in embedded systems" << std::endl;
    std::cout << "- Use to_vector() when pipeline processing is needed" << std::endl;

    return 0;
}
