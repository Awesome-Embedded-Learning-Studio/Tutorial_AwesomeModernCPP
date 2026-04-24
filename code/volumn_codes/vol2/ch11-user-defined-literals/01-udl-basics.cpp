/**
 * 用户自定义字面量基础示例
 * 验证各种 UDL 形式和标准库字面量
 */

#include <cstdint>
#include <chrono>
#include <string>
#include <iostream>
#include <cassert>

// ===== 基础整型 UDL =====
struct Milliseconds {
    std::uint64_t value;
    constexpr explicit Milliseconds(std::uint64_t v) : value(v) {}
};

constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

void test_basic_udl() {
    constexpr auto delay = 500_ms;
    static_assert(delay.value == 500);
    std::cout << "Basic UDL: " << delay.value << " ms" << std::endl;
}

// ===== 频率 UDL（整型和浮点型重载）=====
struct Frequency {
    std::uint32_t hz;
    constexpr explicit Frequency(std::uint32_t v) : hz(v) {}
};

constexpr Frequency operator""_Hz(unsigned long long value) {
    return Frequency{static_cast<std::uint32_t>(value)};
}

constexpr Frequency operator""_kHz(long double value) {
    return Frequency{static_cast<std::uint32_t>(value * 1000.0)};
}

void test_frequency_overload() {
    auto f1 = 100_Hz;     // 整型版本
    auto f2 = 1.5_kHz;    // 浮点版本
    assert(f1.hz == 100);
    assert(f2.hz == 1500);
    std::cout << "Frequency overload: " << f2.hz << " Hz" << std::endl;
}

// ===== 字符串哈希 UDL =====
constexpr std::uint32_t hash_string(
    const char* str, std::uint32_t value = 2166136261u) {
    return *str
        ? hash_string(str + 1,
            (value ^ static_cast<std::uint32_t>(*str)) * 16777619u)
        : value;
}

constexpr std::uint32_t operator""_hash(const char* str, std::size_t) {
    return hash_string(str);
}

void test_string_hash() {
    constexpr auto id1 = "temperature"_hash;
    constexpr auto id2 = "humidity"_hash;
    static_assert(id1 != id2);
    std::cout << "String hash working: " << id1 << " vs " << id2 << std::endl;
}

// ===== 标准 chrono 字面量 =====
void test_standard_chrono() {
    using namespace std::chrono_literals;

    auto t1 = 1s;
    auto t2 = 500ms;
    auto total = 1s + 500ms;

    assert(total == 1500ms);
    std::cout << "Chrono literals: 1s + 500ms = "
              << total.count() << "ms" << std::endl;
}

// ===== 标准 string 字面量 =====
void test_standard_string() {
    using namespace std::string_literals;

    auto s1 = "hello"s;
    assert(s1.length() == 5);
    std::cout << "String literal: " << s1 << std::endl;
}

// ===== 整数溢出测试 =====
struct Bytes {
    std::uint64_t value;
};

constexpr Bytes operator""_KiB(unsigned long long v) {
    return Bytes{v * 1024};
}

void test_overflow_detection() {
    // 正常情况
    constexpr auto normal = 4_KiB;
    static_assert(normal.value == 4096);

    // 溢出情况（注意：这是未定义行为！）
    constexpr auto overflow = operator""_KiB(18446744073709552ULL);
    std::cout << "Overflow example (UB): " << overflow.value << std::endl;
    std::cout << "  Expected: 18446744073709552 * 1024" << std::endl;
    std::cout << "  Got: " << overflow.value << " (wrong!)" << std::endl;
}

int main() {
    std::cout << "=== UDL Basics Verification ===" << std::endl;

    test_basic_udl();
    test_frequency_overload();
    test_string_hash();
    test_standard_chrono();
    test_standard_string();
    test_overflow_detection();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
