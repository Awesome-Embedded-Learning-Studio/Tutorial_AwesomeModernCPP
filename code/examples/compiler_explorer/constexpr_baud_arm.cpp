#include <cstdint>

std::uint32_t calculate_baud_divisor_runtime(std::uint32_t cpu_freq, std::uint32_t baud) {
    return cpu_freq / (16u * baud);
}

constexpr std::uint32_t calculate_baud_divisor_constexpr(std::uint32_t cpu_freq,
                                                         std::uint32_t baud) {
    return cpu_freq / (16u * baud);
}

constexpr std::uint32_t divisor = calculate_baud_divisor_constexpr(72000000u, 115200u);
static_assert(divisor == 39u);

std::uint32_t runtime_divisor(std::uint32_t baud) {
    return calculate_baud_divisor_runtime(72000000u, baud);
}

std::uint32_t constexpr_divisor() {
    return divisor;
}
