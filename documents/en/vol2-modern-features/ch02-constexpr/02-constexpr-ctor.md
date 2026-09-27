---
title: "constexpr Constructors and Literal Types"
description: "Enable custom types to participate in compile-time computation, and understand the design constraints and evolution of literal types"
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
  - 'Chapter 2: constexpr Basics: The Art of Compile-Time Evaluation'
related:
  - 'consteval and constinit: New Tools for Compile-Time Guarantees'
  - 'Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/02-constexpr-ctor.md
  source_hash: 8ae2509395bbeb8acb4e80b39ced2d2d233b2dc228da4369277441b363472b3e
  translated_at: '2026-09-25T14:57:54+00:00'
  engine: anthropic
  token_count: 3100
---

# constexpr Constructors and Literal Types

In the previous chapter we discussed `constexpr` variables and `constexpr` functions, but every example was confined to scalar types—integers, floating-point numbers, pointers, those "raw" kinds of types. You might ask: can I put my own custom classes to work at compile time too? For example, construct a complex number object during compilation, or work out a date at compile time and then use it directly at runtime?

The answer is yes, but with one precondition: your type must be a "literal type". The concept sounds a bit academic, but it's really just a checklist of constraints on types the compiler can understand and manipulate at compile time. In this chapter we'll pin down what a literal type is, how to give a custom type a `constexpr` constructor, and how these restrictions were gradually relaxed after C++14.

## Step 1 — What Is a Literal Type

The name "literal type" is admittedly confusing. It is not the same thing as a "literal" (like `42` or `"hello"`). A literal type refers to a type that satisfies a specific set of constraints—a type whose objects the compiler can fully construct, manipulate, and destroy at compile time.

Concretely, for a type to be a literal type the following must hold: scalar types (arithmetic types, pointers, references, enumerations) are literal types by nature and require nothing extra on your part; a class type needs a `constexpr` constructor (at least one, which may be a copy or move constructor), all of its non-static data members must themselves be literal types or arrays thereof, and its destructor must either be trivial, or—since C++20—`constexpr`.

Put more plainly: the compiler needs to fully understand this type's memory layout and initial values during compilation, with no runtime dynamic allocation, no virtual function table lookups, and no complicated destruction logic.

```cpp
// This is a literal type
struct Point {
    float x;
    float y;

    constexpr Point(float x_, float y_) : x(x_), y(y_) {}
    // The implicit destructor is trivial, which satisfies the requirement
};

constexpr Point kOrigin{0.0f, 0.0f};
static_assert(kOrigin.x == 0.0f);
static_assert(kOrigin.y == 0.0f);
```

The following, on the other hand, is not a literal type:

```cpp
struct NotLiteral {
    std::string name;  // std::string has a non-trivial destructor (before C++20)
    // Even in C++20, though std::string's destructor can be constexpr,
    // its internal dynamic memory allocation is still restricted at compile time
};
```

`std::string`'s problem is that it manages dynamic memory. Before C++20, `new`/`delete` were not allowed inside `constexpr` functions, so any type requiring dynamic allocation was simply unusable at compile time. C++20 relaxed this—`new`/`delete` are now allowed in `constexpr` functions—but with one hard constraint: all memory allocated during compile-time evaluation must be released before that evaluation ends (nothing may leak into runtime). This means you can do complex string operations at compile time, but you cannot return a `std::string` pointing to compile-time-allocated memory to runtime (unless that memory has already been released or moved into persistable storage).

In practice, GCC 15.2.1 and Clang 13+ already fully support `constexpr` operations on `std::string`, including construction, concatenation, substrings, and so on. You can build strings at compile time, validate formats, generate lookup tables—as long as all dynamic memory is properly managed during compilation.

Collecting the literal-type constraints into one checklist, with the qualifiers on one side and the disqualified on the other:

![Literal type qualification checklist](./02-constexpr-ctor-layout.drawio)

## Step 2 — Giving a Custom Type a constexpr Constructor

### The Simplest Case: POD-like Types

If your class is just an aggregate of data—no virtual functions, no dynamic allocation—adding a `constexpr` constructor is very easy.

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

This is already a literal type. The constructor assigns the parameters to the members through the initializer list—about as direct as it gets.

### Constructors with Logic

A constructor can contain logic too—provided that logic stays within what `constexpr` permits. Since C++14, you can write loops, conditional branches, and local variables inside a constructor.

```cpp
struct BcdDecimal {
    unsigned char bcd;

    constexpr explicit BcdDecimal(int decimal) : bcd(0)
    {
        // Convert a decimal integer to BCD encoding
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

This code implements the decimal-to-BCD conversion inside the constructor. The entire computation happens at compile time, and `kDec42`'s `bcd` member is written directly as `0x42`. This pattern is especially useful in embedded development—you can convert human-readable decimal values into the BCD encoding the hardware expects at compile time, and then use the precomputed value at runtime, without a single conversion instruction.

Let's verify: under GCC 15.2.1 (`-std=c++20 -O2`), the assembly for accessing `kDec42.bcd` is just one `mov` instruction loading a constant from the .rodata section, while computing BCD at runtime takes multiple division, shift, and loop instructions. The compile-time version truly achieves zero runtime overhead.

## Step 3 — constexpr Member Functions

Not only constructors—ordinary member functions can be `constexpr` too. And since C++14, a `constexpr` member function may modify an object's member variables (as long as the calling context allows it).

### A Compile-Time Complex Number Class

Let's write a complex number class that can be used at compile time. This example is fairly practical, because complex arithmetic is everywhere in signal processing.

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

// Compile-time complex arithmetic
constexpr Complex kI{0.0f, 1.0f};           // The imaginary unit i
constexpr Complex kI_Squared = kI * kI;     // i^2 = -1
static_assert(kI_Squared == Complex{-1.0f, 0.0f}, "i^2 should equal -1");

// Generate a compile-time sequence of complex numbers (e.g., FFT twiddle factors)
template <std::size_t N>
constexpr Complex compute_twiddle_factor(std::size_t k)
{
    constexpr double kPi = 3.14159265358979323846;
    double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(N);
    // Approximate cos and sin with Taylor expansions
    double cos_val = 1.0 - angle * angle / 2.0 + angle*angle*angle*angle / 24.0;
    double sin_val = angle - angle*angle*angle / 6.0 + angle*angle*angle*angle*angle / 120.0;
    return Complex{static_cast<float>(cos_val), static_cast<float>(sin_val)};
}

constexpr Complex kTwiddle = compute_twiddle_factor<8>(1);
static_assert(kTwiddle.magnitude_squared() > 0.99f, "Twiddle factor should be on unit circle");
```

This `Complex` class is a literal type through and through. Its constructor is `constexpr`, and so are all the operators and member functions. You can do complex arithmetic at compile time and generate FFT twiddle-factor tables—all of these computed results are optimized by the compiler into constants, embedded directly into the code or placed in the .rodata read-only data section (depending on the optimization level and how they are used).

For example, under GCC 15.2.1 (`-std=c++20 -O2`), `kI_Squared` is placed in the .rodata section as a constant, and accessing it is a single memory-load instruction. The `kTwiddleFactors` array is compiled into the binary in full, so runtime access carries no computational overhead at all. If these values get inlined at their points of use, even the load instruction may be optimized away—the value becomes an immediate.

### Compile-Time Date Calculation

Another practical scenario is dates. Many protocols and time-related logic need to validate whether a date is legitimate. We can move that validation to compile time.

```cpp
struct Date {
    int year;
    int month;
    int day;

    constexpr Date(int y, int m, int d) : year(y), month(m), day(d)
    {
        // Validate the date at compile time
        // If invalid, trigger a compile error (expression becomes non-constant)
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

// constexpr Date kInvalid{2023, 2, 29};  // does not fail to compile by itself
// An explicit static_assert check is required:
// static_assert(Date{2023, 2, 29}.is_valid());  // compile error!
```

There is one key point here: a `constexpr` constructor does not itself reject "logically unreasonable" values. You need to actively trigger a compile-time error inside the constructor (for example with `throw`—in a `constexpr` context, an exception is a compile error), or check with `static_assert` combined with `is_valid()`.

### Compile-Time String Length

Having member functions return values usable at compile time is another important application of `constexpr`. For example, a simple compile-time string wrapper class.

```cpp
#include <cstddef>

struct ConstString {
    const char* data;
    std::size_t length;

    template <std::size_t N>
    constexpr ConstString(const char (&str)[N]) : data(str), length(N - 1)
    {
        // N - 1 because a string literal ends with '\0'
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

This `ConstString` is essentially a simplified version of the `conststr` class from cppreference's official examples. It does not own the string data—it just holds a pointer and a length—but that is enough for plenty of string operations at compile time.

## Step 4 — The Restrictions C++14 Lifted

We already mentioned this earlier: C++14 dramatically relaxed the restrictions on `constexpr` constructors and member functions. Specifically for class types, the impact of these changes is this:

In C++11, the body of a `constexpr` constructor had to be empty—all initialization work could only be done through the member initializer list, with no loops, conditional branches, or local variables. This meant that if your construction logic was even slightly complex (say, needing to traverse an array or set different values based on a condition), you had to find ways around the restriction using the ternary operator and recursive functions.

After C++14, a constructor can contain any statement that `constexpr` allows. Local variables, `for` loops, `if-else`—no problem at all. This made many compile-time classes that were previously impossible a reality.

```cpp
// C++11 style: the constructor body must be empty
struct OldStyle {
    int values[4];

    // Only the initializer list may be used
    constexpr OldStyle(int a, int b, int c, int d)
        : values{a, b, c, d} {}
};

// C++14 style: the constructor body may contain logic
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

## Step 5 — constexpr Destructors (C++20)

Before C++20, literal types required the destructor to be trivial. That meant you could not do any cleanup work in the destructor. This restriction was removed in C++20—you can now write `constexpr` destructors.

```cpp
// Only supported starting from C++20
struct Resource {
    int* data;
    std::size_t size;

    constexpr Resource(std::size_t n) : data{}, size(n)
    {
        // C++20 allows new in constexpr contexts
        // but the allocated memory must be released before constant evaluation ends
    }

    // C++20: constexpr destructor
    constexpr ~Resource()
    {
        // Cleanup logic
    }
};
```

This feature is already fully supported by mainstream compilers in C++20. GCC 10+, Clang 10+, and MSVC 19.28+ all support `constexpr` destructors. For most embedded scenarios, the main significance of `constexpr` destructors is letting standard containers such as `std::vector` and `std::string` participate more completely in compile-time computation—you can construct containers at compile time, manipulate elements, and then destroy them at compile time.

Worth mentioning in passing here is C++23's further relaxation of `constexpr`: a `constexpr` function no longer requires its return type and parameter types to be literal types (P2448R2), and local variables of non-literal types, `goto` statements, and labels are now allowed as well. This means that from C++23 onward, the restrictions on defining `constexpr` functions are very few indeed. Of course, actually invoking (evaluating) these functions at compile time is still subject to the rules of constant-expression evaluation—you simply get to write freer function bodies.

## Practical Application: Compile-Time Configuration in Embedded Systems

In embedded development, peripheral configuration is usually a pile of fixed parameters—baud rate, data bits, stop bits, parity scheme, and so on. We can use literal types to pack these configurations into compile-time constants.

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
        // Simplified baud-rate register value computation (STM32 style)
        return clock_freq / baud_rate;
    }
};

// Compile-time constants for common configurations
constexpr UartConfig kDebugUart{115200, 8, StopBits::kOne, Parity::kNone};
constexpr UartConfig kGpsUart{9600, 8, StopBits::kOne, Parity::kNone};

static_assert(kDebugUart.is_valid());
static_assert(kDebugUart.compute_brr(72000000) == 625);  // 72MHz / 115200
```

`kDebugUart` and `kGpsUart` complete all validation and computation at compile time. If someone changes the baud rate to 0 or the data bits to 3, the `static_assert` blows up at compile time. The baud-rate register value is precomputed as well—runtime code just writes it straight into the register.

## Common Pitfalls

### Blocking on a Non-Trivial Destructor

If your class has a non-trivial destructor (for example, it manually manages a resource), it cannot be a literal type before C++20. Even if your constructor is `constexpr`, a destructor that is not `constexpr` (or trivial) still blocks compile-time use. A common workaround is to declare the destructor `= default` and let the compiler generate a trivial one—provided your class genuinely needs no custom destruction logic.

### `mutable` Members

`mutable` data members lead to some unexpected behavior. The `mutable` members of a `constexpr` object are treated as modifiable during compile-time evaluation, yet this can cause compile-time evaluation to fail in certain contexts (because `mutable` breaks the semantic assumption that "the object is fully determined at compile time").

### Virtual Functions and Virtual Base Classes

A class with virtual functions or virtual base classes can never be a literal type (this holds up to the current standard). If you need to use a type hierarchy at compile time, consider using CRTP (Curiously Recurring Template Pattern) in place of virtual functions.

## References

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: LiteralType requirement](https://en.cppreference.com/w/cpp/named_req/LiteralType)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
