#include <cstdint>
#include <cstddef>

// 测试编译期策略模式的零开销特性

// CRC-32 策略
struct Crc32Strategy {
    static constexpr const char* name = "CRC-32";

    static constexpr std::uint32_t compute(const std::uint8_t* data, std::size_t len)
    {
        constexpr std::uint32_t kPoly = 0xEDB88320u;
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t idx = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFF);
            std::uint32_t entry = static_cast<std::uint32_t>(idx);
            for (int j = 0; j < 8; ++j) {
                entry = (entry & 1) ? ((entry >> 1) ^ kPoly) : (entry >> 1);
            }
            crc = (crc >> 8) ^ entry;
        }
        return crc ^ 0xFFFFFFFFu;
    }
};

// CRC-16-CCITT 策略
struct Crc16CcittStrategy {
    static constexpr const char* name = "CRC-16-CCITT";

    static constexpr std::uint16_t compute(const std::uint8_t* data, std::size_t len)
    {
        constexpr std::uint16_t kPoly = 0x1021u;
        std::uint16_t crc = 0xFFFFu;
        for (std::size_t i = 0; i < len; ++i) {
            crc ^= static_cast<std::uint16_t>(data[i]) << 8;
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 0x8000) ? ((crc << 1) ^ kPoly) : (crc << 1);
            }
        }
        return crc;
    }
};

// 编译期策略选择——零虚函数表、零运行时分派
template <typename Strategy>
constexpr auto checksum(const std::uint8_t* data, std::size_t len)
{
    return Strategy::compute(data, len);
}

int main()
{
    // 验证编译期策略选择确实在编译期完成
    constexpr std::uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};

    // 这些计算应该在编译期完成
    constexpr auto crc32_result = checksum<Crc32Strategy>(test_data, 4);
    constexpr auto crc16_result = checksum<Crc16CcittStrategy>(test_data, 4);

    // 验证不同策略确实产生不同结果
    static_assert(crc32_result != static_cast<std::uint32_t>(crc16_result),
                  "Different strategies should produce different results");

    // 验证策略名称是编译期常量
    static_assert(Crc32Strategy::name[0] == 'C',
                  "Strategy name should be compile-time constant");

    return 0;
}
