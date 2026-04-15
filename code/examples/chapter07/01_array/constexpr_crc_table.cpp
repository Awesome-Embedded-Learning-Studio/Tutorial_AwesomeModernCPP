// constexpr_crc_table.cpp - 编译期 CRC 表生成
#include <array>
#include <cstdint>
#include <iostream>

// 编译期生成 CRC32 查找表
constexpr std::array<uint32_t, 256> make_crc_table() {
    std::array<uint32_t, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        uint32_t crc = static_cast<uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

// 表在编译期计算，存储在只读段
constexpr auto crc_table = make_crc_table();

// 编译期验证表中的几个关键值
static_assert(crc_table[0] == 0x00000000, "CRC table[0] mismatch");
static_assert(crc_table[1] == 0x77073096, "CRC table[1] mismatch");
static_assert(crc_table[255] == 0x2D02EF8D, "CRC table[255] mismatch");

// 计算 CRC32 校验和
uint32_t compute_crc(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

int main() {
    std::cout << "CRC32 Table (first 16 entries, computed at compile time):\n";
    for (size_t i = 0; i < 16; ++i) {
        std::cout << "table[" << i << "] = 0x" << std::hex << crc_table[i] << std::dec << '\n';
    }

    // 计算 CRC32 示例
    const char* test_data = "Hello, World!";
    uint32_t crc = compute_crc(reinterpret_cast<const uint8_t*>(test_data), 13);

    std::cout << "\nCRC32 of \"" << test_data << "\" = 0x" << std::hex << crc << std::dec << '\n';

    return 0;
}
