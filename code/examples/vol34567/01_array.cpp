// std::array: compile-time fixed-size array demo
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

int main() {
    // Value-initialized -> all zeros
    std::array<uint8_t, 8> buf{};

    buf[0] = 0xAA;
    buf.at(1) = 0x55;

    // Fill with a value using STL algorithm
    std::fill(buf.begin(), buf.end(), 0xFF);

    // Range-based for loop
    for (auto b : buf) {
        std::cout << static_cast<int>(b) << ' ';
    }
    std::cout << '\n';

    // Compile-time lookup table with constexpr
    constexpr auto make_crc_table = [] {
        std::array<uint32_t, 256> t{};
        for (size_t i = 0; i < 256; ++i) {
            uint32_t crc = static_cast<uint32_t>(i);
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
            }
            t[i] = crc;
        }
        return t;
    };

    constexpr auto crc_table = make_crc_table();
    static_assert(crc_table[0] == 0x00000000u);
    static_assert(crc_table[255] == 0x2D02EF8Du);

    std::cout << "CRC table[128] = 0x" << std::hex << crc_table[128] << '\n';

    // Structured binding (C++17)
    std::array<int, 3> a = {1, 2, 3};
    auto [x, y, z] = a;
    std::cout << std::dec << "Structured binding: " << x << ',' << y << ',' << z << '\n';

    // .size() is constexpr, .data() gives raw pointer
    std::cout << "sizeof(buf) = " << sizeof(buf) << '\n';
    std::cout << "buf.size() = " << buf.size() << '\n';

    return 0;
}
