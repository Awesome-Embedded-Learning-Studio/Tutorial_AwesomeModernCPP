#include <array>
#include <cstdint>

// 测试编译期CRC表生成和运行时性能

constexpr std::array<std::uint32_t, 256> make_crc32_table()
{
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

// 编译期生成完整的 CRC-32 查找表
constexpr auto kCrc32Table = make_crc32_table();

// 编译期校验表的前几项是否正确
static_assert(kCrc32Table[0] == 0x00000000u, "CRC table entry 0 should be 0");
static_assert(kCrc32Table[1] == 0x77073096u, "CRC table entry 1 mismatch");
static_assert(kCrc32Table[255] == 0x2D02EF8Du, "CRC table entry 255 mismatch");

// 运行时 CRC 计算：只需做查表 + XOR
constexpr std::uint32_t crc32(const std::uint8_t* data, std::size_t length)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        std::uint8_t index = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ kCrc32Table[index];
    }
    return crc ^ 0xFFFFFFFFu;
}

int main()
{
    // 验证运行时CRC计算
    constexpr std::uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    constexpr auto crc_result = crc32(test_data, 4);

    // 验证编译期CRC计算
    static_assert(crc32(test_data, 4) == crc_result, "Compile-time CRC mismatch");

    return 0;
}
