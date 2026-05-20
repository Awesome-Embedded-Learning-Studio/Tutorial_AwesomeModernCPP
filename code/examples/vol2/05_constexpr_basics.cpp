// 05_constexpr_basics.cpp
// constexpr 基础：编译期阶乘、CRC-32 查找表生成与 static_assert 校验

#include <array>
#include <cstdint>
#include <iostream>

constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

constexpr std::array<std::uint32_t, 256> make_crc32_table() {
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;
    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto kCrc32Table = make_crc32_table();
static_assert(factorial(5) == 120);
static_assert(kCrc32Table[0] == 0x00000000u);
static_assert(kCrc32Table[1] == 0x77073096u);

int main() {
    std::cout << "=== constexpr 基础演示 ===\n\n";

    std::cout << "1. 编译期求值:\n";
    constexpr int kFact10 = factorial(10);
    std::cout << "  factorial(10) = " << kFact10 << "\n\n";

    std::cout << "2. 运行时求值:\n";
    int n = 6;
    std::cout << "  factorial(" << n << ") = " << factorial(n) << "\n\n";

    std::cout << "3. 编译期 CRC-32 查找表:\n";
    std::cout << "  table[0]   = 0x" << std::hex << kCrc32Table[0] << "\n";
    std::cout << "  table[1]   = 0x" << kCrc32Table[1] << "\n";
    std::cout << "  table[255] = 0x" << kCrc32Table[255] << std::dec << "\n\n";

    std::cout << "4. static_assert 编译期校验通过\n";

    return 0;
}
