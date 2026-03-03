// uart_example.cpp - 模拟 UART 串口接收场景
#include "ring_buffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstdlib>

// 模拟硬件寄存器
std::atomic<bool> uart_data_ready{false};
std::atomic<uint8_t> uart_rx_reg{0};

// 接收缓冲区
RingBuffer<uint8_t, 128> rx_buffer;

// 模拟 UART 硬件接收中断
void USART_IRQHandler() {
    // 在真实硬件中，这是由硬件触发的中断
    uint8_t data = uart_rx_reg.load();
    uart_data_ready.store(false);

    // 中断里只做这件事：把数据放入缓冲区
    if (!rx_buffer.push(data)) {
        std::cout << "[ISR] Buffer overrun! Data lost.\n";
    }
}

// 模拟 UART 接收线程（模拟硬件产生数据）
void uart_rx_thread() {
    const char* test_message = "Hello, Embedded World!";
    size_t index = 0;

    while (test_message[index] != '\0') {
        // 模拟硬件接收一个字节
        uart_rx_reg.store(static_cast<uint8_t>(test_message[index]));
        uart_data_ready.store(true);

        // 触发中断（模拟）
        USART_IRQHandler();

        ++index;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// 处理单个字符
void process_char(uint8_t ch) {
    if (ch >= 32 && ch < 127) {
        std::cout << "Processing: '" << static_cast<char>(ch) << "'\n";
    } else {
        std::cout << "Processing: 0x" << std::hex << static_cast<int>(ch) << std::dec << '\n';
    }
}

int main() {
    std::cout << "=== UART Circular Buffer Example ===\n\n";

    // 启动模拟 UART 接收线程
    std::thread uart_thread(uart_rx_thread);

    // 主循环：处理接收到的数据
    int idle_count = 0;
    while (idle_count < 50) {  // 运行一段时间后退出
        uint8_t ch;
        if (rx_buffer.pop(ch)) {
            process_char(ch);
            idle_count = 0;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++idle_count;
        }
    }

    uart_thread.join();

    std::cout << "\n=== Summary ===\n";
    std::cout << "Buffer size: " << rx_buffer.size() << '\n';
    std::cout << "This pattern demonstrates:\n";
    std::cout << "- ISR pushes data quickly\n";
    std::cout << "- Main loop processes at its own pace\n";
    std::cout << "- No malloc, deterministic behavior\n";

    return 0;
}
