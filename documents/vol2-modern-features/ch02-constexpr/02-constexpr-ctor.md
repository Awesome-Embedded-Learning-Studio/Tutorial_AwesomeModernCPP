---
title: "constexpr 构造函数与字面类型"
description: "让自定义类型参与编译期计算，理解字面类型的设计约束与演进"
chapter: 2
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - constexpr
  - 编译期计算
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 2: constexpr 基础"
related:
  - "consteval 与 constinit"
  - "编译期计算实战"
---

# constexpr 构造函数与字面类型

## 引言

上一章我们讨论了 `constexpr` 变量和 `constexpr` 函数，但所有的例子都局限在标量类型——整数、浮点数、指针这些"原始"类型。你可能会问：我能不能把自定义的类也放到编译期去用？比如在编译期构造一个复数对象，或者编译期算好一个日期，然后在运行时直接拿来用？

答案是肯定的，但有一个前提：你的类型必须是"字面类型"（literal type）。这个概念听起来有点学术，但实际上它就是编译器能理解并在编译期操作类型的约束清单。这一章我们来搞清楚什么是字面类型，怎么给自定义类型加上 `constexpr` 构造函数，以及 C++14 之后这些限制是如何被逐步放宽的。

## 第一步——什么是字面类型

字面类型这个名字确实容易让人困惑。它跟"字面量"（literal，比如 `42`、`"hello"`）不是一回事。字面类型指的是一种满足特定约束的类型——编译器能够在编译期完整地构造、操作和销毁这种类型的对象。

具体来说，一个类型是字面类型需要满足以下条件：对于标量类型（算术类型、指针、引用、枚举）来说它们天然就是字面类型，不需要做任何额外的事情；对于类类型来说它需要有一个 `constexpr` 构造函数（至少一个，可以是拷贝或移动构造），所有非静态数据成员本身也必须是字面类型或其数组，并且它的析构函数要么是平凡的（trivial destructor），要么在 C++20 之后是 `constexpr` 的。

用更直白的话说：编译器需要在编译期就能完整地理解这个类型的内存布局和初始值，不需要运行时动态分配、虚函数表查找、或者复杂的析构逻辑。

```cpp
// 这是一个字面类型
struct Point {
    float x;
    float y;

    constexpr Point(float x_, float y_) : x(x_), y(y_) {}
    // 隐式的析构函数是平凡的，满足条件
};

constexpr Point kOrigin{0.0f, 0.0f};
static_assert(kOrigin.x == 0.0f);
static_assert(kOrigin.y == 0.0f);
```

而下面这个就不是字面类型：

```cpp
struct NotLiteral {
    std::string name;  // std::string 有非平凡的析构函数（C++20 之前）
    // 即使在 C++20 中，std::string 的析构虽然可以是 constexpr，
    // 但它内部涉及动态内存分配，在编译期求值时仍然受限
};
```

`std::string` 的问题在于它管理了动态内存。在 C++20 之前，`constexpr` 函数中不允许使用 `new`/`delete`，所以任何需要动态分配的类型都不可能在编译期使用。C++20 放宽了这个限制（允许编译期的动态分配，但分配的内存必须在编译期结束前释放），但 `std::string` 的 `constexpr` 支持仍然有限制。

## 第二步——给自定义类型加上 constexpr 构造函数

### 最简单的情形：POD-like 类型

如果你的类就是一堆数据的聚合，没有虚函数、没有动态分配，那加上 `constexpr` 构造函数非常简单。

```cpp
struct Color {
    std::uint8_t r, g, b, a;

    constexpr Color(std::uint8_t r_, std::uint8_t g_,
                    std::uint8_t b_, std::uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
};

constexpr Color kRed{255, 0, 0};
constexpr Color kGreen{0, 255, 0};
constexpr Color kTransparentBlack{0, 0, 0, 0};

static_assert(kRed.r == 255);
static_assert(kTransparentBlack.a == 0);
```

这就是一个字面类型了。构造函数用初始化列表把参数赋给成员，非常直接。

### 带逻辑的构造函数

构造函数里也可以有逻辑——前提是这些逻辑在 `constexpr` 允许的范围内。在 C++14 之后，你可以在构造函数里写循环、条件判断、局部变量。

```cpp
struct BcdDecimal {
    unsigned char bcd;

    constexpr explicit BcdDecimal(int decimal) : bcd(0)
    {
        // 将十进制整数转换为 BCD 编码
        int remainder = decimal;
        int shift = 0;
        while (remainder > 0) {
            bcd |= (remainder % 10) << shift;
            remainder /= 10;
            shift += 4;
        }
    }

    constexpr int to_decimal() const
    {
        int result = 0;
        int multiplier = 1;
        unsigned char temp = bcd;
        while (temp > 0) {
            result += (temp & 0x0F) * multiplier;
            temp >>= 4;
            multiplier *= 10;
        }
        return result;
    }
};

constexpr BcdDecimal kDec42{42};
static_assert(kDec42.bcd == 0x42, "BCD of 42 should be 0x42");
static_assert(kDec42.to_decimal() == 42, "Round-trip conversion should work");
```

这段代码在构造函数中实现了十进制到 BCD 编码的转换。整个计算在编译期完成，`kDec42` 的 `bcd` 成员直接被写为 `0x42`。这种模式在嵌入式开发中特别有用——你可以在编译期把人类可读的十进制值转换为硬件要求的 BCD 编码，运行时零开销。

## 第三步——constexpr 成员函数

不仅构造函数可以 `constexpr`，普通成员函数也可以。而且从 C++14 开始，`constexpr` 成员函数可以修改对象的成员变量（只要调用上下文允许）。

### 编译期复数类

让我们来写一个可以在编译期使用的复数类。这个例子比较实用，因为在信号处理中复数运算无处不在。

```cpp
struct Complex {
    float real;
    float imag;

    constexpr Complex(float r = 0.0f, float i = 0.0f) : real(r), imag(i) {}

    constexpr Complex operator+(const Complex& other) const
    {
        return Complex{real + other.real, imag + other.imag};
    }

    constexpr Complex operator-(const Complex& other) const
    {
        return Complex{real - other.real, imag - other.imag};
    }

    constexpr Complex operator*(const Complex& other) const
    {
        return Complex{
            real * other.real - imag * other.imag,
            real * other.imag + imag * other.real
        };
    }

    constexpr float magnitude_squared() const
    {
        return real * real + imag * imag;
    }

    constexpr bool operator==(const Complex& other) const
    {
        return real == other.real && imag == other.imag;
    }
};

// 编译期复数运算
constexpr Complex kI{0.0f, 1.0f};           // 虚数单位 i
constexpr Complex kI_Squared = kI * kI;     // i^2 = -1
static_assert(kI_Squared == Complex{-1.0f, 0.0f}, "i^2 should equal -1");

// 编译期生成复数序列（例如 FFT 的旋转因子）
template <std::size_t N>
constexpr Complex compute_twiddle_factor(std::size_t k)
{
    constexpr double kPi = 3.14159265358979323846;
    double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(N);
    // 用泰勒展开近似 cos 和 sin
    double cos_val = 1.0 - angle * angle / 2.0 + angle*angle*angle*angle / 24.0;
    double sin_val = angle - angle*angle*angle / 6.0 + angle*angle*angle*angle*angle / 120.0;
    return Complex{static_cast<float>(cos_val), static_cast<float>(sin_val)};
}

constexpr Complex kTwiddle = compute_twiddle_factor<8>(1);
static_assert(kTwiddle.magnitude_squared() > 0.99f, "Twiddle factor should be on unit circle");
```

这个 `Complex` 类完全是字面类型。它的构造函数是 `constexpr` 的，所有运算符和成员函数也是。你可以在编译期做复数运算、生成 FFT 旋转因子表——所有这些结果都会被固化到二进制文件中。

### 编译期日期计算

另一个实用场景是日期。很多协议和时间相关的逻辑需要验证日期的合法性。我们可以把这些验证放到编译期。

```cpp
struct Date {
    int year;
    int month;
    int day;

    constexpr Date(int y, int m, int d) : year(y), month(m), day(d)
    {
        // 编译期验证日期合法性
        // 如果日期非法，触发编译错误（通过让表达式非恒常）
    }

    constexpr bool is_leap_year() const
    {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    constexpr int days_in_month() const
    {
        constexpr int kDays[] = {
            0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
        };
        if (month == 2 && is_leap_year()) {
            return 29;
        }
        return kDays[month];
    }

    constexpr bool is_valid() const
    {
        if (month < 1 || month > 12) return false;
        if (day < 1 || day > days_in_month()) return false;
        if (year < 0) return false;
        return true;
    }
};

constexpr Date kEpoch{1970, 1, 1};
static_assert(kEpoch.is_valid());
static_assert(!kEpoch.is_leap_year());

constexpr Date kY2K{2000, 1, 1};
static_assert(kY2K.is_leap_year(), "2000 is a leap year (divisible by 400)");

constexpr Date kLeapDay{2024, 2, 29};
static_assert(kLeapDay.is_valid(), "2024-02-29 is valid (2024 is a leap year)");

// constexpr Date kInvalid{2023, 2, 29};  // 编译时不会直接报错
// 需要用 static_assert 显式检查：
// static_assert(Date{2023, 2, 29}.is_valid());  // 编译错误！
```

这里有一个要点：`constexpr` 构造函数本身不会因为"逻辑上不合理"的值而报错。你需要在构造函数中主动触发编译期错误（比如用 `throw`，在 `constexpr` 上下文中异常就是编译错误），或者用 `static_assert` 配合 `is_valid()` 来检查。

### 编译期字符串长度

让成员函数返回编译期可用的值也是 `constexpr` 的重要应用。比如一个简单的编译期字符串包装类。

```cpp
#include <cstddef>

struct ConstString {
    const char* data;
    std::size_t length;

    template <std::size_t N>
    constexpr ConstString(const char (&str)[N]) : data(str), length(N - 1)
    {
        // N - 1 是因为字符串字面量的末尾有 '\0'
    }

    constexpr char operator[](std::size_t i) const
    {
        return i < length ? data[i] : '\0';
    }

    constexpr bool starts_with(char c) const
    {
        return length > 0 && data[0] == c;
    }

    constexpr bool equals(const ConstString& other) const
    {
        if (length != other.length) return false;
        for (std::size_t i = 0; i < length; ++i) {
            if (data[i] != other.data[i]) return false;
        }
        return true;
    }
};

constexpr ConstString kHello{"Hello"};
static_assert(kHello.length == 5);
static_assert(kHello[0] == 'H');
static_assert(kHello.starts_with('H'));
static_assert(kHello.equals(ConstString{"Hello"}));
```

这个 `ConstString` 本质上就是 cppreference 官方示例中 `conststr` 类的简化版。它不拥有字符串数据，只是持有一个指针和长度，但足以在编译期做很多字符串操作了。

## 第四步——C++14 放宽的限制

前面已经提到了，C++14 对 `constexpr` 构造函数和成员函数的限制做了大幅放宽。具体到类类型，这些变化带来的影响是：

在 C++11 中，`constexpr` 构造函数的函数体必须为空——所有初始化工作只能通过成员初始化列表完成，不能有循环、条件判断或局部变量。这意味着如果你的构造逻辑稍复杂（比如需要遍历数组、根据条件设置不同值），就得想办法用三元运算符和递归函数来绕过限制。

C++14 之后，构造函数里可以写任何 `constexpr` 允许的语句了。局部变量、`for` 循环、`if-else` 都没问题。这让很多原本不可能的编译期类成为现实。

```cpp
// C++11 风格：构造函数体必须为空
struct OldStyle {
    int values[4];

    // 只能用初始化列表
    constexpr OldStyle(int a, int b, int c, int d)
        : values{a, b, c, d} {}
};

// C++14 风格：构造函数体可以有逻辑
struct NewStyle {
    int values[4];
    int sum;

    constexpr NewStyle(int base) : values{}, sum(0)
    {
        for (int i = 0; i < 4; ++i) {
            values[i] = base + i;
            sum += values[i];
        }
    }
};

constexpr NewStyle kObj{10};
static_assert(kObj.values[0] == 10);
static_assert(kObj.values[3] == 13);
static_assert(kObj.sum == 46);  // 10+11+12+13=46
```

## 第五步——constexpr 析构函数（C++20 预告）

在 C++20 之前，字面类型要求析构函数必须是平凡的（trivial）。这意味着你不能在析构函数里做任何清理工作。这个限制在 C++20 中被取消——你可以写 `constexpr` 析构函数了。

```cpp
// C++20 才支持
struct Resource {
    int* data;
    std::size_t size;

    constexpr Resource(std::size_t n) : data{}, size(n)
    {
        // C++20 允许在 constexpr 上下文中使用 new
        // 但分配的内存必须在常量求值结束前释放
    }

    // C++20: constexpr 析构函数
    constexpr ~Resource()
    {
        // 清理逻辑
    }
};
```

这个特性在 C++20 中的完整支持仍然取决于标准库实现。对于大多数嵌入式场景来说，`constexpr` 析构函数的主要意义在于让 `std::array` 等标准容器能够更完整地参与编译期计算。

这里值得顺带提一句的是 C++23 对 `constexpr` 的进一步放宽：`constexpr` 函数不再要求返回类型和参数类型必须是字面类型（P2448R2），非字面类型的局部变量、`goto` 语句和标签也被允许了。这意味着从 C++23 开始，`constexpr` 函数的定义限制已经非常少了。当然，要在编译期实际调用（求值）这些函数，仍然受到常量表达式求值规则的约束——你只是可以写更自由的函数体了。

## 实战应用：嵌入式中的编译期配置

在嵌入式开发中，外设配置通常是一堆固定参数——波特率、数据位、停止位、校验方式等。我们可以用字面类型把这些配置打包成编译期常量。

```cpp
enum class Parity { kNone, kEven, kOdd };
enum class StopBits { kOne, kTwo };

struct UartConfig {
    std::uint32_t baud_rate;
    std::uint8_t data_bits;
    StopBits stop_bits;
    Parity parity;

    constexpr UartConfig(std::uint32_t baud, std::uint8_t data,
                         StopBits stop, Parity par)
        : baud_rate(baud), data_bits(data), stop_bits(stop), parity(par) {}

    constexpr bool is_valid() const
    {
        if (baud_rate == 0) return false;
        if (data_bits < 5 || data_bits > 9) return false;
        return true;
    }

    constexpr std::uint32_t compute_brr(std::uint32_t clock_freq) const
    {
        // 简化的波特率寄存器值计算（STM32 风格）
        return clock_freq / baud_rate;
    }
};

// 常用配置的编译期常量
constexpr UartConfig kDebugUart{115200, 8, StopBits::kOne, Parity::kNone};
constexpr UartConfig kGpsUart{9600, 8, StopBits::kOne, Parity::kNone};

static_assert(kDebugUart.is_valid());
static_assert(kDebugUart.compute_brr(72000000) == 625);  // 72MHz / 115200
```

`kDebugUart` 和 `kGpsUart` 在编译期就完成了所有验证和计算。如果有人改了波特率为 0 或者数据位为 3，`static_assert` 会在编译期炸掉。波特率寄存器的值也被预计算好了，运行时直接写入寄存器即可。

## 常见陷阱

### 非平凡析构函数的阻塞

如果你的类有非平凡的析构函数（比如手动管理了资源），在 C++20 之前它就不能是字面类型。即使你的构造函数是 `constexpr` 的，析构函数不是 `constexpr`（或平凡的）也会阻止编译期使用。一个常见的变通方法是把析构函数声明为 `= default`，让编译器生成一个平凡的析构函数——前提是你的类确实不需要自定义析构逻辑。

### `mutable` 成员

`mutable` 数据成员会导致一些意外行为。`constexpr` 对象的 `mutable` 成员在编译期求值时被视为可修改的，但这会导致某些上下文中编译期求值失败（因为 `mutable` 破坏了"对象在编译期完全确定"的语义假设）。

### 虚函数与虚基类

有虚函数或有虚基类的类永远不可能是字面类型（直到目前的标准都是如此）。如果你需要在编译期使用一个类型层次结构，考虑用 CRTP（Curiously Recurring Template Pattern）来替代虚函数。

## 小结

这一章我们覆盖了字面类型的定义和约束、`constexpr` 构造函数的写法、`constexpr` 成员函数的使用，以及 C++14/C++20/C++23 对这些限制的逐步放宽。核心要点是：只要你的类型的内存布局和生命周期在编译期就能完全确定，编译器就可以在编译期构造和操作它。编译期复数、日期、字符串、配置结构这些类型都可以成为字面类型，从而参与到更复杂的编译期计算中去。

下一章我们会介绍 C++20 新增的 `consteval` 和 `constinit` 关键字，看看它们如何精确控制编译期求值的行为。

## 参考资源

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: LiteralType requirement](https://en.cppreference.com/w/cpp/named_req/LiteralType)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
