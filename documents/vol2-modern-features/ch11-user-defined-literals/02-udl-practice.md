---
title: "UDL 实战：类型安全的单位系统"
description: "用用户自定义字面量实现类型安全的物理单位系统"
chapter: 11
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 字面量
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [14, 17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 11: 用户自定义字面量基础"
  - "Chapter 4: 强类型 typedef"
related:
  - "constexpr 基础"
---

# UDL 实战：类型安全的单位系统

上一篇我们学了用户自定义字面量的基础语法——`operator""` 的各种形式、标准库字面量、命名规则。这一篇我们要把这些知识用起来，构建一个真正实用的**类型安全单位系统**。

我们的目标是：让 `100_m + 500_m` 返回一个长度，让 `100_m / 2_s` 返回一个速度，让 `100_m + 50_s` 直接编译报错。所有转换在编译期完成，运行时零开销。

------

## 第一步：长度单位系统

先从最简单的长度单位开始。我们用模板来定义一个通用的"带单位的值"，然后为不同的长度单位定义字面量：

```cpp
#include <cstdint>
#include <type_traits>

/// 单位标签：用于区分不同类型的物理量
struct MeterTag {};
struct SecondTag {};

/// 带单位的值
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

/// 标量 × 单位（反向乘法）
/// 注意：这个模板要求标量类型 T 必须与 Quantity 的 T 完全匹配
/// 如果需要支持类型转换，需要提供额外的重载
template <typename T, typename UnitTag>
constexpr Quantity<T, UnitTag> operator*(
    T scalar, Quantity<T, UnitTag> q) {
    return q * scalar;
}

/// 支持整数标量 × long double Quantity 的重载
template <typename UnitTag>
constexpr Quantity<long double, UnitTag> operator*(
    int scalar, Quantity<long double, UnitTag> q) {
    return Quantity<long double, UnitTag>{q.value * scalar};
}
```

`Quantity<T, UnitTag>` 是一个模板，`UnitTag` 是一个空的标签类型，唯一的作用是让不同单位的物理量成为不同的类型。`MeterTag` 和 `SecondTag` 之间没有任何继承关系，所以 `Quantity<double, MeterTag>` 和 `Quantity<double, SecondTag>` 是完全不同的类型——你不可能把一个赋给另一个。

现在定义长度类型别名和字面量：

```cpp
using Length = Quantity<long double, MeterTag>;

// 字面量：以米为基准单位
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
```

来测试一下：

```cpp
void test_length() {
    constexpr auto d1 = 1.5_m;       // 1.5 米
    constexpr auto d2 = 2.0_km;      // 2000 米（注意：2_km 会失败，因为只定义了浮点重载）
    constexpr auto d3 = 100.0_cm;    // 1 米
    constexpr auto d4 = 500.0_mm;    // 0.5 米

    // 编译期计算
    constexpr auto total = 1.0_km + 500.0_m;  // 1500 米
    static_assert(total.value == 1500.0L);

    // 标量乘法（现在支持整数了）
    constexpr auto doubled = 2 * 100.0_m;  // 200 米
    static_assert(doubled.value == 200.0L);

    // 类型安全：不能把长度和时间相加
    // auto bad = 100_m + 50_s;  // 编译错误！
}
```

`1.0_km + 500.0_m` 在编译期就被计算为 `1500.0_m`。如果你试图把长度和时间相加，编译器会直接报错——因为 `Quantity<long double, MeterTag>` 和 `Quantity<long double, SecondTag>` 是不同的类型。

------

## 第二步：时间与速度单位

长度系统可以独立运作，但物理计算的魅力在于不同单位的组合。长度除以时间得到速度——我们需要让 `Quantity` 支持这种跨单位的运算：

```cpp
/// 速度标签
struct SpeedTag {};

using TimeDuration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

// 时间字面量（以秒为基准）
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

// 整数版本
constexpr TimeDuration operator""_s(unsigned long long v) {
    return TimeDuration{static_cast<long double>(v)};
}

constexpr TimeDuration operator""_ms(unsigned long long v) {
    return TimeDuration{static_cast<long double>(v) / 1000.0L};
}

/// 长度 / 时间 = 速度
constexpr Speed operator/(Length len, TimeDuration time) {
    return Speed{len.value / time.value};
}

/// 速度 * 时间 = 长度
constexpr Length operator*(Speed spd, TimeDuration time) {
    return Length{spd.value * time.value};
}

constexpr Length operator*(TimeDuration time, Speed spd) {
    return Length{spd.value * time.value};
}
```

现在可以做物理计算了：

```cpp
void test_physics() {
    // 速度 = 距离 / 时间
    constexpr auto speed = 100.0_m / 10.0_s;   // 10 m/s
    static_assert(speed.value == 10.0L);

    // 距离 = 速度 * 时间
    constexpr auto distance = speed * 60.0_s;   // 600 米
    static_assert(distance.value == 600.0L);

    // 换算：36 km/h = 10 m/s
    constexpr auto v1 = 36.0_km / 1.0_h;       // 36000 / 3600 = 10 m/s
    static_assert(v1.value == 10.0L);

    // 类型安全
    // auto bad = 100_m + 10_s;    // 编译错误：长度 + 时间
    // auto bad2 = 100_m * 10_s;   // 编译错误：长度 * 时间（未定义）
}
```

这段代码的美妙之处在于，编译器帮你做了单位检查——你不可能不小心把毫秒当成秒用，也不可能把速度和距离相加。

------

## 第三步：温度转换字面量

温度是一个特殊的物理量，因为不同温标之间不是简单的线性缩放——摄氏度和华氏度之间的转换包含偏移量。这正好是 UDL 的一个好用例：

```cpp
struct TemperatureTag {};
using Temperature = Quantity<long double, TemperatureTag>;

// 摄氏度：以开尔文为基准存储
constexpr Temperature operator""_degC(long double v) {
    return Temperature{v + 273.15L};
}

// 华氏度 -> 开尔文
constexpr Temperature operator""_degF(long double v) {
    return Temperature{(v - 32.0L) * 5.0L / 9.0L + 273.15L};
}

// 开尔文
constexpr Temperature operator""_degK(long double v) {
    return Temperature{v};
}

// 辅助函数：从开尔文转换到各温标
constexpr long double to_celsius(Temperature t) {
    return t.value - 273.15L;
}

constexpr long double to_fahrenheit(Temperature t) {
    return (t.value - 273.15L) * 9.0L / 5.0L + 32.0L;
}

constexpr long double to_kelvin(Temperature t) {
    return t.value;
}
```

使用：

```cpp
void test_temperature() {
    constexpr auto t1 = 0.0_degC;     // 冰点：273.15 K
    constexpr auto t2 = 100.0_degC;   // 沸点：373.15 K
    constexpr auto t3 = 32.0_degF;    // 冰点（华氏）：273.15 K

    static_assert(to_kelvin(t1) == 273.15L);

    // 温度差可以相减（在开尔文空间中）
    constexpr auto delta = 10.0_degC - 0.0_degC;  // 10K
    static_assert(delta.value == 10.0L);

    // 摄氏 -> 华氏
    constexpr auto body_temp = 37.0_degC;
    // to_fahrenheit(body_temp) ≈ 98.6°F
}
```

这里我们用开尔文作为内部存储，所有字面量在构造时都转换到开尔文。这样温度差就可以正确地相加减了。

------

## 第四步：字符串处理字面量

UDL 不只能用于物理单位。在通用 C++ 开发中，字符串处理字面量也很常用：

```cpp
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

/// 编译期字符串哈希——用于高效的字符串比较
constexpr std::uint32_t operator""_hash(
    const char* str, std::size_t len) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) {
        hash = (hash ^ static_cast<std::uint8_t>(str[i]))
             * 16777619u;
    }
    return hash;
}

/// 运行时转大写
std::string operator""_upper(const char* str, std::size_t len) {
    std::string result(str, len);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

/// 运行时 trim 空白
std::string operator""_trim(const char* str, std::size_t len) {
    std::string_view sv(str, len);
    while (!sv.empty() && std::isspace(sv.front())) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(sv.back())) sv.remove_suffix(1);
    return std::string(sv);
}

void test_string_literals() {
    constexpr auto id = "sensor_temp"_hash;   // 编译期整数
    auto upper = "hello world"_upper;          // "HELLO WORLD"
    auto trimmed = "  padded  "_trim;           // "padded"

    // 用于 switch-case（比字符串比较高效）
    constexpr auto cmd = "start"_hash;
    switch (cmd) {
        case "start"_hash:  /* 启动 */ break;
        case "stop"_hash:   /* 停止 */ break;
        default: break;
    }
}
```

字符串哈希字面量在嵌入式场景中特别有用——你可以用编译期生成的整数代替运行时字符串比较，既节省 Flash（不需要存储字符串），又提升性能（整数比较 vs 字符串比较）。

------

## 嵌入式实战

在嵌入式开发中，UDL 最实用的场景是频率/波特率字面量和寄存器地址字面量。来看具体例子。

### 频率与波特率

```cpp
#include <cstdint>

struct Frequency {
    std::uint32_t hz;

    constexpr std::uint32_t to_hz() const { return hz; }
    constexpr std::uint32_t to_khz() const { return hz / 1000; }

    /// 频率转周期（纳秒）
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

/// 波特率寄存器计算（STM32 USART）
constexpr std::uint16_t compute_brr(
    Frequency periph_clock, Frequency baud) {
    return static_cast<std::uint16_t>(
        periph_clock.to_hz() / baud.to_hz());
}

void configure_uart() {
    constexpr auto sysclk = 72.0_MHz;  // 注意：必须用浮点字面量
    constexpr auto baud = 115200_Hz;

    // USART1->BRR = compute_brr(sysclk, baud);
    // 生成的代码等价于直接写 USART1->BRR = 625;

    constexpr auto brr = compute_brr(sysclk, baud);
    static_assert(brr == 625, "BRR calculation mismatch");
}
```

### 内存大小与静态断言

```cpp
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

// 编译期资源检查
constexpr auto kFlashSize = 512_KiB;
constexpr auto kAppSize = 256_KiB;
constexpr auto kStackSize = 4_KiB;
constexpr auto kRamSize = 128_KiB;

static_assert(kAppSize.to_bytes() <= kFlashSize.to_bytes(),
    "Application too large for flash!");
static_assert(kStackSize.to_bytes() < kRamSize.to_bytes(),
    "Stack exceeds RAM!");
```

这些 `static_assert` 在编译期就能发现资源分配的问题，而不是等到运行时才发现 RAM 不够用。

### 寄存器地址字面量

在嵌入式裸机开发中，寄存器操作非常频繁。虽然通常用 CMSIS 提供的宏来访问寄存器，但如果你需要自定义外设或者调试时快速查看地址，一个地址字面量可以增加可读性：

```cpp
struct RegisterAddress {
    std::uintptr_t addr;
};

constexpr RegisterAddress operator""_reg(unsigned long long v) {
    return RegisterAddress{static_cast<std::uintptr_t>(v)};
}

// 使用
void debug_example() {
    // STM32F103 USART1 基地址 = 0x40013800
    constexpr auto usart1_base = 0x40013800_reg;
    constexpr auto gpioa_base = 0x40010800_reg;

    // volatile auto* usart1_sr =
    //     reinterpret_cast<volatile std::uint32_t*>(usart1_base.addr);
}
```

------

## 练习：实现一个长度单位系统

作为这一篇的练习，尝试自己实现一个完整的长度单位系统，包含以下功能：

1. 定义 `_m`、`_km`、`_mi`（英里）三个字面量，以米为基准单位
2. 支持加减运算和标量乘法
3. 支持长度除以时间得到速度
4. 用 `static_assert` 验证编译期计算的正确性

参考框架：

```cpp
#include <cstdint>

struct MeterTag {};
struct SecondTag {};
struct SpeedTag {};

template <typename T, typename Tag>
struct Quantity {
    T value;
    constexpr explicit Quantity(T v) : value(v) {}

    // TODO: 实现加、减、标量乘法、比较运算
};

using Length = Quantity<long double, MeterTag>;
using Duration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

// TODO: 定义 _m, _km, _mi 字面量
// TODO: 定义 _s 字面量
// TODO: 实现 Length / Duration -> Speed

// 验证
void test() {
    constexpr auto marathon = 26.2_mi;     // 英里转米
    // constexpr auto pace = marathon / 4.0_h;  // 配速（米/小时）
    // 注意：需要先定义 _h 字面量才能使用

    // 提示：1 英里 = 1609.344 米
    static_assert(marathon.value > 42000.0);
}
```

这个练习会帮你巩固模板、运算符重载、`constexpr` 和 UDL 的组合使用。完成之后，你就有了一个可以直接用在项目中的轻量级单位系统。

------

## 小结

这一篇我们把 UDL 的基础知识用到了实处。通过 `Quantity<T, UnitTag>` 模板 + 运算符重载 + 字面量运算符的组合，我们构建了一个类型安全的物理单位系统：长度可以和长度相加，长度除以时间得到速度，但长度和时间不能直接相加——所有这些检查都在编译期完成，运行时零开销。

嵌入式场景下，UDL 特别适合用于频率/波特率字面量（`72_MHz`、`115200_Hz`）、内存大小字面量（`4_KiB`、`512_KiB`）和寄存器地址字面量。这些字面量让裸机代码的可读性大幅提升，配合 `static_assert` 还能在编译期发现资源分配错误。

到这里，ch11 用户自定义字面量就讲完了。UDL 是一个精巧但实用的语言特性——它的语法并不复杂，但在正确的场景下使用，可以让代码的清晰度和安全性产生质的飞跃。

## 参考资源

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [Bjarne Stroustrup: The C++ Programming Language, Chapter 18.6](https://www.stroustrup.com/)
