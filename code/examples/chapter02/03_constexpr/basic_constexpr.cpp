// constexpr 基础示例：编译期计算

#include <iostream>
#include <array>
#include <cstdint>

// 简单的 constexpr 函数
constexpr int square(int x) {
    return x * x;
}

// 更复杂的 constexpr：编译期生成查找表
constexpr std::array<int, 5> generate_square_table() {
    std::array<int, 5> table{};
    for (int i = 0; i < 5; ++i) {
        table[i] = square(i);
    }
    return table;
}

// 编译期常量表
constexpr auto SQUARE_TABLE = generate_square_table();

// 寄存器位定义示例（嵌入式常见）
constexpr uint32_t GPIO_PIN_MASK(uint8_t pin) {
    return 1U << pin;
}

// 编译期计算波特率分频器
constexpr uint32_t calculate_baud_divisor(uint32_t cpu_freq, uint32_t baud, uint32_t oversample = 16) {
    return cpu_freq / (oversample * baud);
}

// 常用的 UART 配置
constexpr uint32_t UART_DIVISOR_115200 = calculate_baud_divisor(72000000, 115200);
constexpr uint32_t UART_DIVISOR_9600 = calculate_baud_divisor(72000000, 9600);

int main() {
    std::cout << "=== constexpr 示例 ===" << std::endl;

    // 编译期计算的值
    std::cout << "square(8) = " << square(8) << std::endl;
    std::cout << "SQUARE_TABLE[3] = " << SQUARE_TABLE[3] << std::endl;

    // GPIO 位掩码
    std::cout << "\n--- 寄存器位定义 ---" << std::endl;
    std::cout << "GPIO_PIN_5_MASK = 0x" << std::hex << GPIO_PIN_MASK(5) << std::dec << std::endl;

    // UART 波特率分频器
    std::cout << "\n--- UART 波特率分频器（编译期计算）---" << std::endl;
    std::cout << "115200 baud 分频器 = " << UART_DIVISOR_115200 << std::endl;
    std::cout << "9600 baud 分频器 = " << UART_DIVISOR_9600 << std::endl;

    // 验证编译期计算
    static_assert(UART_DIVISOR_115200 == 39, "波特率分频器应该在编译期计算");
    static_assert(SQUARE_TABLE[4] == 16, "查表应该在编译期生成");

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. constexpr 让计算在编译期完成，运行时无开销" << std::endl;
    std::cout << "2. 查找表可以在编译期生成，存储在 Flash 中" << std::endl;
    std::cout << "3. 寄存器位定义、波特率分频器等配置，编译期确定" << std::endl;
    std::cout << "4. static_assert 验证值确实在编译期确定" << std::endl;

    return 0;
}
