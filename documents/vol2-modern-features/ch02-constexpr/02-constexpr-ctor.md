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
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 15
prerequisites:
  - "Chapter 2: constexpr 基础"
related:
  - "consteval 与 constinit"
  - "编译期计算实战"
---

# constexpr 构造函数与字面类型

上一篇咱们把 `constexpr` 变量和 `constexpr` 函数过了一遍，例子基本停在标量值和标准库现成的 `array` 上，咱们自己写的类型，还没作为对象在编译期构造过呢。您多半会接着往下问：那自定义的类呢？能不能在编译期构造一个复数对象，或者干脆把一个日期在编译期就算好了，运行时拿过来就能用了？

能的，不过编译器有要求：您的类型得是"字面类型"（literal type）。名字是唬人了点，事情倒是不复杂：它其实就是一份约束清单，类型达到了清单上的要求，编译器才能在编译期完整地构造和操作它。这一篇咱们就把清单拆开，看看怎么给自定义类型配上 `constexpr` 的构造函数。

## 第一步——什么是字面类型

咱们得把"字面类型"跟"字面量"（您都见过的 `42`、`"hello"`）分开，它们可不是一回事呢。字面量是写在源代码里的值，字面类型指的是另一头：类型本身要满足特定的约束，编译器才能在编译期完整地构造、操作和销毁它的对象。

咱们把具体条件分两头数。标量类型这边咱们最省心：算术类型、指针、引用、枚举，天然就是字面类型的，咱们什么都不用做。类类型那边要求就多了，咱们一条一条数。

头一条说的是构造：非聚合类至少得有一个 `constexpr` 的构造函数，而且不能是拷贝或移动的。拷贝构造、移动构造当然也能标 `constexpr`，可咱们光靠它们过不了关。

咱们把第二条落在成员上，看看里面装的都是什么：所有非静态数据成员本身也得是字面类型的，数组当然也算，只要它的元素还是字面类型的。

第三条咱们留给析构函数：要么是平凡的（trivial destructor），要么在 C++20 之后是 `constexpr` 的。数到这里呢，就有三条了。不过还压着一条虚基类的约束，咱们留到篇末「几个容易出问题的地方」再摊开。

您无需想得复杂，其实，编译器要的只有一件事：在编译期就把该类型的对象彻底算清楚。内存布局长什么样、初始值是多少，全都定下来了。运行时的动态分配、虚函数表查找、复杂的析构逻辑，咱们一样都不沾。

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

咱们再来看一个反例，这个就不是字面类型了：

```cpp
struct NotLiteral {
    std::string name;  // std::string 有非平凡的析构函数（C++20 之前）
    // 即使在 C++20 中，std::string 的析构虽然可以是 constexpr，
    // 但它内部涉及动态内存分配，在编译期求值时仍然受限
};
```

`std::string` 坏就坏在它管理着动态的内存。咱们把时间放回 C++20 之前：那时 `constexpr` 函数里是不允许用 `new`/`delete` 的，于是任何需要动态分配的类型，就都没法进编译期了。C++20 把禁令放宽了，咱们可以在 `constexpr` 函数里用 `new`/`delete` 了，但附带一个硬性的约束：所有在编译期分配的内存，咱们必须赶在求值结束之前把它释放掉，当然不能泄漏到运行时去。

咱们把 `std::string` 的许可和禁止分开说。您可以在编译期做复杂的字符串操作，这没问题的。但是把一个指向编译期分配内存的 `std::string` 返回给运行时接着用，可就不行了。

例外咱们看内存的去处就明白：那块内存要是在编译期求值结束前被释放了，或者转移进了能持久化的存储，就不算数了。反过来呢，只要它还挂在编译期的分配上，咱们在运行时就不能再碰它了。

咱们把编译期 `new` 的许可和禁止做成了动画，您可以按步进键逐段看：

<Anim id="constexpr-transient-alloc" />

编译器这边的支持已经很到位：GCC 12+（libstdc++）和 Clang 15+（libc++）就完整支持 `std::string` 的 `constexpr` 操作了，构造、拼接、子串全都在支持的范围里。咱们可以在编译期构建字符串、验证格式、生成查找表，只要咱们把所有动态内存都在编译期管好了就行。

## 第二步——给自定义类型加上 constexpr 构造函数

### 最简单的情形：类 POD 的类型

咱们可以把 POD（Plain Old Data）粗略理解成一类"除了数据本身什么都没有"的 C 风格结构体，标量类型本身也算进 POD 里去了。要是您的类就是没有虚函数、没有动态分配的一堆数据聚合，咱们轻轻松松就能给它加上 `constexpr` 的构造函数。

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

咱们走到这儿，`Color` 已经是字面类型了。您看它的构造函数，其实就是拿初始化列表把参数挨个地交给成员，够直接的。

### 带逻辑的构造函数

构造函数里也是可以写逻辑的，条件是这些逻辑得在 `constexpr` 允许的范围内。咱们等到 C++14，就可以在构造函数里写循环、条件判断和局部变量了。

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

咱们看它干了什么：构造函数里实现了十进制到 BCD 编码的转换。BCD 的全称是 Binary-Coded Decimal，咱们直译成二进制编码的十进制，硬件侧常用的表示法，每个十进制位拿 4 个比特来装的，所以 42 的 BCD 恰好就是 `0x42`：`4` 和 `2` 各占 4 个比特。整个计算都是在编译期完成的，`kDec42` 的 `bcd` 成员直接被写成 `0x42`。

这套做法咱们在嵌入式开发里特别用得上。咱们在编译期把人类可读的十进制值，转换成硬件要求的 BCD 编码，运行时直接使用预计算好的值，连一条转换指令都省了。

零开销不是咱们空口说的，笔者在 GCC 15.2.1（`-std=c++20 -O2`）下验证过：直接返回 `kDec42.bcd` 的时候，汇编里只剩一条 `movl $66, %eax` 了，常量直接做了立即数。真要在运行时用到它的存储（比如取 `kDec42` 的地址），值就落进 .rodata（只读数据段）了，咱们访问它，用的就是一条内存加载指令。咱们要是换成运行时计算 BCD，就得跑多条除法、移位和循环指令了。编译期版本确实做到了零运行时开销。

## 第三步——constexpr 成员函数

咱们接着看成员函数这一侧：能标 `constexpr` 的可不只是构造函数哦，咱们照样给普通成员函数标上。而且从 C++14 起，`constexpr` 成员函数可以修改对象的成员变量（只要调用上下文允许）。

### 编译期复数类

咱们来写一个能在编译期用的复数类。为什么挑它呢？您看信号处理就知道了：复数运算到处都是。咱们在 FFT（Fast Fourier Transform，也就是咱们说的快速傅里叶变换）里见得更多：每个角频率都对应一个单位复数，也就是行话叫的旋转因子（twiddle factor）。

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

咱们看刚写完的 `Complex`：它是完全的字面类型，构造函数是 `constexpr` 的，所有运算符和成员函数也全都标上了 `constexpr`。咱们可以在编译期做复数运算、生成 FFT 的旋转因子表。咱们拿到手的计算结果会被编译器优化成常量，要么直接嵌入代码，要么放进 .rodata 只读数据段，具体就要看优化级别和使用方式了。

笔者拿 GCC 15.2.1（`-std=c++20 -O2`）看过实际产物：`kI_Squared` 被放进 .rodata 段作为一个常量，访问它只要一条内存加载指令就够了。要是把旋转因子铺成数组（上面代码里的 `kTwiddle` 只是单个值），整个数组会被完整地编译进二进制，运行时访问是不带任何计算开销的。这些值再被内联到使用点的话，连加载指令都可能给优化掉了，直接成为立即数了。

### 编译期日期计算

咱们再看一个跟日期有关的实用场景。不少协议和时间相关的逻辑，都要验证日期的合法性，咱们可以把这些验证整个搬进编译期。

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

有一处还请您多看一眼：`constexpr` 构造函数是不会因为值"逻辑上不合理"就报错的。咱们要想让非法日期在编译期就被拦下来的话，那咱们就得自己动手了。咱们要么在构造函数里主动触发编译期错误，用 `throw` 就可以了，它在 `constexpr` 上下文中就是编译错误的。要么呢，咱们就拿 `static_assert` 配合 `is_valid()` 来检查。

### 编译期字符串长度

成员函数返回编译期可用的值，也是 `constexpr` 的一个重要用法。咱们来写个简单的编译期字符串包装类。

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

咱们这个 `ConstString`，本质上就是 cppreference 官方示例里 `conststr` 类的简化版。它并不拥有字符串数据哦，手里就是指针和长度的组合。指针指的那块存储会不会过期？咱们看一眼就能放心：它接的是字符串字面量，字面量的寿命跟程序一样长，编译期用完了，运行时也还在呢。所以在编译期做字符串操作，它已经够用了。

## 第四步——C++14 放宽的限制

咱们回头看一个事实：刚才 `BcdDecimal` 构造函数里那个 `while` 循环，放到 C++11 是写不出来的。

C++11 那会儿 `constexpr` 构造函数的函数体是必须为空的，所有的初始化工作只能靠成员初始化列表来完成。咱们连循环、条件判断、局部变量都不能写，统统都不许有的。构造逻辑稍微复杂一点就难受了：您想遍历个数组、按条件设置不同的值，就都得拿三元运算符和递归函数去硬绕了。

咱们把时间拨到 C++14：函数体里可以写任何 `constexpr` 允许的语句了。很多原来咱们根本写不出来的编译期类，从这一刻起成为现实了。

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

## 第五步——constexpr 析构函数（C++20）

而在 C++20 之前，字面类型要求析构函数必须是平凡的（trivial），等于不让咱们在析构函数里做任何清理工作。这个限制在 C++20 被取消：您可以写 `constexpr` 析构函数了。

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

主流编译器在 C++20 里对它的支持已经很完整：GCC 10+ 和 Clang 10+ 早就支持了，MSVC 19.28+ 也跟上了。对咱们大多数嵌入式场景来说，它的主要意义在于，让 `std::vector`、`std::string` 这些标准容器能更完整地参与编译期计算。咱们可以在编译期构造容器、操作元素，然后咱们再在编译期把它们统统销毁掉。

> 咱们再往 C++23 看，还有继续的放宽（P2448R2 是标准委员会的提案编号）：`constexpr` 函数不再要求返回类型和参数类型必须是字面类型，非字面类型的局部变量、`goto` 语句和标签也被放行了。而分量更重的一条，是 `constexpr` 函数模板不再要求每个实例化都能常量求值了。咱们放宽了一圈回头看，`constexpr` 函数在定义层面的限制已经所剩无几。当然，咱们真要在编译期调用（求值）这些函数的话，还是得受常量表达式求值规则的约束。您拿到手的，其实只是"函数体可以写得更自由"这一层而已。

## 实战应用：嵌入式中的编译期配置

嵌入式开发的外设配置，通常就是一堆固定的参数：波特率、数据位、停止位、校验方式这些。咱们可以用字面类型把配置打包成编译期常量。

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

`kDebugUart` 和 `kGpsUart` 的验证和计算，编译期就全部做完了。哪天您手一滑，把波特率改成了 0、数据位改成了 3，`static_assert` 当场就把编译拦下来了。波特率寄存器的值也是预计算好的，运行时咱们直接往寄存器里写就行。

## 几个容易出问题的地方

### 非平凡析构函数的阻塞

要是您的类有非平凡的析构函数（比如手动管理了资源），它在 C++20 之前就当不上字面类型了。哪怕构造函数是 `constexpr` 的，析构函数不是 `constexpr`（也不是平凡的），咱们想在编译期用它照样会被堵住。常见的变通办法，就是咱们把析构函数声明成 `= default`，让编译器生成一个平凡的析构函数。当然，前提是您的类确实不需要自定义的析构逻辑，咱们才敢这么省事。

### `mutable` 成员

咱们还得为 `mutable` 数据成员提个醒。`constexpr` 对象的 `mutable` 成员，在编译期求值时是会被视为可修改的。可这么一来，某些上下文里的编译期求值就会失败了，因为 `mutable` 破坏了"对象在编译期完全确定"这个语义假设。

### 虚函数与虚基类

咱们得把它们拆开说，规则是完全不一样的。有虚基类的类，到 C++23 为止都当不了字面类型。GCC 的编译提示显示，C++26 起这个限制就会放开了。

虚函数那边呢，就是另一回事了：它并不剥夺字面类型的资格。检验这一点的时候，笔者拿 `std::is_literal_type` 当过探针。这个 trait 从 C++17 起就挂了弃用标记，而 C++23 已经把它从标准里除名了，不过 libstdc++ 还留着它，正适合咱们拿来干这活。实测结果：带虚函数的类，只要配上了 `constexpr` 构造函数和平凡析构，`-std=c++11/17/20` 三档下就全是 `true` 了。真正会出问题的是虚析构这一头：带虚函数的类多半还要配虚析构，而虚析构必然不是平凡的。咱们实测 `-std=c++17`，连 `virtual ~DV() = default;` 的类都不是字面类型。而从 C++20 起，`= default` 的虚析构才按 `constexpr` 放行。

调用的麻烦紧随其后：C++20 之前，咱们在常量求值里做不了虚函数调用。而 C++20（P1064，也是标准委员会的提案编号）之后，连虚成员函数都能标上 `constexpr` 了。

您要是确实需要在编译期拿到多态调用的话，可以考虑的方案就是 CRTP（Curiously Recurring Template Pattern，奇特重现模板模式）：咱们让派生类把自身作为模板参数传给基类，在编译期就把该调谁给定下来了，用它顶替虚函数的运行时分派。

咱们把这一篇讲过的字面类型约束收拢成一张清单，达标的和不达标的各在一边：

![字面类型达标清单](./02-constexpr-ctor-layout.drawio)

## 参考资源

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: LiteralType requirement](https://en.cppreference.com/w/cpp/named_req/LiteralType)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
