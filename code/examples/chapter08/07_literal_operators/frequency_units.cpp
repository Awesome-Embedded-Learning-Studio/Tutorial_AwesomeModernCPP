// frequency_units.cpp
// 频率和波特率单位字面量运算符示例

#include <cstdint>
#include <iostream>

// ===== 频率单位 =====

struct Hertz {
    std::uint32_t value;
    constexpr explicit Hertz(std::uint32_t v) : value(v) {}
};

struct KiloHertz {
    std::uint32_t value;
    constexpr explicit KiloHertz(std::uint32_t v) : value(v) {}

    constexpr Hertz to_hertz() const {
        return Hertz{value * 1000};
    }
};

struct MegaHertz {
    std::uint32_t value;
    constexpr explicit MegaHertz(std::uint32_t v) : value(v) {}

    constexpr Hertz to_hertz() const {
        return Hertz{value * 1000000};
    }
};

// 频率字面量运算符
constexpr Hertz     operator""_Hz (unsigned long long v) { return Hertz{static_cast<std::uint32_t>(v)}; }
constexpr KiloHertz operator""_kHz(unsigned long long v) { return KiloHertz{static_cast<std::uint32_t>(v)}; }
constexpr MegaHertz operator""_MHz(unsigned long long v) { return MegaHertz{static_cast<std::uint32_t>(v)}; }

// ===== 波特率单位 =====

struct BaudRate {
    std::uint32_t value;
    constexpr explicit BaudRate(std::uint32_t v) : value(v) {}
};

constexpr BaudRate operator""_baud(unsigned long long v) {
    return BaudRate{static_cast<std::uint32_t>(v)};
}

// 使用示例函数
void set_clock_frequency(Hertz freq);
void set_uart_baudrate(BaudRate baud);

void set_clock_frequency(Hertz freq) {
    std::cout << "Clock frequency: " << freq.value << " Hz\n";
}

void set_uart_baudrate(BaudRate baud) {
    std::cout << "UART baudrate: " << baud.value << " baud\n";
}

int main() {
    std::cout << "=== Frequency and Baud Rate Units ===\n\n";

    // 频率单位
    Hertz sysclk = 72_MHz.to_hertz();
    std::cout << "System clock: " << sysclk.value << " Hz\n";

    set_clock_frequency(48_MHz.to_hertz());
    set_clock_frequency(16_kHz.to_hertz());
    set_clock_frequency(1000_Hz);

    // 波特率
    std::cout << "\n=== UART Configuration ===\n";
    set_uart_baudrate(115200_baud);
    set_uart_baudrate(9600_baud);
    set_uart_baudrate(1000000_baud);

    // 演示自文档化代码
    std::cout << "\n=== Self-Documenting Code ===\n";
    std::cout << "Compare:\n";
    std::cout << "  delay(5000)      <- What unit?\n";
    std::cout << "  delay(5000_ms)   <- Clear: 5000 milliseconds\n";
    std::cout << "  set_clk(72000000)        <- What frequency?\n";
    std::cout << "  set_clk(72_MHz.to_hertz()) <- Clear: 72 MHz\n";

    return 0;
}
