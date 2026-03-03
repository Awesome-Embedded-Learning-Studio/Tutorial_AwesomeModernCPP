// using_register.cpp
// 类型安全的寄存器访问体系示例

#include <cstdint>
#include <type_traits>
#include <iostream>

// 基础类型别名
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using i32 = std::int32_t;

// 寄存器宽度类型
using Reg8  = volatile u8;
using Reg16 = volatile u16;
using Reg32 = volatile u32;

// 寄存器模板
template<typename T, u32 Address>
struct Register {
    using value_type = T;

    static inline T read() {
        return *reinterpret_cast<T*>(Address);
    }

    static inline void write(T value) {
        *reinterpret_cast<T*>(Address) = value;
    }
};

// 具体寄存器定义（模拟的 GPIO 外设）
namespace GPIOA {
    constexpr u32 Base = 0x40010800;

    using MODER = Register<Reg32, Base + 0x00>;
    using ODR   = Register<Reg32, Base + 0x0C>;
    using IDR   = Register<Reg32, Base + 0x08>;
}

// 模拟硬件寄存器
static volatile u32 mock_gpio_moder = 0;
static volatile u32 mock_gpio_odr = 0;

int main() {
    std::cout << "=== Type-Safe Register Access ===\n\n";

    // 类型别名让代码更清晰
    std::cout << "Type sizes:\n";
    std::cout << "  sizeof(u8): " << sizeof(u8) << "\n";
    std::cout << "  sizeof(u32): " << sizeof(u32) << "\n";
    std::cout << "  sizeof(Reg32): " << sizeof(Reg32) << "\n";

    // 模拟寄存器操作（演示类型安全）
    std::cout << "\nRegister operations:\n";

    // 读取-修改-写入模式
    u32 moder = 0x12345678;
    std::cout << "  Original MODER: 0x" << std::hex << moder << std::dec << "\n";

    // 清除位 [11:10]
    moder = moder & ~(0b11 << 10);
    std::cout << "  After clear: 0x" << std::hex << moder << std::dec << "\n";

    // 设置位 [11:10] = 01
    moder = moder | (0b01 << 10);
    std::cout << "  After set: 0x" << std::hex << moder << std::dec << "\n";

    // using 声明引入名字
    using std::uint32_t;
    uint32_t value = 42;
    std::cout << "\n  Using declaration value: " << value << "\n";

    return 0;
}
