---
title: "用户自定义字面量基础"
description: "operator\"\" 的原始/cooked 形式与标准库字面量"
chapter: 11
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 字面量
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 2: constexpr 基础"
related:
  - "UDL 实战"
---

# 用户自定义字面量基础

笔者在写嵌入式代码的时候，经常遇到这种让人难受的场景：`TIM1->ARR = (1000 - 1)` 里面的 1000 是毫秒还是微秒？`USART1->BRR = 0x271` 到底是 9600 还是 115200？`#define BUFFER_SIZE 1024` 是字节还是字？这些"魔数"不仅难以理解，还容易出错——更糟糕的是，不同单位之间的转换完全依赖程序员手动计算，稍有不慎就会出问题。

C++11 引入的**用户自定义字面量（User-Defined Literals，UDL）**就是为了解决这个问题。它允许我们定义自己的字面量后缀，比如 `100_ms`、`72_MHz`、`4_KiB`，让代码更直观、更安全，而且所有转换都可以在编译期完成，零运行时开销。

------

## operator"" 的四种形式

用户自定义字面量通过 `operator""` 后缀运算符来定义。根据参数类型的不同，有几种主要的定义形式，分别对应整数字面量、浮点数字面量、字符串字面量和字符字面量：

```cpp
// 整数字面量（cooked 形式）
ReturnType operator""_suffix(unsigned long long value);

// 浮点数字面量（cooked 形式）
ReturnType operator""_suffix(long double value);

// 字符串字面量（raw 形式）
ReturnType operator""_suffix(const char* str, size_t length);

// 字符字面量（cooked 形式）
ReturnType operator""_suffix(char c);
```

这里有两对概念需要区分：**cooked** 和 **raw**。Cooked 字面量是指编译器已经解析并转换后的字面量——对于整型和浮点型，编译器会先把它们解析成数值类型再传给 `operator""`。Raw 字面量则接收原始的字符序列，编译器不做任何解析——只有字符串字面量和整数字面量支持 raw 形式（整数的 raw 形式参数为 `const char*`）。

先从一个最简单的例子开始：

```cpp
#include <cstdint>

struct Milliseconds {
    std::uint64_t value;
    constexpr explicit Milliseconds(std::uint64_t v) : value(v) {}
};

constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

void delay(Milliseconds ms);

void example() {
    delay(500_ms);  // 清晰：500 毫秒
    // delay(500);  // 编译错误！必须明确单位
}
```

`500_ms` 被编译器解析后，调用 `operator""_ms(500)`，返回一个 `Milliseconds` 对象。函数签名 `delay(Milliseconds)` 只接受带单位的参数——裸整数传不进去，编译器会直接报错。这就是类型安全的来源。

### 整型与浮点型重载

你可以为整型和浮点型分别定义重载，让同一个后缀在不同上下文中有不同的行为：

```cpp
struct Frequency {
    std::uint32_t hz;
    constexpr explicit Frequency(std::uint32_t v) : hz(v) {}
};

// 整数版本：100_Hz
constexpr Frequency operator""_Hz(unsigned long long value) {
    return Frequency{static_cast<std::uint32_t>(value)};
}

// 浮点版本：1.5_kHz
constexpr Frequency operator""_kHz(long double value) {
    return Frequency{static_cast<std::uint32_t>(value * 1000.0)};
}

void example() {
    auto f1 = 100_Hz;    // 整型版本，f1.hz = 100
    auto f2 = 1.5_kHz;   // 浮点版本，f2.hz = 1500
}
```

### 字符串字面量

字符串字面量运算符接收一个指向字符串的指针和长度，可以用于编译期字符串处理：

```cpp
#include <cstdint>

/// FNV-1a 哈希（编译期）
constexpr std::uint32_t hash_string(
    const char* str, std::uint32_t value = 2166136261u) {
    return *str
        ? hash_string(str + 1,
            (value ^ static_cast<std::uint32_t>(*str)) * 16777619u)
        : value;
}

constexpr std::uint32_t operator""_hash(
    const char* str, std::size_t len) {
    return hash_string(str);
}

void example() {
    constexpr auto id1 = "temperature"_hash;
    constexpr auto id2 = "humidity"_hash;
    static_assert(id1 != id2);
}
```

这在嵌入式里可以用于实现高效的事件 ID、消息类型标识符等——字符串在编译期被转换为整数，运行时零开销。

### raw 整数字面量

整数字面量还有一种 raw 形式，接收 `const char*`，让你可以处理编译器原生不支持的格式：

```cpp
#include <cstdint>

struct Binary {
    std::uint64_t value;
};

constexpr Binary operator""_bin(const char* str, std::size_t length) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        value = value * 2;
        if (str[i] == '1') value += 1;
    }
    return Binary{value};
}

void example() {
    auto b1 = 1010_bin;       // 10
    auto b2 = 11111111_bin;   // 255
}
```

这种 raw 形式在 C++14 之前非常有用——因为 C++14 才引入了 `0b1010` 二进制字面量。现在虽然标准已经支持了，但 raw 形式依然可以用来实现自定义的进制转换。

------

## 标准库字面量

C++14 在标准库中引入了一批常用的字面量后缀，使用时需要通过 `using namespace` 引入对应的命名空间。这些后缀不带下划线前缀——因为它们在 `std::literals` 命名空间内，属于标准库保留的字面量。

### chrono 字面量（C++14）

```cpp
#include <chrono>

using namespace std::chrono_literals;

void example() {
    auto t1 = 1s;         // std::chrono::seconds{1}
    auto t2 = 500ms;      // std::chrono::milliseconds{500}
    auto t3 = 2us;        // std::chrono::microseconds{2}
    auto t4 = 100ns;      // std::chrono::nanoseconds{100}
    auto t5 = 1min;       // std::chrono::minutes{1}
    auto t6 = 1h;         // std::chrono::hours{1}

    auto total = 1s + 500ms;  // 1500ms
}
```

### string 字面量（C++14）

```cpp
#include <string>

using namespace std::string_literals;

void example() {
    auto s1 = "hello"s;    // std::string
    auto s2 = L"wide"s;    // std::wstring
    auto s3 = u"utf16"s;   // std::u16string
    auto s4 = U"utf32"s;   // std::u32string
}
```

### complex 字面量（C++14）

```cpp
#include <complex>

using namespace std::complex_literals;

void example() {
    auto c1 = 3.0 + 4.0i;   // std::complex<double>{3.0, 4.0}
    auto c2 = 1.0i;          // 虚数单位
}
```

### string_view 字面量（C++17）

```cpp
#include <string_view>

using namespace std::string_view_literals;

void example() {
    auto sv = "hello"sv;   // std::string_view
}
```

------

## 命名规则

关于 UDL 后缀的命名，C++ 标准有明确的规定：

**不以 `_` 开头的后缀保留给标准库**。所以 `1ms`、`3.14s` 这些不需要下划线的后缀，只有标准库才能定义。用户自定义的后缀**必须以 `_` 开头**，比如 `_ms`、`_Hz`、`_V`。

另外，以 `__`（双下划线）开头或者包含 `__` 的标识符是保留给实现（编译器）的，不能使用。

推荐的命名风格是用 `_` 加上简短但清晰的后缀：`_ms`、`_us`、`_Hz`、`_kHz`、`_MHz`、`_V`、`_mV`、`_KiB`。在头文件中定义时，务必放在命名空间内，避免全局命名空间污染：

```cpp
namespace mylib::literals {
    constexpr Milliseconds operator""_ms(unsigned long long v) {
        return Milliseconds{v};
    }
}

// 使用时
using namespace mylib::literals;
auto t = 500_ms;
```

------

## 编译期 vs 运行期

UDL 配合 `constexpr` 可以实现纯编译期的单位转换，这是它最强大的特性之一。务必把字面量运算符标记为 `constexpr`，这样 `500_ms` 就会被编译器优化成一个常量，没有运行时开销：

```cpp
constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto startup_delay = 100_ms;
// startup_delay 在编译期就已经构造好了
// 生成的代码等价于直接写 Milliseconds{100}
```

如果不标记 `constexpr`，字面量运算符就成了普通函数调用——虽然内联后开销也不大，但失去了编译期计算的能力，也无法用于 `static_assert` 和模板参数。

C++20 引入了 `consteval`，可以强制字面量运算符只在编译期执行：

```cpp
consteval Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}

constexpr auto t1 = 100_ms;   // OK，编译期执行
// int x = 100;
// auto t2 = x_ms;            // 错误！x 不是编译期常量
```

------

## 常见陷阱

### 后缀命名冲突

如果你在头文件中定义了 `_deg` 后缀，而另一个库也定义了同名的 `_deg` 但实现不同，`using namespace` 时就会出现二义性。解决方案是为后缀使用独特的前缀，或者始终使用完整的命名空间限定。

### 浮点精度

浮点 UDL 可能有精度问题。`0.1_V + 0.2_V` 在浮点运算中可能不等于 `0.3_V`。解决方案是用整数表示——比如存储毫伏而不是伏特：

```cpp
struct Voltage {
    std::int64_t millivolts;  // 用整数存储
};

constexpr Voltage operator""_V(long double value) {
    return Voltage{
        static_cast<std::int64_t>(value * 1000.0 + 0.5)};
}

constexpr auto v1 = 0.1_V + 0.2_V;
constexpr auto v2 = 0.3_V;
static_assert(v1.millivolts == v2.millivolts);  // OK
```

### 运算优先级

```cpp
auto x = 100_km / 2 * 3;  // (100_km / 2) * 3 = 150_km
auto y = 100_km / (2 * 3); // 100_km / 6 ≈ 16.67_km
```

字面量运算符的优先级和普通运算符一样，从左到右结合。写复杂表达式时要注意加括号。

### 整数溢出

大数字的单位转换可能溢出。如果你的 UDL 涉及乘法（比如 `operator""_ms` 里乘以 1000000），要考虑 `unsigned long long` 的上限（约 1.8 * 10^19），并在文档中注明范围限制。

------

## 通用示例

最后来看几个常用的字面量定义，可以直接用到你的项目中：

```cpp
#include <cstdint>

namespace mylib::literals {

// ===== 时间单位 =====
struct Milliseconds { std::uint64_t value; };
struct Microseconds { std::uint64_t value; };
struct Seconds      { std::uint64_t value; };

constexpr Milliseconds operator""_ms(unsigned long long v) {
    return Milliseconds{v};
}
constexpr Microseconds operator""_us(unsigned long long v) {
    return Microseconds{v};
}
constexpr Seconds operator""_s(unsigned long long v) {
    return Seconds{v};
}

// ===== 频率单位 =====
struct Hertz { std::uint32_t value; };

constexpr Hertz operator""_Hz(unsigned long long v) {
    return Hertz{static_cast<std::uint32_t>(v)};
}
constexpr Hertz operator""_kHz(long double v) {
    return Hertz{static_cast<std::uint32_t>(v * 1000.0)};
}
constexpr Hertz operator""_MHz(long double v) {
    return Hertz{static_cast<std::uint32_t>(v * 1000000.0)};
}

// ===== 内存单位 =====
struct Bytes { std::uint64_t value; };

constexpr Bytes operator""_B(unsigned long long v) {
    return Bytes{v};
}
constexpr Bytes operator""_KiB(unsigned long long v) {
    return Bytes{v * 1024};
}
constexpr Bytes operator""_MiB(unsigned long long v) {
    return Bytes{v * 1024 * 1024};
}

// ===== 温度单位 =====
struct Celsius    { double value; };
struct Fahrenheit { double value; };

constexpr Celsius operator""_degC(long double v) {
    return Celsius{static_cast<double>(v)};
}
constexpr Fahrenheit operator""_degF(long double v) {
    return Fahrenheit{static_cast<double>(v)};
}
constexpr Celsius operator""_degK(long double v) {
    return Celsius{static_cast<double>(v - 273.15)};
}

// ===== 角度单位 =====
struct Degrees { double value; };

constexpr Degrees operator""_deg(long double v) {
    return Degrees{static_cast<double>(v)};
}
constexpr Degrees operator""_rad(long double v) {
    return Degrees{static_cast<double>(v * 180.0 / 3.14159265358979323846)};
}

}  // namespace mylib::literals
```

使用时：

```cpp
using namespace mylib::literals;

auto delay_time = 100_ms;
auto sys_clock = 72_MHz;
auto buffer_size = 4_KiB;
auto room_temp = 25.0_degC;
auto angle = 3.14159_rad;
```

每个数字后面都带着它的单位，代码几乎不需要注释。这就是 UDL 的价值。

------

## 小结

用户自定义字面量是 C++11 引入的一个精巧机制，它让我们能够为字面量添加类型和语义。核心要点有三个：`operator""` 的四种形式（整型、浮点、字符串、字符）及其 cooked/raw 区别；后缀必须以下划线 `_` 开头，标准库后缀不带下划线；务必标记 `constexpr` 以获得编译期计算和零运行时开销。

下一篇我们会进入实战，用 UDL 构建一个类型安全的物理单位系统——包括长度、时间、速度的单位组合与编译期检查。

## 参考资源

- [cppreference: User-defined literals](https://en.cppreference.com/w/cpp/language/user_literal)
- [cppreference: std::literals](https://en.cppreference.com/w/cpp/symbol_index/literals)
