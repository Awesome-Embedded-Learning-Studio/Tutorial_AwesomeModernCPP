#include <iostream>
#include <concepts>
#include <cstdint>

consteval bool is_power_of_two(std::size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

template<typename S, std::size_t k>
concept BufferSpace = requires(S buf) {
    { buf.size() } -> std::convertible_to<std::size_t>;
    requires (S::size_value >= k);
    requires is_power_of_two(S::size_value);
};

struct SmallBuffer {
    static constexpr std::size_t size_value = 64;
    constexpr std::size_t size() const { return size_value; }
};

struct NetworkBuffer {
    static constexpr std::size_t size_value = 1024;
    constexpr std::size_t size() const { return size_value; }
};

template<typename S>
    requires BufferSpace<S, 128>
void process_buffer(S& buf) {
    constexpr std::size_t mask = S::size_value - 1;
    std::cout << "buffer size: " << buf.size() << ", mask: " << mask << "\n";
}

int main() {
    NetworkBuffer net;
    process_buffer(net);
    // SmallBuffer small; process_buffer(small);  // compile error: 64 < 128
    return 0;
}
