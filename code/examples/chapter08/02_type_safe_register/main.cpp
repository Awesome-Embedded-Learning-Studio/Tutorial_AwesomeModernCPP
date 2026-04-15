// main.cpp
// 类型安全寄存器访问使用示例

#include "uart_regs.hpp"
#include <iostream>

// 模拟硬件寄存器存储
static uint32_t mock_uart_cr = 0;

// 在 0x40001000 地址放置我们的模拟寄存器
// 注意：这只是为了演示，实际硬件上这会是真实的 MMIO 地址

void uart_init() {
    // 设波特率分频
    uart_baud::write_raw(16);            // 直接写数值
    std::cout << "Set baud divider to: 16\n";

    // 设置模式
    uart_mode_f::write(uart_mode::TxRx); // 强类型枚举
    std::cout << "Set UART mode to TxRx\n";

    // 使能 UART
    uart_en::write_raw(1);
    std::cout << "Enable UART\n";
}

int main() {
    std::cout << "=== Type-Safe Register Access Demo ===\n\n";

    uart_init();

    std::cout << "\n=== Register Operations Demo ===\n";

    // 演示 set_bits 和 clear_bits
    uart_cr_t::set_bits(1 << 5);   // 设置位 5
    std::cout << "Set bit 5\n";

    uart_cr_t::clear_bits(1 << 5); // 清除位 5
    std::cout << "Clear bit 5\n";

    // 演示 modify
    uart_cr_t::modify([](uint32_t v) {
        return v | (1 << 3);  // 设置位 3
    });
    std::cout << "Modified register via lambda\n";

    // 读取字段
    auto baud_val = uart_baud::read_raw();
    std::cout << "Current baud divider: " << baud_val << "\n";

    auto mode_val = uart_mode_f::read_as<uart_mode>();
    std::cout << "Current mode: " << static_cast<int>(mode_val) << "\n";

    return 0;
}
