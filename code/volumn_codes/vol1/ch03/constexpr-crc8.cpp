#include <array>
#include <cstdint>
#include <cstdio>

/// @brief 编译期平方计算
constexpr int square(int x)
{
    return x * x;
}

/// @brief 编译期阶乘（迭代版，C++14 风格）
constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

/// @brief 编译期整数幂
constexpr int power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; ++i) {
        result *= base;
    }
    return result;
}

/// @brief 编译期生成 CRC8 查找表
constexpr std::array<uint8_t, 256> make_crc8_table()
{
    std::array<uint8_t, 256> table{};
    constexpr uint8_t kPoly = 0x07;
    for (int i = 0; i < 256; ++i) {
        uint8_t byte = static_cast<uint8_t>(i);
        for (int bit = 0; bit < 8; ++bit) {
            byte = (byte & 0x80) ? (byte << 1) ^ kPoly : (byte << 1);
        }
        table[i] = byte;
    }
    return table;
}

// 编译期验证
static_assert(square(5) == 25, "square(5) should be 25");
static_assert(square(-3) == 9, "square(-3) should be 9");
static_assert(factorial(5) == 120, "5! should be 120");
static_assert(factorial(10) == 3628800, "10! should be 3628800");
static_assert(power(2, 10) == 1024, "2^10 should be 1024");

// 编译期生成查找表
constexpr auto kCrc8Table = make_crc8_table();

/// @brief 用查找表计算 CRC8
uint8_t compute_crc8(const uint8_t* data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc = kCrc8Table[crc ^ data[i]];
    }
    return crc;
}

int main()
{
    printf("=== 编译期计算结果 ===\n");
    printf("square(5)     = %d\n", square(5));
    printf("factorial(10) = %d\n", factorial(10));
    printf("power(2, 16)  = %d\n", power(2, 16));
    printf("CRC8 table[0] = 0x%02X\n", kCrc8Table[0]);
    printf("CRC8 table[1] = 0x%02X\n", kCrc8Table[1]);

    printf("\n=== 运行时 CRC8 计算 ===\n");
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t crc = compute_crc8(test_data, sizeof(test_data));
    printf("CRC8 of {01 02 03 04 05} = 0x%02X\n", crc);

    printf("\n=== 编译期 vs 运行时 ===\n");
    constexpr int kCompileTime = square(7);
    int runtime_input = 7;
    int runtime_result = square(runtime_input);
    printf("constexpr square(7) = %d\n", kCompileTime);
    printf("runtime  square(7)  = %d\n", runtime_result);
    printf("结果一致: %s\n",
           kCompileTime == runtime_result ? "是" : "否");

    return 0;
}
