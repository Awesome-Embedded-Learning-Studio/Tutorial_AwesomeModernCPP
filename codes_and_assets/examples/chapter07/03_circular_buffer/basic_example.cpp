// basic_example.cpp - 循环缓冲区基本用法演示
#include "ring_buffer.h"
#include <iostream>

int main() {
    // 创建一个容量为 16 的循环缓冲区（实际可用 15）
    RingBuffer<int, 16> buffer;

    std::cout << "Ring Buffer Demo\n";
    std::cout << "Capacity: " << buffer.capacity() << "\n\n";

    // 测试：填充和读取
    std::cout << "=== Pushing 10 elements ===\n";
    for (int i = 0; i < 10; ++i) {
        if (buffer.push(i)) {
            std::cout << "Pushed: " << i << ", size: " << buffer.size() << '\n';
        }
    }

    std::cout << "\n=== Popping 5 elements ===\n";
    for (int i = 0; i < 5; ++i) {
        int value;
        if (buffer.pop(value)) {
            std::cout << "Popped: " << value << ", size: " << buffer.size() << '\n';
        }
    }

    std::cout << "\n=== Pushing 8 more elements ===\n";
    for (int i = 10; i < 18; ++i) {
        if (buffer.push(i)) {
            std::cout << "Pushed: " << i << ", size: " << buffer.size() << '\n';
        } else {
            std::cout << "Failed to push: " << i << " (buffer full)\n";
        }
    }

    std::cout << "\n=== Remaining contents ===\n";
    while (!buffer.empty()) {
        int value;
        buffer.pop(value);
        std::cout << value << ' ';
    }
    std::cout << "\n";

    std::cout << "\nFinal size: " << buffer.size() << '\n';
    std::cout << "Empty: " << (buffer.empty() ? "yes" : "no") << '\n';
    std::cout << "Full: " << (buffer.full() ? "yes" : "no") << '\n';

    return 0;
}
