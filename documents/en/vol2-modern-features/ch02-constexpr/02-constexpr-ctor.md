# constexpr Constructors and Literal Types

## Introduction

In the previous chapter, we discussed `constexpr` variables and `constexpr` functions, but all the examples were limited to scalar types—integers, floating-point numbers, and pointers, which are "primitive" types. You might ask: can we use custom classes at compile time too? For example, constructing a complex number object at compile time, or calculating a date at compile time, and then using it directly at runtime?

The answer is yes, but with a prerequisite: your type must be a "literal type." This concept sounds a bit academic, but it is essentially a checklist of constraints that allows the compiler to understand and manipulate a type at compile time. In this chapter, we will clarify what literal types are, how to add `constexpr` constructors to custom types, and how these restrictions were gradually relaxed after C++14.

## Step 1 — What is a Literal Type

The name "literal type" is indeed a bit confusing. It is not the same thing as a "literal" (like `42`, `"hello"`). A literal type refers to a type that satisfies specific constraints—the compiler can fully construct, manipulate, and destroy objects of this type at compile time.

Specifically, for a type to be a literal type, it must meet the following conditions: scalar types (arithmetic types, pointers, references, enumerations) are naturally literal types and require no extra effort; for class types, it needs to have a `constexpr` constructor (at least one, which can be a copy or move constructor), all non-static data members must themselves be literal types or arrays of literal types, and its destructor must either be trivial or, after C++20, `constexpr`.

In plainer terms: the compiler needs to fully understand the memory layout and initial values of this type at compile time, without requiring runtime dynamic allocation, virtual function table lookups, or complex destruction logic.

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

The following, however, is not a literal type:

```cpp
struct NotLiteral {
    std::string name;  // std::string 有非平凡的析构函数（C++20 之前）
    // 即使在 C++20 中，std::string 的析构虽然可以是 constexpr，
    // 但它内部涉及动态内存分配，在编译期求值时仍然受限
};
```

The issue with `std::string` is that it manages dynamic memory. Before C++20, `constexpr` functions were not allowed to use `new`/`delete`, so any type requiring dynamic allocation could not be used at compile time. C++20 relaxed this restriction—allowing `new`/`delete` in `constexpr` functions, but with a hard constraint: all memory allocated at compile time must be freed before the compile-time evaluation ends (it cannot leak into runtime). This means you can do complex string operations at compile time, but you cannot return a `std::string` pointing to compile-time allocated memory into runtime (unless that memory has already been freed or transferred to persistent storage).

In practice, GCC 15.2.1 and Clang 13+ fully support `constexpr` operations on `std::string`, including construction, concatenation, and substring extraction. You can build strings, validate formats, and generate lookup tables at compile time, as long as all dynamic memory is properly managed during compilation.

## Step 2 — Adding constexpr Constructors to Custom Types

### The Simplest Case: POD-like Types

If your class is simply an aggregate of data, without virtual functions or dynamic allocation, adding a `constexpr` constructor is very straightforward.

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

This is now a literal type. The constructor uses an initializer list to assign parameters to members, which is very direct.

### Constructors with Logic

Constructors can also contain logic—provided that this logic falls within what `constexpr` allows. After C++14, you can write loops, conditional statements, and local variables inside constructors.

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

This code implements decimal-to-BCD encoding conversion inside the constructor. The entire calculation completes at compile time, and the `bcd` member of `kDec42` is directly written as `0x42`. This pattern is particularly useful in embedded development—you can convert human-readable decimal values into hardware-required BCD encoding at compile time, and use the pre-calculated values directly at runtime without any conversion instructions.

Let's verify this: under GCC 15.2.1 (`-std=c++20 -O2`), the assembly code for accessing `kDec42.bcd` is just a single `mov` instruction loading a constant from the .rodata section, whereas runtime BCD calculation requires multiple division, shift, and loop instructions. The compile-time version truly achieves zero runtime overhead.

## Step 3 — constexpr Member Functions

Not only can constructors be `constexpr`, but ordinary member functions can be as well. Furthermore, starting from C++14, `constexpr` member functions can modify an object's member variables (as long as the calling context permits).

### A Compile-Time Complex Number Class

Let's write a complex number class that can be used at compile time. This example is quite practical because complex number operations are ubiquitous in signal processing.

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

This `Complex` class is entirely a literal type. Its constructor is `constexpr`, and all operators and member functions are too. You can perform complex number operations at compile time, generate FFT twiddle factor tables—all these calculation results will be optimized by the compiler into constants, directly embedded into the code or placed in the .rodata read-only data section (depending on the optimization level and usage).

For example, under GCC 15.2.1 (`-std=c++20 -O2`), `kI_Squared` will be placed in the .rodata section as a constant, and accessing it is just a single memory load instruction. The `kTwiddleFactors` array will be fully compiled into the binary, and runtime access incurs no computational overhead. If these values are inlined at their point of use, even the load instruction might be optimized away, becoming an immediate value.

### Compile-Time Date Calculation

Another practical scenario is dates. Many protocols and time-related logic need to validate the legitimacy of dates. We can move this validation to compile time.

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

A key point here: the `constexpr` constructor itself will not report an error just because of "logically unreasonable" values. You need to proactively trigger a compile-time error in the constructor (for example, using `throw`, where an exception in a `constexpr` context is a compilation error), or use `static_assert` combined with `is_valid()` for checking.

### Compile-Time String Length

Having member functions return values available at compile time is also an important application of `constexpr`. For instance, a simple compile-time string wrapper class.

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

This `ConstString` is essentially a simplified version of the `conststr` class from the official cppreference example. It does not own the string data; it merely holds a pointer and a length, but this is sufficient to perform many string operations at compile time.

## Step 4 — Restrictions Relaxed in C++14

As mentioned earlier, C++14 significantly relaxed the restrictions on `constexpr` constructors and member functions. Specifically for class types, the impact of these changes is:

In C++11, the function body of a `constexpr` constructor had to be empty—all initialization work could only be done through member initializer lists, and loops, conditional statements, or local variables were not allowed. This meant that if your construction logic was even slightly complex (such as needing to iterate over an array or set different values based on conditions), you had to find ways to work around the limitations using ternary operators and recursive functions.

After C++14, you can write any statement permitted by `constexpr` inside constructors. Local variables, `for` loops, and `if-else` are all fine. This made many previously impossible compile-time classes a reality.

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

## Step 5 — constexpr Destructors (C++20 Preview)

Before C++20, literal types required the destructor to be trivial. This meant you could not do any cleanup work in the destructor. This restriction was removed in C++20—you can now write `constexpr` destructors.

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

This feature is fully supported by mainstream compilers in C++20. GCC 10+, Clang 10+, and MSVC 19.28+ all support `constexpr` destructors. For most embedded scenarios, the main significance of `constexpr` destructors is that they allow standard containers like `std::vector` and `std::string` to participate more fully in compile-time computation—you can construct containers, manipulate elements, and then destroy them, all at compile time.

It is worth mentioning in passing the further relaxation of `constexpr` in C++23: `constexpr` functions no longer require their return type and parameter types to be literal types (P2448R2), and non-literal-type local variables, `goto` statements, and labels are also allowed. This means that starting from C++23, there are very few restrictions on defining `constexpr` functions. Of course, to actually call (evaluate) these functions at compile time, they are still subject to the rules of constant expression evaluation—you simply have more freedom in writing the function body.

## Practical Application: Compile-Time Configuration in Embedded Systems

In embedded development, peripheral configuration is usually a set of fixed parameters—baud rate, data bits, stop bits, parity, and so on. We can use literal types to package these configurations as compile-time constants.

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

`kDebugUart` and `kGpsUart` complete all validation and calculation at compile time. If someone changes the baud rate to 0 or the data bits to 3, `static_assert` will blow up at compile time. The baud rate register value is also pre-calculated, so at runtime we simply write it directly to the register.

## Common Pitfalls

### Blocking by Non-Trivial Destructors

If your class has a non-trivial destructor (for example, it manually manages resources), it cannot be a literal type before C++20. Even if your constructor is `constexpr`, a destructor that is not `constexpr` (or trivial) will prevent compile-time usage. A common workaround is to declare the destructor as `= default`, letting the compiler generate a trivial destructor—provided your class truly does not need custom destruction logic.

### `mutable` Members

`mutable` data members can lead to some unexpected behavior. The `mutable` members of a `constexpr` object are treated as modifiable during compile-time evaluation, but this can cause compile-time evaluation to fail in certain contexts (because `mutable` breaks the semantic assumption that "the object is fully determined at compile time").

### Virtual Functions and Virtual Base Classes

Classes with virtual functions or virtual base classes can never be literal types (this remains true up to the current standard). If you need to use a type hierarchy at compile time, consider using CRTP (Curiously Recurring Template Pattern) to replace virtual functions.

## Summary

In this chapter, we covered the definition and constraints of literal types, how to write `constexpr` constructors, the use of `constexpr` member functions, and the gradual relaxation of these restrictions in C++14/C++20/C++23. The core takeaway is: as long as the memory layout and lifetime of your type can be fully determined at compile time, the compiler can construct and manipulate it at compile time. Types like compile-time complex numbers, dates, strings, and configuration structures can all become literal types, thereby participating in more complex compile-time computations.

In the next chapter, we will introduce the `consteval` and `constinit` keywords added in C++20, and see how they precisely control the behavior of compile-time evaluation.

## Reference Resources

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: LiteralType requirement](https://en.cppreference.com/w/cpp/named_req/LiteralType)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
