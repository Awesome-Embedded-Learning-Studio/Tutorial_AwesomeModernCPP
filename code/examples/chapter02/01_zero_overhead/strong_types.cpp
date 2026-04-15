// 强类型示例：类型安全的单位系统

#include <cstdint>
#include <iostream>

// ==================== 易错：单位混淆 =====================
void delay_raw(uint32_t time) {
    // time 是毫秒还是微秒？不明确！
    std::cout << "延迟 " << time << " 时间单位（未知）" << std::endl;
}

// ==================== 零开销强类型 =====================
struct Milliseconds { uint32_t value; };
struct Microseconds { uint32_t value; };

void delay(Milliseconds ms) {
    std::cout << "延迟 " << ms.value << " 毫秒" << std::endl;
}

void delay_us(Microseconds us) {
    std::cout << "延迟 " << us.value << " 微秒" << std::endl;
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 强类型示例：零开销类型安全 ===" << std::endl;

    std::cout << "\n--- 原始类型（单位不明确）---" << std::endl;
    delay_raw(100);  // 100 是什么单位？

    std::cout << "\n--- 强类型（单位清晰）---" << std::endl;
    delay(Milliseconds{100});   // 清晰：100 毫秒
    delay_us(Microseconds{1000}); // 清晰：1000 微秒

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. Milliseconds{100} 清楚表达 100 毫秒" << std::endl;
    std::cout << "2. 类型错误会在编译期捕获：delay(Microseconds{100}) 会报错" << std::endl;
    std::cout << "3. 强类型在运行时完全零开销" << std::endl;
    std::cout << "4. Milliseconds 本质就是 uint32_t，编译器会优化掉包装" << std::endl;

    // 验证零开销
    static_assert(sizeof(Milliseconds) == sizeof(uint32_t),
                  "强类型应该与原始类型大小相同");

    return 0;
}
