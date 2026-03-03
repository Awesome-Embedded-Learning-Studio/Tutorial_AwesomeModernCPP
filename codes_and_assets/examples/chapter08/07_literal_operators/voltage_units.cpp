// voltage_units.cpp
// 浮点字面量运算符示例（电压、长度等）

#include <cstdint>
#include <iostream>
#include <cmath>

// ===== 浮点单位 =====

struct Voltage {
    float value;
    constexpr explicit Voltage(float v) : value(v) {}

    // 运算符
    constexpr Voltage operator+(Voltage other) const {
        return Voltage{value + other.value};
    }
    constexpr Voltage operator-(Voltage other) const {
        return Voltage{value - other.value};
    }
};

struct Length {
    double value;
    constexpr explicit Length(double v) : value(v) {}

    constexpr Length operator+(Length other) const {
        return Length{value + other.value};
    }
    constexpr bool operator<(Length other) const {
        return value < other.value;
    }
};

// 浮点字面量运算符（参数必须是 long double）
constexpr Voltage operator""_V(long double v) {
    return Voltage{static_cast<float>(v)};
}

constexpr Length operator""_mm(long double v) {
    return Length{static_cast<double>(v)};
}

constexpr Length operator""_cm(long double v) {
    return Length{static_cast<double>(v) * 10.0};
}

constexpr Length operator""_m(long double v) {
    return Length{static_cast<double>(v) * 1000.0};
}

// 使用函数
void set_voltage(Voltage v);
void measure(Length l);

void set_voltage(Voltage v) {
    std::cout << "Voltage set to: " << v.value << " V\n";
}

void measure(Length l) {
    std::cout << "Length: " << l.value << " mm\n";
}

int main() {
    std::cout << "=== Floating-Point Literal Operators ===\n\n";

    // 电压单位
    set_voltage(3.3_V);
    set_voltage(5.0_V);
    set_voltage(1.2_V);

    // 长度单位
    std::cout << "\n=== Length Units ===\n";
    measure(1.5_mm);
    measure(2.5_cm);
    measure(0.01_m);

    // 单位运算
    std::cout << "\n=== Unit Operations ===\n";
    Length total = 1.5_mm + 2.5_mm;
    std::cout << "1.5_mm + 2.5_mm = " << total.value << " mm\n";

    Length l1 = 5.0_cm;
    Length l2 = 50.0_mm;
    std::cout << "5.0_cm < 50.0_mm: " << (l1 < l2 ? "true" : "false") << "\n";

    // 自文档化代码演示
    std::cout << "\n=== Self-Documenting Code ===\n";
    std::cout << "Compare:\n";
    std::cout << "  voltage = 3.3           <- What unit?\n";
    std::cout << "  voltage = 3.3_V          <- Clear: 3.3 Volts\n";
    std::cout << "  thickness = 1.5          <- What unit?\n";
    std::cout << "  thickness = 1.5_mm       <- Clear: 1.5 millimeters\n";

    return 0;
}
