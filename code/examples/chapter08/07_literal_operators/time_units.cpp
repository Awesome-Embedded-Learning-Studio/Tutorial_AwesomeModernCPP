// time_units.cpp
// 时间单位字面量运算符示例

#include <cstdint>
#include <iostream>

// ===== 时间单位 =====

struct Milliseconds {
    std::uint64_t value;
    constexpr explicit Milliseconds(std::uint64_t v) : value(v) {}

    // 运算符重载
    constexpr Milliseconds operator+(Milliseconds other) const {
        return Milliseconds{value + other.value};
    }
    constexpr Milliseconds operator-(Milliseconds other) const {
        return Milliseconds{value - other.value};
    }
    constexpr Milliseconds operator*(std::uint64_t factor) const {
        return Milliseconds{value * factor};
    }
    constexpr bool operator==(Milliseconds other) const {
        return value == other.value;
    }
    constexpr bool operator<(Milliseconds other) const {
        return value < other.value;
    }
};

// 标量 × 单位（反向乘法）
constexpr Milliseconds operator*(std::uint64_t factor, Milliseconds ms) {
    return ms * factor;
}

struct Microseconds {
    std::uint64_t value;
    constexpr explicit Microseconds(std::uint64_t v) : value(v) {}

    // 转换到毫秒
    constexpr Milliseconds to_milliseconds() const {
        return Milliseconds{value / 1000};
    }
};

struct Seconds {
    std::uint64_t value;
    constexpr explicit Seconds(std::uint64_t v) : value(v) {}

    constexpr Milliseconds to_milliseconds() const {
        return Milliseconds{value * 1000};
    }

    constexpr Microseconds to_microseconds() const {
        return Microseconds{value * 1000000};
    }
};

// 字面量运算符
constexpr Milliseconds operator""_ms(unsigned long long v) { return Milliseconds{v}; }
constexpr Microseconds operator""_us(unsigned long long v) { return Microseconds{v}; }
constexpr Seconds        operator""_s (unsigned long long v) { return Seconds{v}; }

// 延时函数（声明）
void delay(Milliseconds ms);
void delay_us(Microseconds us);

// 实现模拟
void delay(Milliseconds ms) {
    std::cout << "Delay: " << ms.value << " ms\n";
}
void delay_us(Microseconds us) {
    std::cout << "Delay: " << us.value << " us\n";
}

int main() {
    std::cout << "=== Time Units ===\n\n";

    // 基本使用
    delay(500_ms);
    delay_us(1500_us);

    // 单位运算
    std::cout << "\n=== Unit Operations ===\n";
    Milliseconds total = 100_ms + 250_ms;
    std::cout << "100_ms + 250_ms = " << total.value << " ms\n";

    Milliseconds double_time = 2 * 100_ms;
    std::cout << "2 * 100_ms = " << double_time.value << " ms\n";

    Milliseconds triple = 100_ms * 3;
    std::cout << "100_ms * 3 = " << triple.value << " ms\n";

    // 单位转换
    std::cout << "\n=== Unit Conversion ===\n";
    Seconds s = 2_s;
    Milliseconds ms = s.to_milliseconds();
    std::cout << "2_s = " << ms.value << " ms\n";

    Microseconds us = s.to_microseconds();
    std::cout << "2_s = " << us.value << " us\n";

    Microseconds us2 = 1500_us;
    Milliseconds ms2 = us2.to_milliseconds();
    std::cout << "1500_us = " << ms2.value << " ms\n";

    // 类型安全演示
    std::cout << "\n=== Type Safety ===\n";
    // delay(500);        // 编译错误！必须明确单位
    // delay(500_s);      // 编译错误！类型不匹配

    return 0;
}
