// enum_class_basics.cpp
// 示例：传统 enum vs enum class 基本对比

#include <cstdint>
#include <iostream>

// 传统 enum（容易隐式转换）
enum Color { Red, Green, Blue };

void setColor(int c) {
    std::cout << "Set color (int): " << c << "\n";
}

// 强类型枚举
enum class EColor : uint8_t { Red, Green, Blue };

void setEColor(EColor c) {
    std::cout << "Set EColor: " << static_cast<int>(c) << "\n";
}

int main() {
    // 传统 enum - 隐式转换
    setColor(Red);  // 隐式转换成 int，有可能传错值
    setColor(42);   // 也可以接受任意 int，不安全

    // 强类型枚举 - 必须显式使用 EColor
    setEColor(EColor::Red);  // 必须显式使用 EColor，安全
    // setEColor(42);         // 编译错误！不能隐式转换

    // 验证大小
    static_assert(sizeof(EColor) == 1, "EColor 应该是 1 字节");

    std::cout << "sizeof(Color): " << sizeof(Color) << "\n";
    std::cout << "sizeof(EColor): " << sizeof(EColor) << "\n";

    return 0;
}
