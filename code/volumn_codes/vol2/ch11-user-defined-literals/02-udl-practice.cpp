/**
 * UDL 实战：类型安全的单位系统
 * 验证物理单位系统、温度转换和嵌入式应用
 */

#include <cstdint>
#include <iostream>
#include <cassert>
#include <type_traits>

// ===== 单位标签系统 =====
struct MeterTag {};
struct SecondTag {};
struct SpeedTag {};
struct TemperatureTag {};

// ===== 带单位的值模板 =====
template <typename T, typename UnitTag>
struct Quantity {
    T value;
    constexpr explicit Quantity(T v) : value(v) {}

    constexpr Quantity operator+(Quantity other) const {
        return Quantity{value + other.value};
    }

    constexpr Quantity operator-(Quantity other) const {
        return Quantity{value - other.value};
    }

    constexpr Quantity operator*(T scalar) const {
        return Quantity{value * scalar};
    }

    constexpr Quantity operator/(T scalar) const {
        return Quantity{value / scalar};
    }

    constexpr bool operator==(Quantity other) const {
        return value == other.value;
    }

    constexpr bool operator<(Quantity other) const {
        return value < other.value;
    }
};

// 标量 × 单位（反向乘法）
template <typename T, typename UnitTag>
constexpr Quantity<T, UnitTag> operator*(
    T scalar, Quantity<T, UnitTag> q) {
    return q * scalar;
}

// 支持整数标量 × long double Quantity
template <typename UnitTag>
constexpr Quantity<long double, UnitTag> operator*(
    int scalar, Quantity<long double, UnitTag> q) {
    return Quantity<long double, UnitTag>{q.value * scalar};
}

// ===== 长度单位 =====
using Length = Quantity<long double, MeterTag>;

constexpr Length operator""_m(long double v) {
    return Length{v};
}

constexpr Length operator""_km(long double v) {
    return Length{v * 1000.0L};
}

constexpr Length operator""_cm(long double v) {
    return Length{v / 100.0L};
}

constexpr Length operator""_mm(long double v) {
    return Length{v / 1000.0L};
}

// 整数版本
constexpr Length operator""_m(unsigned long long v) {
    return Length{static_cast<long double>(v)};
}

constexpr Length operator""_km(unsigned long long v) {
    return Length{static_cast<long double>(v) * 1000.0L};
}

void test_length() {
    std::cout << "=== Length System ===" << std::endl;

    constexpr auto d1 = 1.5_m;
    constexpr auto d2 = 2.0_km;       // 注意：必须是 2.0_km
    constexpr auto d3 = 100.0_cm;
    constexpr auto d4 = 500.0_mm;

    // 编译期计算
    constexpr auto total = 1.0_km + 500.0_m;
    static_assert(total.value == 1500.0L);
    std::cout << "1.0_km + 500.0_m = " << total.value << " m" << std::endl;

    // 标量乘法（支持整数）
    constexpr auto doubled = 2 * 100.0_m;
    static_assert(doubled.value == 200.0L);
    std::cout << "2 * 100.0_m = " << doubled.value << " m" << std::endl;
}

// ===== 时间与速度单位 =====
using TimeDuration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

constexpr TimeDuration operator""_s(long double v) {
    return TimeDuration{v};
}

constexpr TimeDuration operator""_ms(long double v) {
    return TimeDuration{v / 1000.0L};
}

constexpr TimeDuration operator""_min(long double v) {
    return TimeDuration{v * 60.0L};
}

constexpr TimeDuration operator""_h(long double v) {
    return TimeDuration{v * 3600.0L};
}

// 长度 / 时间 = 速度
constexpr Speed operator/(Length len, TimeDuration time) {
    return Speed{len.value / time.value};
}

// 速度 * 时间 = 长度
constexpr Length operator*(Speed spd, TimeDuration time) {
    return Length{spd.value * time.value};
}

constexpr Length operator*(TimeDuration time, Speed spd) {
    return Length{spd.value * time.value};
}

void test_physics() {
    std::cout << "\n=== Physics Calculations ===" << std::endl;

    // 速度 = 距离 / 时间
    constexpr auto speed = 100.0_m / 10.0_s;
    static_assert(speed.value == 10.0L);
    std::cout << "100.0_m / 10.0_s = " << speed.value << " m/s" << std::endl;

    // 距离 = 速度 * 时间
    constexpr auto distance = speed * 60.0_s;
    static_assert(distance.value == 600.0L);
    std::cout << "10 m/s * 60.0_s = " << distance.value << " m" << std::endl;

    // 换算：36 km/h = 10 m/s
    constexpr auto v1 = 36.0_km / 1.0_h;
    static_assert(v1.value == 10.0L);
    std::cout << "36.0_km / 1.0_h = " << v1.value << " m/s" << std::endl;
}

// ===== 温度单位 =====
using Temperature = Quantity<long double, TemperatureTag>;

constexpr Temperature operator""_degC(long double v) {
    return Temperature{v + 273.15L};  // 转换为开尔文
}

constexpr Temperature operator""_degF(long double v) {
    return Temperature{(v - 32.0L) * 5.0L / 9.0L + 273.15L};
}

constexpr Temperature operator""_degK(long double v) {
    return Temperature{v};
}

constexpr long double to_celsius(Temperature t) {
    return t.value - 273.15L;
}

constexpr long double to_fahrenheit(Temperature t) {
    return (t.value - 273.15L) * 9.0L / 5.0L + 32.0L;
}

constexpr long double to_kelvin(Temperature t) {
    return t.value;
}

void test_temperature() {
    std::cout << "\n=== Temperature System ===" << std::endl;

    constexpr auto t1 = 0.0_degC;    // 冰点：273.15 K
    constexpr auto t2 = 100.0_degC;  // 沸点：373.15 K
    constexpr auto t3 = 32.0_degF;   // 冰点（华氏）：273.15 K

    static_assert(to_kelvin(t1) == 273.15L);
    static_assert(to_celsius(t1) == 0.0L);
    static_assert(to_celsius(t3) == 0.0L);

    std::cout << "0°C = " << to_kelvin(t1) << " K" << std::endl;
    std::cout << "100°C = " << to_kelvin(t2) << " K" << std::endl;
    std::cout << "32°F = " << to_celsius(t3) << " °C" << std::endl;

    // 温度差（在开尔文空间中）
    constexpr auto delta = 10.0_degC - 0.0_degC;
    static_assert(delta.value == 10.0L);
    std::cout << "10°C - 0°C = " << delta.value << " K difference" << std::endl;

    // 体温度转换
    constexpr auto body_temp = 37.0_degC;
    constexpr auto fahrenheit = to_fahrenheit(body_temp);
    std::cout << "37°C = " << fahrenheit << " °F" << std::endl;
}

// ===== 嵌入式应用：频率与波特率 =====
struct Frequency {
    std::uint32_t hz;

    constexpr std::uint32_t to_hz() const { return hz; }
    constexpr std::uint32_t to_khz() const { return hz / 1000; }

    constexpr std::uint64_t period_ns() const {
        return 1000000000ULL / hz;
    }
};

constexpr Frequency operator""_Hz(unsigned long long v) {
    return Frequency{static_cast<std::uint32_t>(v)};
}

constexpr Frequency operator""_kHz(long double v) {
    return Frequency{static_cast<std::uint32_t>(v * 1000.0)};
}

constexpr Frequency operator""_MHz(long double v) {
    return Frequency{static_cast<std::uint32_t>(v * 1000000.0)};
}

constexpr std::uint16_t compute_brr(Frequency periph_clock, Frequency baud) {
    return static_cast<std::uint16_t>(periph_clock.to_hz() / baud.to_hz());
}

void test_embedded_frequency() {
    std::cout << "\n=== Embedded: Frequency & Baud Rate ===" << std::endl;

    constexpr auto sysclk = 72.0_MHz;  // 注意：必须是浮点字面量
    constexpr auto baud = 115200_Hz;

    constexpr auto brr = compute_brr(sysclk, baud);
    static_assert(brr == 625);
    std::cout << "STM32 BRR for 72MHz / 115200 baud = " << brr << std::endl;

    // 周期计算
    constexpr auto freq = 1.0_MHz;
    constexpr auto period = freq.period_ns();
    static_assert(period == 1000);
    std::cout << "1 MHz period = " << period << " ns" << std::endl;
}

// ===== 嵌入式应用：内存大小 =====
struct Bytes {
    std::uint64_t value;
    constexpr std::uint64_t to_bytes() const { return value; }
};

constexpr Bytes operator""_KiB(unsigned long long v) {
    return Bytes{v * 1024};
}

constexpr Bytes operator""_MiB(unsigned long long v) {
    return Bytes{v * 1024 * 1024};
}

void test_embedded_memory() {
    std::cout << "\n=== Embedded: Memory Layout ===" << std::endl;

    constexpr auto kFlashSize = 512_KiB;
    constexpr auto kAppSize = 256_KiB;
    constexpr auto kStackSize = 4_KiB;
    constexpr auto kRamSize = 128_KiB;

    static_assert(kAppSize.to_bytes() <= kFlashSize.to_bytes());
    static_assert(kStackSize.to_bytes() < kRamSize.to_bytes());

    std::cout << "Flash: " << kFlashSize.to_bytes() << " bytes" << std::endl;
    std::cout << "App: " << kAppSize.to_bytes() << " bytes" << std::endl;
    std::cout << "Stack: " << kStackSize.to_bytes() << " bytes" << std::endl;
    std::cout << "RAM: " << kRamSize.to_bytes() << " bytes" << std::endl;
    std::cout << "Resource check: PASSED" << std::endl;
}

// ===== 字符串哈希字面量 =====
constexpr std::uint32_t operator""_hash(const char* str, std::size_t len) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) {
        hash = (hash ^ static_cast<std::uint8_t>(str[i])) * 16777619u;
    }
    return hash;
}

void test_string_hash_udl() {
    std::cout << "\n=== String Hash for Switch-Case ===" << std::endl;

    constexpr auto cmd = "start"_hash;

    switch (cmd) {
        case "start"_hash:
            std::cout << "Command: START" << std::endl;
            break;
        case "stop"_hash:
            std::cout << "Command: STOP" << std::endl;
            break;
        default:
            std::cout << "Unknown command" << std::endl;
    }
}

int main() {
    std::cout << "=== UDL Practice Verification ===" << std::endl;

    test_length();
    test_physics();
    test_temperature();
    test_embedded_frequency();
    test_embedded_memory();
    test_string_hash_udl();

    std::cout << "\n✓ All tests passed!" << std::endl;
    return 0;
}
