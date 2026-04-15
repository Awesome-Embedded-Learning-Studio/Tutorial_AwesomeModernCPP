// GPIO 控制示例：C 风格 vs C++ 类型安全抽象

#include <cstdint>
#include <iostream>

// ==================== C 风格（传统做法）====================
// 直接操作寄存器 - 容易出错，有魔法数字
#define GPIO_PORT_A_C ((volatile uint32_t*)0x40020000)
#define PIN_5_C (1 << 5)

void set_pin_c() {
    *GPIO_PORT_A_C |= PIN_5_C;  // 容易出错，魔法数字
}

// ==================== C++ 类型安全抽象 ====================
// 类型安全的抽象 - 零开销，编译时展开
template<uint32_t Address>
class GPIO_Port {
    static volatile uint32_t& reg() {
        return *reinterpret_cast<volatile uint32_t*>(Address);
    }
public:
    static void set_pin(uint8_t pin) {
        reg() |= (1 << pin);
    }

    static void clear_pin(uint8_t pin) {
        reg() &= ~(1 << pin);
    }
};

// 类型别名，更清晰的代码
using GPIOA = GPIO_Port<0x40020000>;

void set_pin_cpp() {
    GPIOA::set_pin(5);  // 类型安全，可读性强
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== GPIO 控制示例 ===" << std::endl;
    std::cout << "C 风格：使用 #define 宏和直接寄存器操作" << std::endl;
    std::cout << "C++ 风格：使用类型安全的模板类" << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. C++ 版本在编译后生成的机器码与 C 版本相同" << std::endl;
    std::cout << "2. GPIO_Port<0x40020000> 和 GPIO_Port<0x40020400> 是不同类型" << std::endl;
    std::cout << "3. 编译器可以进行更好的优化，零开销抽象" << std::endl;

    return 0;
}
