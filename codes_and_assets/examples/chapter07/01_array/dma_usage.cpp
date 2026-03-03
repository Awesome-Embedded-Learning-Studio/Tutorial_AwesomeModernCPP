// dma_usage.cpp - 演示 std::array 在 DMA 场景的使用
#include <array>
#include <cstdint>
#include <iostream>

// 模拟 HAL 层的 DMA 传输函数
void hal_dma_transmit(const uint8_t* data, size_t length) {
    std::cout << "DMA transmitting " << length << " bytes from " << static_cast<const void*>(data) << ":\n";
    for (size_t i = 0; i < length; ++i) {
        std::cout << std::hex << int(data[i]) << ' ';
        if ((i + 1) % 16 == 0) std::cout << '\n';
    }
    std::cout << std::dec << "\n";
}

// 模拟 HAL 层的 DMA 接收函数
void hal_dma_receive(uint8_t* buffer, size_t length) {
    std::cout << "DMA receiving " << length << " bytes to " << static_cast<void*>(buffer) << '\n';
    // 模拟接收数据
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<uint8_t>(i * 2);
    }
}

int main() {
    // DMA 发送场景
    std::array<uint8_t, 32> tx_buffer{};
    for (size_t i = 0; i < tx_buffer.size(); ++i) {
        tx_buffer[i] = static_cast<uint8_t>(i);
    }

    // std::array 保证连续内存，可以安全地传给 DMA
    hal_dma_transmit(tx_buffer.data(), tx_buffer.size());

    // DMA 接收场景
    std::array<uint8_t, 64> rx_buffer{};
    hal_dma_receive(rx_buffer.data(), rx_buffer.size());

    // 验证接收的数据
    std::cout << "First 8 bytes received: ";
    for (size_t i = 0; i < 8; ++i) {
        std::cout << std::hex << int(rx_buffer[i]) << ' ';
    }
    std::cout << std::dec << '\n';

    // 注意：用于 DMA 的数组元素类型应该是 trivially copyable
    static_assert(std::is_trivially_copyable_v<uint8_t>,
                  "DMA buffer type must be trivially copyable");

    return 0;
}
