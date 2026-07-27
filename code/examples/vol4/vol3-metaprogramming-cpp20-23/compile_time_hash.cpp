// 配套 05-compile-time-strings.md「编译期哈希:不一定非得 NTTP」
// FNV-1a 编译期字符串哈希(纯 constexpr 函数,不需要 NTTP)
// 编译运行:g++ -std=c++20 -Wall -Wextra compile_time_hash.cpp -o cth && ./cth
#include <cstdint>
#include <iostream>
#include <string_view>

// FNV-1a 编译期字符串哈希(纯 constexpr 函数,不需要 NTTP)
// 64 位魔法数是算法规定的
constexpr std::uint64_t fnv1a_64(std::string_view s) {
    std::uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    for (char c : s) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL; // FNV prime
    }
    return hash;
}

int main() {
    // 这些哈希值在编译期就算定
    constexpr auto h_hello = fnv1a_64("hello");
    constexpr auto h_hello2 = fnv1a_64("hello");
    constexpr auto h_world = fnv1a_64("world");

    static_assert(h_hello == h_hello2, "same string, same hash");
    static_assert(h_hello != h_world, "different strings, different hash");

    std::cout << "fnv1a_64(\"hello\") = " << h_hello << "\n";
    std::cout << "fnv1a_64(\"world\") = " << h_world << "\n";
    std::cout << "编译期哈希断言通过\n";
}
