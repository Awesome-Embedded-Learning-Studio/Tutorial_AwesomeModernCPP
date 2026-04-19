// basic_usage.cpp - std::span 基本用法演示
#include <span>
#include <cstdint>
#include <vector>
#include <array>
#include <iostream>

// 接受多种容器来源的统一函数接口
void print_bytes(std::span<const uint8_t> s) {
    for (auto b : s) std::cout << std::hex << int(b) << ' ';
    std::cout << std::dec << '\n';
}

// 演示可写 span
void fill_with_value(std::span<uint8_t> s, uint8_t value) {
    for (auto& b : s) {
        b = value;
    }
}

int main() {
    // 从内置数组构造
    uint8_t buffer[] = {0x10, 0x20, 0x30};
    std::cout << "From C array: ";
    print_bytes(buffer);

    // 从 vector 构造
    std::vector<uint8_t> v = {1, 2, 3, 4};
    std::cout << "From vector: ";
    print_bytes(v);

    // 从 std::array 构造
    std::array<uint8_t, 3> a = {9, 8, 7};
    std::cout << "From std::array: ";
    print_bytes(a);

    // 从 pointer + size 构造
    std::cout << "From ptr+size (first 2 of vector): ";
    print_bytes({v.data(), 2});

    // 演示可写 span
    std::array<uint8_t, 5> mutable_array{};
    fill_with_value(mutable_array, 0x42);
    std::cout << "After fill with 0x42: ";
    print_bytes(mutable_array);

    // 演示 span 的成员函数
    std::span<const uint8_t> s = buffer;
    std::cout << "\nspan member functions:\n";
    std::cout << "size(): " << s.size() << '\n';
    std::cout << "size_bytes(): " << s.size_bytes() << '\n';
    std::cout << "data(): " << static_cast<const void*>(s.data()) << '\n';
    std::cout << "empty(): " << s.empty() << '\n';
    std::cout << "front(): " << int(s.front()) << '\n';
    std::cout << "back(): " << int(s.back()) << '\n';

    return 0;
}
