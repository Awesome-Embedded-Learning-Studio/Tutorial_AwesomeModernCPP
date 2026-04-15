// inline 函数 vs 宏：类型安全与副作用

#include <iostream>
#include <cstdint>

// ===== 宏定义（旧方式）=====
#define SQUARE_MACRO(x) ((x) * (x))
#define MAX_MACRO(a, b) ((a) > (b) ? (a) : (b))
#define INCREMENT_MACRO(x) (++x)

// ===== inline 函数（现代 C++ 方式）=====
inline constexpr int square(int x) {
    return x * x;
}

template<typename T>
inline constexpr T max_val(T a, T b) {
    return (a > b) ? a : b;
}

template<typename T>
inline T increment(T& x) {
    return ++x;
}

// ===== 寄存器访问示例（嵌入式场景）=====
// 模拟寄存器读取
inline constexpr uint32_t gpio_read(uint32_t* base_addr, uint8_t pin) {
    return (*base_addr) & (1U << pin);
}

// 宏版本的寄存器读取（不推荐）
#define GPIO_READ_MACRO(base, pin) ((*base) & (1U << (pin)))

int main() {
    std::cout << "=== inline 函数 vs 宏示例 ===" << std::endl;

    // ===== 问题 1: 宏的副作用 =====
    std::cout << "\n--- 问题 1: 宏的副作用 ---" << std::endl;

    int x = 5;
    std::cout << "使用宏: SQUARE_MACRO(++x) = " << SQUARE_MACRO(++x) << std::endl;
    std::cout << "但 x 现在是: " << x << " (被自增了 2 次！)" << std::endl;

    x = 5;
    std::cout << "使用 inline: square(++x) = " << square(++x) << std::endl;
    std::cout << "x 现在是: " << x << " (只自增 1 次，正确)" << std::endl;

    // ===== 问题 2: 宏的类型安全 =====
    std::cout << "\n--- 问题 2: 类型安全 ---" << std::endl;

    int a = 10, b = 20;
    double c = 3.14, d = 2.71;

    std::cout << "MAX_MACRO(a, b) = " << MAX_MACRO(a, b) << std::endl;
    std::cout << "max_val(c, d) = " << max_val(c, d) << " (支持任意类型)" << std::endl;

    // 宏的意外行为
    std::cout << "MAX_MACRO(a++, b--) = " << MAX_MACRO(a++, b--) << std::endl;
    std::cout << "a = " << a << ", b = " << b << " (副作用！)" << std::endl;

    // ===== 问题 3: 作用域和调试 =====
    std::cout << "\n--- 问题 3: 调试体验 ---" << std::endl;
    std::cout << "宏在预处理阶段展开，难以调试" << std::endl;
    std::cout << "inline 函数有符号信息，可以设置断点" << std::endl;

    // ===== 嵌入式场景：寄存器访问 =====
    std::cout << "\n--- 嵌入式场景：寄存器访问 ---" << std::endl;

    uint32_t gpio_reg = 0x0000010F;  // 模拟 GPIO 寄存器，bit 0-3 置位
    uint32_t* gpio_base = &gpio_reg;

    std::cout << "GPIO 寄存器值: 0x" << std::hex << gpio_reg << std::dec << std::endl;
    std::cout << "GPIO_READ_MACRO(gpio_base, 3) = " << GPIO_READ_MACRO(gpio_base, 3) << std::endl;
    std::cout << "gpio_read(gpio_base, 3) = " << gpio_read(gpio_base, 3) << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. inline 函数类型安全，宏不是" << std::endl;
    std::cout << "2. inline 函数避免副作用问题" << std::endl;
    std::cout << "3. inline 函数支持模板，通用性更强" << std::endl;
    std::cout << "4. inline 函数可以在编译期计算（constexpr）" << std::endl;
    std::cout << "5. inline 函数更好的调试体验" << std::endl;

    return 0;
}
