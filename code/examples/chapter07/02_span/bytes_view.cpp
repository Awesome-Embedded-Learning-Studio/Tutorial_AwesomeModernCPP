// bytes_view.cpp - 字节视图演示 (as_bytes, as_writable_bytes)
#include <span>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>

void print_bytes(std::string_view label, std::span<const std::byte> bytes) {
    std::cout << label << ": ";
    for (auto b : bytes) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(b) << ' ';
    }
    std::cout << std::dec << '\n';
}

int main() {
    // 演示 as_bytes - 将任意类型 span 转换为字节视图（只读）
    std::array<uint32_t, 4> data = {0x12345678, 0x9ABCDEF0, 0x11223344, 0x55667788};

    std::span<uint32_t> data_span = data;
    auto byte_view = std::as_bytes(data_span);

    std::cout << "Original uint32_t values:\n";
    for (auto v : data) {
        std::cout << "0x" << std::hex << v << ' ';
    }
    std::cout << std::dec << "\n\n";

    std::cout << "As bytes (little-endian):\n";
    print_bytes("Byte view", byte_view);

    // 演示 as_writable_bytes - 可写字节视图
    std::array<uint8_t, 8> buffer = {};
    std::span<uint8_t> buffer_span = buffer;

    auto writable_bytes = std::as_writable_bytes(buffer_span);
    std::cout << "\nBefore writing:\n";
    print_bytes("Buffer", std::as_bytes(buffer_span));

    // 写入一些字节
    for (size_t i = 0; i < writable_bytes.size(); ++i) {
        writable_bytes[i] = static_cast<std::byte>(i * 2 + 1);
    }

    std::cout << "\nAfter writing:\n";
    print_bytes("Buffer", std::as_bytes(buffer_span));

    // 演示从 uint8_t span 转换
    std::array<uint8_t, 5> raw_data = {0x10, 0x20, 0x30, 0x40, 0x50};
    std::span<const uint8_t> raw_span = raw_data;
    auto raw_bytes = std::as_bytes(raw_span);

    std::cout << "\nRaw uint8_t data as bytes:\n";
    print_bytes("Raw", raw_bytes);

    // 注意：对于已经是 uint8_t 的 span，as_bytes 不会改变表示
    std::cout << "\nNote: For uint8_t span, as_bytes() doesn't change representation\n";
    std::cout << "raw_span.size(): " << raw_span.size() << '\n';
    std::cout << "raw_bytes.size(): " << raw_bytes.size() << '\n';

    return 0;
}
