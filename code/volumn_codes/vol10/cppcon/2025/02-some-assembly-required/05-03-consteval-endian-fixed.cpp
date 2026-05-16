/*
 * 修正版：编译期判断字节序的合法方案
 *
 * 方案 1：使用编译器内置宏 __BYTE_ORDER__（推荐，所有主流编译器支持）
 * 方案 2：使用 std::bit_cast（C++20，可在 constexpr/consteval 中使用）
 *
 * 编译命令：
 *   g++ -std=c++20 05-03-consteval-endian-fixed.cpp -o fixed && ./fixed
 */

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>

// 方案 1：编译器内置宏（最简洁可靠）
consteval bool is_little_endian_macro() {
    return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
}

// 方案 2：使用 std::bit_cast（C++20，类型安全）
// std::bit_cast 可以在常量表达式中使用，而 reinterpret_cast 不行
consteval bool is_little_endian_bitcast() {
    // 将整数 1 的字节表示转换为字节数组，检查第一个字节
    // 如果是小端序，最低有效字节（值为 1）存储在最低地址
    constexpr auto bytes = std::bit_cast<std::array<unsigned char, sizeof(int)>>(1);
    return bytes[0] == 1;
}

int main() {
    std::cout << "=== Endianness Detection (Compile-time) ===\n";
    std::cout << "Macro method:    " << (is_little_endian_macro() ? "little" : "big")
              << " endian\n";
    std::cout << "bit_cast method: " << (is_little_endian_bitcast() ? "little" : "big")
              << " endian\n";

    // 静态断言：两种方法结果必须一致
    static_assert(is_little_endian_macro() == is_little_endian_bitcast(),
                  "Both methods should agree on endianness");

    return 0;
}
