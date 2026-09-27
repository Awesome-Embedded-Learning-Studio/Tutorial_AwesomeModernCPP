---
title: "constexpr Constructors and Literal Types"
description: "Let custom types take part in compile-time computation, and understand the design constraints and evolution of literal types"
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
  source_hash: 800d645be49b93c2ad8937d6871e145c0eeee59d615eefa5608d600673191301
  translated_at: '2026-09-27T10:20:50+00:00'
  engine: anthropic
  token_count: 4200
---

# constexpr Constructors and Literal Types

In the previous article we walked through `constexpr` variables and `constexpr` functions, and the examples mostly stopped at scalar values and the standard library's ready-made `array`—types of our own writing have never yet been constructed as objects at compile time. Your next question almost writes itself: what about custom classes? Can we construct a complex number object at compile time, or work out a date during compilation so runtime code can just pick it up and use it?

Yes, we can—but the compiler has a requirement: your type must be a "literal type". The name sounds more intimidating than the concept is; it is really just a checklist of constraints. Once a type meets everything on the checklist, the compiler can fully construct and manipulate it at compile time. In this article we'll take the checklist apart and see how to equip a custom type with a `constexpr` constructor.

## Step 1 — What Is a Literal Type

We have to separate "literal type" from "literal" (`42`, `"hello"`—you have seen them all before); they are not the same thing at all. A literal is a value written in the source code. A literal type is about the other end: the type itself has to satisfy specific constraints before the compiler can fully construct, manipulate, and destroy its objects at compile time.

Let's count the concrete conditions on two fronts. The scalar side is the easy one: arithmetic types, pointers, references, and enumerations are literal types by nature—there is nothing for us to do. The class-type side carries many more requirements, so let's count them one by one.

The first condition concerns construction: a non-aggregate class must have at least one `constexpr` constructor, and it must not be a copy or move constructor. Copy and move constructors can of course also be marked `constexpr`, but they alone will not get us through the gate.

The second condition lands on the members—on what the class actually holds: every non-static data member must itself be of literal type. Arrays count too, as long as their elements are still of literal type.

The third condition we leave to the destructor: it must either be trivial, or—since C++20—`constexpr`. Counting up, that makes three. One more constraint on virtual base classes is still pressing underneath; we'll unfold it in "Where Things Tend to Go Wrong" at the end of this article.

You don't need to overthink it. What the compiler wants boils down to one thing: the type's objects must be worked out completely during compilation. What the memory layout looks like, what the initial values are—everything gets pinned down. Runtime dynamic allocation, virtual function table lookups, complicated destruction logic: none of that comes into play.

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

Now a counter-example—this one is not a literal type:

```cpp
struct NotLiteral {
    std::string name;  // std::string has a non-trivial destructor (before C++20)
    // Even in C++20, although std::string's destructor can be constexpr,
    // its internals involve dynamic memory allocation, which is still restricted during compile-time evaluation
};
```

`std::string`'s trouble is precisely that it manages dynamic memory. Set the clock back to before C++20: `new`/`delete` were not allowed inside `constexpr` functions then, so any type that needed dynamic allocation was locked out of compile time. C++20 relaxed the ban—we can now use `new`/`delete` in `constexpr` functions—but it comes with one hard constraint: all memory allocated during compile-time evaluation must be released before that evaluation ends, and of course none of it may leak into runtime.

Let's spell out what `std::string` may and may not do. You can carry out complicated string operations at compile time—no problem there. But returning a `std::string` that points to compile-time-allocated memory for runtime to keep using, that won't fly.

The exception becomes clear once you look at where the memory ends up: if the memory was released before compile-time evaluation finished, or was moved into storage that persists, it no longer counts. Conversely, as long as it still hangs on a compile-time allocation, we must not touch it at runtime.

We turned the do's and don'ts of compile-time `new` into an animation—press the step key to walk through it segment by segment:

<Anim id="constexpr-transient-alloc" />

Compiler support on this front is already in place: GCC 12+ (libstdc++) and Clang 15+ (libc++) fully support `constexpr` operations on `std::string`—construction, concatenation, and substrings are all covered. We can build strings at compile time, validate formats, and generate lookup tables, as long as we keep all the dynamic memory properly managed during compilation.

## Step 2 — Giving Custom Types a constexpr Constructor

### The Simplest Case: POD-like Types

You can loosely think of POD (Plain Old Data) as a family of C-style structs that contain "nothing but the data itself"—scalar types count as POD too. If your class is just an aggregate of data with no virtual functions and no dynamic allocation, adding a `constexpr` constructor is easy.

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

At this point `Color` is already a literal type. Look at its constructor: it simply hands each parameter over to a member through the initializer list—as direct as it gets.

### Constructors with Logic

A constructor can contain logic too, provided that logic stays within what `constexpr` permits. From C++14 onward, we can write loops, conditionals, and local variables inside a constructor.

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

Look at what it does: the constructor implements the conversion from decimal to BCD encoding. BCD stands for Binary-Coded Decimal, a representation commonly used on the hardware side, where each decimal digit is packed into 4 bits—so the BCD encoding of 42 happens to be exactly `0x42`: `4` and `2` each occupy 4 bits. The whole computation happens at compile time, and `kDec42`'s `bcd` member is written directly as `0x42`.

This pattern is especially useful in embedded development. We convert human-readable decimal values into the BCD encoding the hardware expects at compile time, and at runtime we use the precomputed value directly—not a single conversion instruction left to execute.

Zero overhead is not an empty claim—we verified it under GCC 15.2.1 (`-std=c++20 -O2`): when the code just returns `kDec42.bcd`, the assembly contains nothing but a single `movl $66, %eax`; the constant became an immediate. When runtime code genuinely needs the object's storage (taking the address of `kDec42`, say), the value lands in .rodata (the read-only data section), and accessing it costs a single memory-load instruction. Swap this for computing the BCD at runtime, and you would be running multiple division, shift, and loop instructions. The compile-time version truly delivers zero runtime overhead.

## Step 3 — constexpr Member Functions

Next, the member-function side: constructors are not the only things that can be marked `constexpr`—we can badge ordinary member functions the same way. And since C++14, a `constexpr` member function may modify an object's member variables (as long as the calling context allows it).

### A Compile-Time Complex Number Class

Let's write a complex number class that works at compile time. Why pick this one? Look at signal processing and you have the answer: complex arithmetic is everywhere. We meet it even more in the FFT (Fast Fourier Transform): every angular frequency corresponds to a unit complex number—what the trade calls the twiddle factor.

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

Look at the `Complex` we just wrote: it is a literal type through and through—the constructor is `constexpr`, and so are all the operators and member functions. We can do complex arithmetic at compile time and generate FFT twiddle-factor tables. The computed results we end up with are optimized by the compiler into constants: either embedded directly into the code or placed in the .rodata read-only data section, depending on the optimization level and how they are used.

We inspected the actual output under GCC 15.2.1 (`-std=c++20 -O2`): `kI_Squared` is placed in the .rodata section as a constant, and a single memory-load instruction is enough to access it. If you lay the twiddle factors out as an array (`kTwiddle` in the code above is just a single value), the whole array gets compiled into the binary in full, and runtime access carries no computational overhead at all. And once these values get inlined at their points of use, even the load instruction may be optimized away—the value becomes an immediate outright.

### Compile-Time Date Calculation

Here is another practical scenario, this one about dates. Plenty of protocols and time-related logic need to validate whether a date is legitimate, and we can move that validation wholesale into compile time.

```cpp
struct Date {
    int year;
    int month;
    int day;

    constexpr Date(int y, int m, int d) : year(y), month(m), day(d)
    {
        // Validate the date at compile time
        // If the date is invalid, trigger a compile error (by making the expression non-constant)
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

One spot deserves an extra glance from you: a `constexpr` constructor does not error out just because a value is "logically unreasonable". If we want invalid dates stopped at compile time, we have to do it ourselves. Either we actively trigger a compile-time error inside the constructor—`throw` will do, since in a `constexpr` context an exception is a compile error—or we check with `static_assert` combined with `is_valid()`.

### Compile-Time String Length

Member functions returning values that are usable at compile time are another important use of `constexpr`. Let's write a simple compile-time string wrapper class.

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

This `ConstString` of ours is essentially a simplified version of the `conststr` class from cppreference's official examples. It does not own the string data—all it holds is a pointer paired with a length. Will the storage the pointer aims at expire? One glance and we can relax: it accepts string literals, whose lifetime lasts as long as the program's—used up during compilation, still there at runtime. So for string operations at compile time, it is already sufficient.

## Step 4 — The Restrictions C++14 Lifted

Let's look back at one fact: that `while` loop in the `BcdDecimal` constructor could not have been written in C++11.

Back in C++11, the body of a `constexpr` constructor had to be empty; all initialization work could only be done through the member initializer list. No loops, no conditionals, no local variables—none of them were allowed. Construction logic that was even slightly complex got painful: to traverse an array or set different values by condition, you had to force your way around it with the ternary operator and recursive functions.

Wind the clock forward to C++14: the function body may now contain any statement that `constexpr` allows. Many compile-time classes that were simply beyond us before became real from that moment on.

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

Before C++20, literal types required the destructor to be trivial, which amounted to forbidding any cleanup work in the destructor. C++20 removed this restriction: you can now write `constexpr` destructors.

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

Mainstream compilers already support it fully in C++20: GCC 10+ and Clang 10+ have had it for a while, and MSVC 19.28+ caught up too. For most of our embedded scenarios, its main significance is letting standard containers such as `std::vector` and `std::string` take part in compile-time computation more completely. We can construct containers at compile time, manipulate elements, and then destroy them all—still at compile time.

> Looking ahead to C++23, the relaxations continue (P2448R2 is a standards-committee proposal number): `constexpr` functions no longer require their return type and parameter types to be literal types, and local variables of non-literal types, `goto` statements, and labels are now allowed as well. The weightier change is that a `constexpr` function template no longer requires every instantiation to be constant-evaluable. After this whole round of loosening, look back and hardly any restrictions remain on `constexpr` functions at the definition level. Of course, when we actually call (evaluate) these functions at compile time, we are still bound by the rules of constant-expression evaluation. What you end up with is just the one layer of "function bodies may be written more freely".

## Practical Application: Compile-Time Configuration in Embedded Systems

In embedded development, peripheral configuration is usually a pile of fixed parameters: baud rate, data bits, stop bits, parity scheme, and the like. We can use literal types to pack these configurations into compile-time constants.

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

All the validation and computation for `kDebugUart` and `kGpsUart` finishes at compile time. The day your hand slips and the baud rate becomes 0 or the data bits become 3, the `static_assert` stops the build on the spot. The baud-rate register value is precomputed as well—at runtime we just write it straight into the register.

## Where Things Tend to Go Wrong

### Getting Blocked by a Non-Trivial Destructor

If your class has a non-trivial destructor (because it manually manages a resource, say), it cannot qualify as a literal type before C++20. Even with a `constexpr` constructor, a destructor that is neither `constexpr` nor trivial still blocks compile-time use. The common workaround is to declare the destructor `= default` and let the compiler generate a trivial one—provided, of course, that your class genuinely needs no custom destruction logic; only then may we take this shortcut.

### `mutable` Members

A word of caution about `mutable` data members, too. The `mutable` members of a `constexpr` object are treated as modifiable during compile-time evaluation. But precisely because of that, compile-time evaluation fails in certain contexts, because `mutable` breaks the semantic assumption that "the object is fully determined at compile time".

### Virtual Functions and Virtual Base Classes

We have to take these apart—the rules are completely different. A class with virtual base classes cannot be a literal type up through C++23. GCC's compiler hint says this restriction will be lifted starting from C++26.

Virtual functions are another story: they do not strip away literal-type qualification. When checking this, we used `std::is_literal_type` as a probe. That trait has carried a deprecation mark since C++17, and C++23 removed it from the standard outright, but libstdc++ still keeps it—just right for the job. The measured result: a class with virtual functions, as long as it has a `constexpr` constructor and a trivial destructor, comes out `true` under all three of `-std=c++11/17/20`. The real trouble sits at the virtual-destructor end: a class with virtual functions mostly needs a virtual destructor as well, and a virtual destructor is necessarily non-trivial. Under `-std=c++17` we measured that even a class with `virtual ~DV() = default;` is not a literal type. Only from C++20 onward is a `= default` virtual destructor let through as `constexpr`.

The calling trouble follows right behind: before C++20, we could not make virtual function calls during constant evaluation. After C++20 (P1064, also a standards-committee proposal number), even virtual member functions can be marked `constexpr`.

If you genuinely need polymorphic calls at compile time, the option worth considering is CRTP (Curiously Recurring Template Pattern): the derived class passes itself as a template argument to the base class, pinning down who gets called at compile time—replacing the runtime dispatch of virtual functions.

We have gathered the literal-type constraints covered in this article into one checklist, with the qualifying on one side and the disqualified on the other:

![Literal type qualification checklist](./02-constexpr-ctor-layout.drawio)

## References

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: LiteralType requirement](https://en.cppreference.com/w/cpp/named_req/LiteralType)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
