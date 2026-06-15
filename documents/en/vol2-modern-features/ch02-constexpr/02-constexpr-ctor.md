---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Enable custom types to participate in compile-time computation, and understand
  the design constraints and evolution of literal types.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 2: constexpr 基础'
reading_time_minutes: 11
related:
- consteval 与 constinit
- 编译期计算实战
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
title: constexpr constructors and literal types
translation:
  engine: anthropic
  source: documents/vol2-modern-features/ch02-constexpr/02-constexpr-ctor.md
  source_hash: a8de6bf5dd8148d2a32f15ee4dea8aedc612ab95be646c3092c8c3bcca3c5a3c
  token_count: 3119
  translated_at: '2026-06-13T11:49:48.019532+00:00'
---
# `constexpr` Constructors and Literal Types

## Introduction

In the previous chapter, we discussed `constexpr` variables and `constexpr` functions, but all the examples were limited to scalar types—primitives like integers, floating-point numbers, and pointers. You might ask: Can I use custom classes at compile time too? For example, constructing a complex number object at compile time, or calculating a date in advance and using it directly at runtime?

The answer is yes, but with a prerequisite: your type must be a "literal type." This concept sounds a bit academic, but it is essentially a checklist of constraints that allows the compiler to understand and manipulate types during compilation. In this chapter, we will clarify what a literal type is, how to add `constexpr` constructors to custom types, and how these restrictions were gradually relaxed in C++14 and later.

## Step 1 — What is a Literal Type?

The name "literal type" can be confusing. It is not the same as a "literal" (like `42` or `3.14`). A literal type refers to a type that satisfies specific constraints—the compiler can fully construct, manipulate, and destroy objects of this type during compilation.

Specifically, a type is a literal type if it meets the following conditions: scalar types (arithmetic types, pointers, references, enumerations) are naturally literal types and require no extra effort; for class types, it needs to have a `constexpr` constructor (at least one, which can be a copy or move constructor), all non-static data members must themselves be literal types (or arrays thereof), and its destructor must either be trivial or, since C++20, `constexpr`.

In plain terms: the compiler needs to fully understand the memory layout and initial value of this type at compile time, without requiring runtime dynamic allocation, virtual function table lookups, or complex destruction logic.

```cpp
struct Point {
    float x, y;
    // Implicitly has a constexpr trivial constructor
    // and a constexpr trivial destructor.
};

constexpr Point p{1.0f, 2.0f}; // OK
```

The following is **not** a literal type:

```cpp
struct Buffer {
    int* data;
    size_t size;

    Buffer(size_t s) : size(s), data(new int[s]) {}
    ~Buffer() { delete[] data; }

    // Non-trivial destructor prevents this from being a literal type in C++11/14/17
    // (unless we make the destructor constexpr in C++20)
};
```

The problem with `Buffer` is that it manages dynamic memory. Before C++20, `new`/`delete` were not allowed in `constexpr` functions, so any type requiring dynamic allocation could not be used at compile time. C++20 relaxed this restriction—allowing `new`/`delete` in `constexpr` functions—but with a hard constraint: all memory allocated at compile time must be released before the end of the compile-time evaluation (it cannot leak into runtime). This means you can perform complex string manipulations at compile time, but you cannot return a `std::string` pointing to compile-time allocated memory to runtime (unless that memory has been freed or transferred to persistent storage).

In fact, GCC 15.2.1 and Clang 13+ fully support `std::string` and `std::vector` operations in `constexpr` contexts, including construction, concatenation, and substring operations. You can build strings, validate formats, and generate lookup tables at compile time, as long as all dynamic memory is correctly managed during compilation.

## Step 2 — Adding `constexpr` Constructors to Custom Types

### The Simplest Case: POD-like Types

If your class is just an aggregate of data, without virtual functions or dynamic allocation, adding a `constexpr` constructor is very straightforward.

```cpp
struct BCDValue {
    uint8_t value;

    constexpr BCDValue(uint8_t v) : value(v) {}
};
```

This is now a literal type. The constructor uses an initializer list to assign parameters to members, which is very direct.

### Constructors with Logic

Constructors can also contain logic—provided that logic falls within the rules allowed by `constexpr`. Since C++14, you can write loops, conditional statements, and local variables inside constructors.

```cpp
struct BCDValue {
    uint8_t value;

    // Converts decimal (0-99) to BCD at compile time
    constexpr BCDValue(int dec)
        : value(static_cast<uint8_t>((dec / 10) << 4 | (dec % 10))) {
        // Static assertion to ensure input range
        static_assert(dec >= 0 && dec <= 99, "Decimal value out of range");
    }
};

constexpr BCDValue seconds{45}; // Compile-time conversion: 45 -> 0x45
```

This code implements decimal to BCD encoding conversion within the constructor. The entire calculation happens at compile time, and the `value` member of `seconds` is directly written as `0x45`. This pattern is particularly useful in embedded development—you can convert human-readable decimal values to hardware-required BCD encoding at compile time, and use the pre-calculated value directly at runtime without any conversion instructions.

Let's verify this: under GCC 15.2.1 (`-O2`), accessing `seconds` results in assembly that is just a `mov` instruction loading a constant from the `.rodata` section, whereas calculating BCD at runtime requires multiple division, shift, and loop instructions. The compile-time version indeed achieves zero runtime overhead.

## Step 3 — `constexpr` Member Functions

Not only can constructors be `constexpr`, but ordinary member functions can be too. Furthermore, starting with C++14, `constexpr` member functions can modify an object's member variables (as long as the calling context allows).

### A Compile-Time Complex Number Class

Let's write a complex number class that can be used at compile time. This example is quite practical since complex arithmetic is ubiquitous in signal processing.

```cpp
struct Complex {
    double real, imag;

    constexpr Complex(double r = 0, double i = 0) : real(r), imag(i) {}

    constexpr Complex operator+(const Complex& other) const {
        return {real + other.real, imag + other.imag};
    }

    constexpr Complex operator*(const Complex& other) const {
        return {
            real * other.real - imag * other.imag,
            real * other.imag + imag * other.real
        };
    }
};

// Compile-time complex arithmetic
constexpr Complex c1{1.0, 2.0};
constexpr Complex c2{3.0, 4.0};
constexpr Complex c3 = c1 + c2; // Evaluated at compile time

// Generating FFT twiddle factors at compile time
constexpr Complex twiddle_factors[4] = {
    Complex{1.0, 0.0},
    Complex{0.0, 1.0},
    Complex{-1.0, 0.0},
    Complex{0.0, -1.0}
};
```

This `Complex` class is entirely a literal type. Its constructor is `constexpr`, and so are all operators and member functions. You can perform complex arithmetic at compile time, generate FFT twiddle factor tables—all these results are optimized by the compiler into constants, directly embedded in the code or placed in the `.rodata` read-only data section (depending on optimization level and usage).

For example, under GCC 15.2.1 (`-O3`), `c3` is placed in the `.rodata` section as a constant, and accessing it is just a single memory load instruction. The `twiddle_factors` array is fully compiled into the binary, and accessing it at runtime incurs no calculation overhead. If these values are inlined at the point of use, even the load instruction might be optimized away, becoming immediate values.

### Compile-Time Date Calculation

Another practical scenario is dates. Many protocols and time-related logic require validating the legality of a date. We can move this validation to compile time.

```cpp
struct Date {
    unsigned short year;
    unsigned char month;
    unsigned char day;

    constexpr Date(unsigned short y, unsigned char m, unsigned char d)
        : year(y), month(m), day(d) {
        // Compile-time validation
        if (m < 1 || m > 12) {
            throw "Invalid month"; // Exception in constexpr context is a compile error
        }
        // ... (leap year and day validation logic omitted)
    }
};

// constexpr Date d{2023, 13, 1}; // Compile error: Invalid month
```

Here is a key point: the `constexpr` constructor itself does not report an error just because the value is "logically unreasonable." You need to actively trigger a compile-time error in the constructor (e.g., using `throw`, where an exception in a `constexpr` context is a compile error), or use `if` combined with `static_assert` to check.

### Compile-Time String Length

Making member functions return compile-time usable values is also an important application of `constexpr`. For example, a simple compile-time string wrapper class.

```cpp
struct ConstexprString {
    const char* str;
    std::size_t len;

    template <std::size_t N>
    constexpr ConstexprString(const char (&s)[N]) : str(s), len(N - 1) {}

    constexpr std::size_t length() const { return len; }
    constexpr char operator[](std::size_t i) const { return str[i]; }
};

constexpr ConstexprString msg = "Hello";
static_assert(msg.length() == 5);
```

This `ConstexprString` is essentially a simplified version of the `std::string_view` class from the cppreference official examples. It doesn't own the string data, it just holds a pointer and a length, but it is sufficient to perform many string operations at compile time.

## Step 4 — Relaxations in C++14

As mentioned earlier, C++14 significantly relaxed the restrictions on `constexpr` constructors and member functions. Specifically for class types, the impact of these changes is:

In C++11, the function body of a `constexpr` constructor had to be empty—all initialization work could only be done through member initializer lists; loops, conditional statements, or local variables were not allowed. This meant that if your construction logic was slightly complex (e.g., needing to iterate over an array or set different values based on conditions), you had to find ways to use ternary operators and recursive functions to bypass the limitations.

After C++14, you can write any statement allowed by `constexpr` inside constructors. Local variables, `for` loops, `if` statements are all fine. This made many previously impossible compile-time classes a reality.

```cpp
// C++14 allows local variables and logic in constexpr constructors
struct LookupTable {
    int data[256];

    constexpr LookupTable() : data{} {
        for (int i = 0; i < 256; ++i) {
            data[i] = i * i; // Calculate squares at compile time
        }
    }
};

constexpr Table squares; // Fully constructed at compile time
```

## Step 5 — `constexpr` Destructors (C++20)

Before C++20, literal types required the destructor to be trivial. This meant you couldn't do any cleanup work in the destructor. This restriction was removed in C++20—you can write `constexpr` destructors.

```cpp
struct ManagedBuffer {
    int* data;
    std::size_t size;

    constexpr ManagedBuffer(std::size_t s) : size(s), data(new int[s]) {}

    constexpr ~ManagedBuffer() {
        delete[] data; // Cleanup at compile time
    }
};

// Usage in a constexpr context
constexpr auto create_buffer() {
    ManagedBuffer buf{10}; // Allocates memory
    // ... use buf ...
    return; // buf is destroyed, memory freed
}
```

This feature is fully supported by mainstream compilers in C++20. GCC 10+, Clang 10+, and MSVC 19.28+ all support `constexpr` destructors. For most embedded scenarios, the main significance of `constexpr` destructors is that standard containers like `std::vector` and `std::string` can participate more fully in compile-time computation—you can construct containers, manipulate elements, and destroy them at compile time.

It is worth mentioning that C++23 further relaxed `constexpr`: `constexpr` functions no longer require return types and parameter types to be literal types (P2448R2), and non-literal type local variables, `goto` statements, and labels are also allowed. This means starting from C++23, there are very few restrictions on defining `constexpr` functions. Of course, to actually call (evaluate) these functions at compile time, they are still subject to constant expression evaluation rules—you just have more freedom in writing the function body.

## Practical Application: Compile-Time Configuration in Embedded Systems

In embedded development, peripheral configuration is usually a bunch of fixed parameters—baud rate, data bits, stop bits, parity, etc. We can use literal types to package these configurations into compile-time constants.

```cpp
struct UARTConfig {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;

    constexpr UARTConfig(uint32_t br, uint8_t db, uint8_t sb, uint8_t p)
        : baud_rate(br), data_bits(db), stop_bits(sb), parity(p) {
        // Compile-time validation
        if (br == 0) throw "Baud rate cannot be zero";
        if (db < 5 || db > 9) throw "Invalid data bits";
    }

    // Calculate hardware register value at compile time
    constexpr uint32_t get_control_reg() const {
        return (1 << 0) | (data_bits << 12) | (parity << 9);
    }
};

// Compile-time configuration
constexpr UARTConfig uart_cfg{115200, 8, 1, 0};

// Runtime usage (just write the pre-calculated register value)
void init_uart() {
    UART->CTRL = uart_cfg.get_control_reg();
    // ...
}
```

`uart_cfg` and `get_control_reg` complete all validation and calculation at compile time. If someone changes the baud rate to 0 or data bits to 3, the `throw` statement will cause a compile-time explosion. The baud rate register value is also pre-calculated, so at runtime, we just write it directly to the register.

## Common Pitfalls

### Blocking by Non-Trivial Destructors

If your class has a non-trivial destructor (e.g., it manually manages resources), it cannot be a literal type before C++20. Even if your constructor is `constexpr`, if the destructor is not `constexpr` (or trivial), it will block compile-time usage. A common workaround is to declare the destructor as `= default`, letting the compiler generate a trivial destructor—provided your class indeed doesn't need custom destruction logic.

### `mutable` Members

`mutable` data members can lead to unexpected behavior. `mutable` members of a `constexpr` object are treated as modifiable during compile-time evaluation, but this can cause compile-time evaluation to fail in certain contexts (because `mutable` breaks the semantic assumption that "the object is fully determined at compile time").

### Virtual Functions and Virtual Base Classes

Classes with virtual functions or virtual base classes can never be literal types (at least up to the current standard). If you need to use a type hierarchy at compile time, consider using CRTP (Curiously Recurring Template Pattern) to replace virtual functions.

## Summary

In this chapter, we covered the definition and constraints of literal types, how to write `constexpr` constructors, the use of `constexpr` member functions, and the gradual relaxation of these restrictions in C++14/20/23. The key takeaway is: as long as your type's memory layout and lifetime can be fully determined at compile time, the compiler can construct and manipulate it then. Compile-time complex numbers, dates, strings, and configuration structures can all become literal types, thereby participating in more complex compile-time computations.

In the next chapter, we will introduce the `consteval` and `constinit` keywords added in C++20, and see how they precisely control compile-time evaluation behavior.

## Reference Resources

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: LiteralType requirement](https://en.cppreference.com/w/cpp/named_req/LiteralType)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
