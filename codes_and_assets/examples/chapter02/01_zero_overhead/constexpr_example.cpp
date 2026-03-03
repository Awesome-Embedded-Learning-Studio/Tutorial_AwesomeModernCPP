// constexpr 编译期计算示例：运行时计算 vs 编译期计算

#include <cstdint>
#include <iostream>

// ==================== 运行时计算（浪费 CPU）====================
uint32_t calculate_baud_divisor_runtime(uint32_t cpu_freq, uint32_t baud) {
    return cpu_freq / (16 * baud);
}

// ==================== 编译期计算（零运行时开销）====================
constexpr uint32_t calculate_baud_divisor_constexpr(uint32_t cpu_freq, uint32_t baud) {
    return cpu_freq / (16 * baud);
}

// 这个值在编译时计算，直接嵌入代码
constexpr uint32_t DIVISOR = calculate_baud_divisor_constexpr(72000000, 115200);

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== constexpr 编译期计算示例 ===" << std::endl;

    // 运行时计算
    uint32_t divisor_runtime = calculate_baud_divisor_runtime(72000000, 115200);
    std::cout << "运行时计算: " << divisor_runtime << std::endl;

    // 编译期计算（已经是常量）
    std::cout << "编译期计算: " << DIVISOR << std::endl;

    // 验证编译期计算
    static_assert(DIVISOR == 39, "DIVISOR 应该在编译期计算为 39");

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. constexpr 函数在参数为编译期常量时，编译期就能计算结果" << std::endl;
    std::cout << "2. DIVISOR 是编译期常量，直接嵌入机器码，无运行时计算" << std::endl;
    std::cout << "3. 除法在 MCU 上很慢，constexpr 可以节省几十个时钟周期" << std::endl;
    std::cout << "4. static_assert 验证：DIVISOR = " << DIVISOR << " (编译期常量)" << std::endl;

    return 0;
}
