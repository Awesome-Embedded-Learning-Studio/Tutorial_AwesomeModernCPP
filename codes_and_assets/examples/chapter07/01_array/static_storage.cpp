// static_storage.cpp - 演示静态存储与栈存储的选择
#include <array>
#include <cstdint>
#include <iostream>

// 小数组 - 可以放在栈上
void stack_example() {
    const int stack_size = 64;
    std::array<uint8_t, stack_size> small_buf{};
    small_buf[0] = 0x42;
    std::cout << "Stack array at: " << static_cast<void*>(small_buf.data()) << '\n';
}

// 大数组 - 应放在静态区
static std::array<uint8_t, 1024> big_buf;  // 在 .bss，程序启动后分配

// 只读数据 - 可以放 flash（使用 constexpr）
constexpr std::array<uint8_t, 16> readonly_data = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

int main() {
    stack_example();

    std::cout << "Static array at: " << static_cast<void*>(big_buf.data()) << '\n';
    std::cout << "Readonly array at: " << static_cast<const void*>(readonly_data.data()) << '\n';

    // 静态数组在程序启动时自动零初始化
    bool all_zero = true;
    for (auto b : big_buf) {
        if (b != 0) {
            all_zero = false;
            break;
        }
    }
    std::cout << "Big array all zero: " << (all_zero ? "yes" : "no") << '\n';

    return 0;
}
