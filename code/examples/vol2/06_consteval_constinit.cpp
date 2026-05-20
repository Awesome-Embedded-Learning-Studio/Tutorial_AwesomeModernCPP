// 06_consteval_constinit.cpp
// C++20 consteval 强制编译期哈希与 constinit 可变全局变量

#include <cstdint>
#include <iostream>

consteval std::uint32_t fnv1a32(const char* str, std::size_t len) {
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

template <std::size_t N> consteval std::uint32_t command_id(const char (&s)[N]) {
    return fnv1a32(s, N - 1);
}

constexpr auto kIdStart = command_id("START");
constexpr auto kIdStop = command_id("STOP");
constexpr auto kIdReset = command_id("RESET");
static_assert(kIdStart != kIdStop);

consteval int validate_buffer_size(int size) {
    return size > 0 && size <= 4096 && (size & (size - 1)) == 0 ? size : throw "Invalid";
}

constexpr int kBufferSize = validate_buffer_size(1024);
constinit int gMutableVal = 42;

int main() {
    std::cout << "=== consteval 演示 ===\n\n";

    std::cout << "1. 编译期命令 ID:\n";
    std::cout << "  \"START\" -> 0x" << std::hex << kIdStart << "\n";
    std::cout << "  \"STOP\"  -> 0x" << kIdStop << "\n";
    std::cout << "  \"RESET\" -> 0x" << kIdReset << std::dec << "\n\n";

    std::cout << "2. 编译期配置校验: kBufferSize = " << kBufferSize << "\n\n";

    std::cout << "=== constinit 演示 ===\n\n";
    std::cout << "  gMutableVal (constinit) = " << gMutableVal << "\n";
    gMutableVal = 100;
    std::cout << "  修改后 gMutableVal = " << gMutableVal << "\n";

    return 0;
}
