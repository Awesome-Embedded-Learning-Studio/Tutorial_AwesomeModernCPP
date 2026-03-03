// basic_usage.cpp - std::array 基本用法演示
#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>

int main() {
    // value-initialized -> all zeros
    std::array<uint8_t, 8> buf{};
    buf[0] = 0xAA;
    // .at 会做边界检查（抛异常）- 裸机环境慎用
    buf.at(1) = 0x55;

    // 兼容 STL 算法
    std::fill(buf.begin(), buf.end(), 0xFF);

    // 范围 for 遍历
    for (auto b : buf) std::cout << int(b) << ' ';
    std::cout << '\n';

    // 其他有用的成员函数
    std::cout << "size: " << buf.size() << '\n';
    std::cout << "front: " << int(buf.front()) << '\n';
    std::cout << "back: " << int(buf.back()) << '\n';

    // 填充为不同值
    std::fill_n(buf.begin(), 4, 0x11);
    std::fill_n(buf.begin() + 4, 4, 0x22);

    for (auto b : buf) std::cout << std::hex << int(b) << ' ';
    std::cout << std::dec << '\n';

    return 0;
}
