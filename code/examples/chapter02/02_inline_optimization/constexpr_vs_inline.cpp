// constexpr vs inline：编译期计算 vs 调用展开

#include <iostream>
#include <array>
#include <cstdint>

// ===== inline 函数 =====
// 关注点：是否在调用点展开
inline int square_inline(int x) {
    return x * x;
}

// ===== constexpr 函数 =====
// 关注点：是否可以在编译期计算
constexpr int square_constexpr(int x) {
    return x * x;
}

// ===== constexpr 编译期查表 =====
constexpr std::array<int, 256> generate_crc_table() {
    std::array<int, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[i] = i ^ (i >> 1);  // 简化的 CRC
    }
    return table;
}

// 这个表在编译期生成，存储在 Flash 中
constexpr auto CRC_TABLE = generate_crc_table();

// ===== 内联版本的寄存器配置 =====
inline uint32_t config_uart_inline(uint32_t baud) {
    // 运行时计算
    return 72000000 / (16 * baud);
}

// ===== constexpr 版本的寄存器配置 =====
constexpr uint32_t config_uart_constexpr(uint32_t baud) {
    // 可以在编译期计算
    return 72000000 / (16 * baud);
}

// 预计算的常用波特率配置
constexpr uint32_t UART_BAUD_115200 = config_uart_constexpr(115200);
constexpr uint32_t UART_BAUD_9600 = config_uart_constexpr(9600);

// ===== 验证编译期计算 =====
static_assert(UART_BAUD_115200 == 39, "波特率应该在编译期计算");
static_assert(square_constexpr(8) == 64, "平方应该在编译期计算");

int main() {
    std::cout << "=== constexpr vs inline 示例 ===" << std::endl;

    // ===== 运行时计算对比 =====
    std::cout << "\n--- 运行时计算 ---" << std::endl;
    volatile int x = 8;  // volatile 防止编译期优化

    // inline 版本：调用可能被展开，但计算在运行时
    int result_inline = square_inline(x);
    std::cout << "square_inline(8) = " << result_inline << " (运行时计算)" << std::endl;

    // constexpr 版本：因为 x 是 volatile，所以在运行时计算
    int result_constexpr = square_constexpr(x);
    std::cout << "square_constexpr(8) = " << result_constexpr << " (运行时计算)" << std::endl;

    // ===== 编译期计算 =====
    std::cout << "\n--- 编译期计算 ---" << std::endl;

    // constexpr 函数用常量参数调用：编译期计算
    int compile_time_result = square_constexpr(8);
    std::cout << "square_constexpr(8) = " << compile_time_result << " (编译期计算，直接是 64)" << std::endl;

    // ===== 查表访问 =====
    std::cout << "\n--- 编译期查表 ---" << std::endl;
    std::cout << "CRC_TABLE[0] = " << CRC_TABLE[0] << std::endl;
    std::cout << "CRC_TABLE[255] = " << CRC_TABLE[255] << std::endl;
    std::cout << "整个表在编译期生成，存储在 Flash 中" << std::endl;

    // ===== UART 配置 =====
    std::cout << "\n--- UART 配置 ---" << std::endl;
    std::cout << "UART_BAUD_115200 = " << UART_BAUD_115200 << " (编译期常量)" << std::endl;
    std::cout << "UART_BAUD_9600 = " << UART_BAUD_9600 << " (编译期常量)" << std::endl;

    // 运行时计算（如果波特率是动态配置的）
    uint32_t dynamic_baud = 57600;
    uint32_t runtime_config = config_uart_inline(dynamic_baud);
    std::cout << "运行时配置 57600 baud = " << runtime_config << std::endl;

    std::cout << "\n关键区别：" << std::endl;
    std::cout << "1. inline: 关注调用是否展开（可能还是运行时计算）" << std::endl;
    std::cout << "2. constexpr: 关注是否能在编译期计算（运行时消失）" << std::endl;
    std::cout << "3. constexpr 也可以内联，但内联不一定在编译期计算" << std::endl;
    std::cout << "4. constexpr 变量可以用作数组大小、模板参数等" << std::endl;

    std::cout << "\n嵌入式建议：" << std::endl;
    std::cout << "- 常量配置、查表：用 constexpr" << std::endl;
    std::cout << "- 类型安全的封装：用 inline" << std::endl;
    std::cout << "- 两者可以同时使用：inline constexpr" << std::endl;

    return 0;
}
