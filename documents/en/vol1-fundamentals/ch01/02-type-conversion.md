---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand C++ implicit and explicit conversion rules, master the use
  of `static_cast`, and avoid classic type-casting pitfalls.
difficulty: beginner
order: 2
platform: host
prerequisites:
- 基本数据类型
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Type Conversion
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch01/02-type-conversion.md
  source_hash: d5116402dce2ac13fa1175892d2bd7244cce4ab8cb5fc33dd72d86392b81badc
  token_count: 2297
  translated_at: '2026-05-26T10:42:40.481800+00:00'
---
# Type Conversion

After writing just a few lines of C++, you will inevitably run into this situation: a `float` needs to become an `int`, a `long` needs to be truncated to a `short`, or a signed number is being compared with an unsigned number. Type conversions are virtually everywhere in real programs—and if you don't understand the rules, the compiler will quietly make decisions for you behind the scenes, and you will end up with a completely baffling bug late at night.

In this chapter, we will thoroughly clarify the rules of type conversion: when the compiler automatically converts for you, when you need to explicitly specify it, and how to avoid those classic precision traps.

> ⚠️ **Warning**: Bugs related to type conversion have a particularly nasty trait—by default, they often don't cause compilation errors or crash the program. Instead, they silently produce incorrect calculation results. Therefore, we recommend treating warnings as errors. Our CFbox project enforces this in its pipeline to prevent unexpected corner cases from producing undesirable results.

## Implicit Conversion—The Compiler's Hidden Operations

Implicit conversion is when the compiler decides, "The types don't match here, but I know how to handle it," and automatically performs the conversion without you writing any extra code. This sounds thoughtful, but if you don't know the rules, it acts like an overzealous assistant whose good intentions lead to bad outcomes.

### Integer Promotion and Arithmetic Conversions

C++ implicit conversion has a few core rules. The first is **integer promotion**: integer types smaller than `int` (`char`, `int8_t`, `bool`, etc.) are automatically promoted to `int` when participating in operations. For example, when two `int8_t` values are added together, the result type is `int` rather than `int8_t`—because on many CPUs, `int` is the native computation width and offers the best efficiency.

The second rule is **arithmetic conversion**: when two values of different types are used together in an operation, the compiler "promotes to the larger type." When adding an `int` and a `double`, the `int` is first converted to a `double`, and the result is a `double`. Conversely, assigning a `double` to an `int` truncates the fractional part—it does not round, it simply chops it off.

Let's look at a comprehensive example that walks through these types of implicit conversions:

```cpp
#include <iostream>

int main()
{
    // 赋值转换：double -> int，小数部分直接截断
    double pi = 3.14159;
    int truncated = pi;
    std::cout << "3.14159 -> int: " << truncated << std::endl;  // 3

    // 算术转换：int + double -> double
    int i = 5;
    double d = 2.5;
    auto result = i + d; // 不知道啥类型，鼠标hover到auto这个单词上，IDE会提示你的
    std::cout << "5 + 2.5 = " << result << " (double)" << std::endl;  // 7.5

    // 布尔转换：零 -> false，非零 -> true
    bool b1 = 42;   // true，输出为 1
    bool b2 = -3;   // true
    bool b3 = 0;    // false，输出为 0
    std::cout << "42->" << b1 << ", -3->" << b2 << ", 0->" << b3
              << std::endl;  // 1, 1, 0
    return 0;
}
```

## Classic Pitfalls of Implicit Conversion

Understanding the rules is one thing; actually getting burned by them is another. Let's look at two typical cases that appear frequently in real projects.

### Signed and Unsigned Collisions

```cpp
int a = -1;
unsigned int b = a;  // 有符号转无符号
// a = -1, b = 4294967295
```

The binary representation of `-1` is all `1`s (in two's complement), which, when interpreted as an unsigned integer, becomes `4294967295` (i.e., `UINT_MAX`). The compiler won't say a word to you. What's even more terrifying is that if you compare a signed number with an unsigned number, the compiler will implicitly convert the signed number to an unsigned number for the comparison, and the result will leave you thoroughly confused.

> ⚠️ **Warning**: Comparing signed and unsigned numbers is a particularly common source of bugs. For example, if you use an `int` to compare against `std::vector::size()` (which returns `size_t`, an unsigned type), and the `int` is negative, it will be converted into a massive unsigned number, completely reversing the comparison result. Many compilers will warn about this when `-Wsign-compare` is enabled, so make sure to turn on these warning flags.

### Overflow—Your "Small Number" Might Not Be So Small

```cpp
short s = 32767;   // short 的最大值（假设 16 位）
s = s + 1;         // 溢出！输出 -32768
```

The maximum positive value a `uint8_t` can represent is `255`, and adding `1` to that causes an overflow. Even though `1` is promoted to `int` during the calculation and the intermediate result `256` falls within the `int` range, truncation occurs when assigning back to the `uint8_t`, causing the result to wrap around to `0`.

## C-Style Casts—Valid, But Don't Use Them

In C, explicit type conversions have two syntaxes: `(int)x` and `int(x)`. Both remain legal in C++, but they are a "brute-force" approach—the compiler will almost never reject you, regardless of whether the conversion makes sense. C++ provides four named cast operators, each with a clear purpose. Let's look at the one we use most often in daily practice.

## static_cast—The Workhorse of Everyday Casting

`static_cast` is the cast operator we use the most. Its syntax is `static_cast<T>(expr)`. It performs checks at compile time, can handle most "reasonable" conversions, and rejects obviously invalid operations.

```cpp
#include <iostream>

int main()
{
    int i = 42;
    double d = static_cast<double>(i);       // int -> double，输出 42
    double pi = 3.14159;
    int truncated = static_cast<int>(pi);    // double -> int，输出 3

    std::cout << d << " " << truncated << std::endl;
    return 0;
}
```

You might ask: what's the difference between this and a direct assignment? The difference lies in **clear intent**. `static_cast` loudly tells anyone reading the code, "A type conversion is genuinely needed here, and I know exactly what I'm doing," whereas an implicit conversion happens silently. Another important distinction is that `static_cast` performs compile-time checks—if you try to cast a `Foo*` to a `Bar*`, `static_cast` will outright refuse with an error, because no reasonable conversion path exists between these two pointer types.

## reinterpret_cast—Reinterpreting the Underlying Bit Pattern

Among the things `static_cast` cannot do is a large category of "treating a block of memory as a different type." For example, if you receive a `void*` pointer, you need to cast it back to an `int*` before you can dereference it; or you might need to look at the underlying bit pattern of a `float` as a `uint32_t`. These operations go beyond the safety guarantees of the type system, and the compiler cannot check their validity for you—this is where `reinterpret_cast` comes in.

```cpp
#include <iostream>
#include <cstdint>

int main()
{
    // 场景一：void* 和类型指针之间的转换
    int value = 100;
    void* pv = &value;
    int* pi = reinterpret_cast<int*>(pv);
    std::cout << *pi << std::endl;  // 100

    // 场景二：查看浮点数的底层位模式
    float f = 1.0f;
    uint32_t bits = reinterpret_cast<uint32_t&>(f);
    // 1.0f 的 IEEE 754 表示：0x3f800000
    std::cout << std::hex << bits << std::endl;

    return 0;
}
```

The name `reinterpret_cast` says it all—"reinterpret." It does not change the underlying binary data; it merely tells the compiler, "Please treat this block of memory as a different type." Because of this, it is also the most dangerous cast operator—using it incorrectly leads directly to undefined behavior.

> ⚠️ **Warning**: Many uses of `reinterpret_cast` result in undefined behavior or implementation-defined behavior. For instance, casting a `float*` to an `int*` and then dereferencing it yields completely unpredictable results due to differing alignment requirements and sizes. Its truly safe use cases are actually quite rare: converting between `std::uintptr_t` and raw pointer types, observing underlying bytes via `std::byte`, and certain serialization and hardware register access scenarios. We will encounter it more frequently in embedded development, but it is basically unnecessary in host-side application code. A simple rule of thumb: **95% of explicit casts in daily development can be handled with `static_cast`**. If you find yourself reaching for `reinterpret_cast`, stop and think about whether there's a flaw in your design.

## const_cast and dynamic_cast (Brief Overview)

`const_cast` is used to remove or add `const` qualification—if the original object is inherently `const`, forcibly removing `const` to write to it is undefined behavior. `dynamic_cast` is used for safe downcasting in inheritance hierarchies and checks the object's true type at runtime. We will discuss it in detail after we cover object-oriented programming.

## Numerical Precision—Those Moments That Make You Doubt Your Sanity

Another major topic brought up by type conversion is numerical precision. Here we will look at three classic scenarios.

### The Trap of Integer Division

```cpp
int a = 5, b = 2;
double result = a / b;           // 整数除法！结果是 2，不是 2.5
double correct = static_cast<double>(a) / b;  // 正确：5.0 / 2 = 2.5
```

Both operands of `5 / 2` are `int`, so integer division is performed, and the result is also an `int`. Even though the variable on the left is a `double`, that simply converts the result `2` into a `2.0`. The assignment happens after the operation—if you want a floating-point result, you must convert at least one operand to a floating-point type before the division.

> ⚠️ **Warning**: Integer division truncation is one of the most common mistakes beginners make, especially when calculating averages or percentages. Remember: as long as both sides of the division operator are integers, the result will always be an integer. To get a floating-point result, convert at least the numerator or the denominator to a `double`.

### The Unreliability of Floating-Point Comparison

```cpp
#include <iostream>
#include <cmath>

int main()
{
    double a = 0.1 + 0.2;
    double b = 0.3;

    // 直接比较：false！因为 0.1+0.2 实际存储为 0.30000000000000004
    std::cout << std::boolalpha << (a == b) << std::endl;  // false

    // 正确做法：判断差值是否足够小
    double epsilon = 1e-9;
    bool approx_equal = std::abs(a - b) < epsilon;
    std::cout << approx_equal << std::endl;  // true
    return 0;
}
```

`0.1 + 0.2` does not equal `0.3`—because `0.1` and `0.2` cannot be represented exactly in binary floating-point, `0.1 + 0.2` can only be stored as an approximation. The correct approach is to check whether the difference between two floating-point numbers is less than a sufficiently small threshold (epsilon).

### Integer Overflow—The Consequences of Going Out of Range

```cpp
#include <climits>

int max_int = INT_MAX;     // 2147483647
int overflow = max_int + 1;  // 未定义行为！通常是 -2147483648

unsigned char uc = 255;
uc = uc + 1;               // 明确定义的回绕，变成 0
```

Signed integer overflow is **undefined behavior** in C++—the compiler can do anything with such code. Although most implementations will wrap around to a negative number, you cannot rely on this behavior. Unsigned integer overflow, on the other hand, is a well-defined wraparound behavior. It is sometimes used intentionally in embedded development (such as for ring buffers), but it must be a conscious decision.

## Comprehensive Example—conversion.cpp

Now let's integrate the concepts we've covered into a complete program, encompassing implicit conversion, `static_cast`, integer division, floating-point comparison, and overflow. We recommend reading through the code first and predicting the output of each line, then checking the actual results.

```cpp
// conversion.cpp —— 类型转换综合演示
// Platform: host
// Standard: C++11

#include <iostream>
#include <cmath>
#include <climits>

int main()
{
    // 1. 隐式转换：double -> int
    double price = 9.99;
    int rounded = price;
    std::cout << "[隐式转换] 9.99 -> int: " << rounded << std::endl;

    // 2. static_cast：显式转换
    int count = 7;
    double avg = static_cast<double>(count) / 2;
    std::cout << "[static_cast] 7 / 2 = " << avg << std::endl;

    // 3. 整数除法陷阱
    int wrong = count / 2;
    std::cout << "[整数除法] 7 / 2 = " << wrong << std::endl;

    // 4. 有符号与无符号
    int neg = -1;
    unsigned int pos = static_cast<unsigned int>(neg);
    std::cout << "[有符号转无符号] -1 -> " << pos << std::endl;

    // 5. 浮点精度
    double x = 0.1 + 0.2;
    double y = 0.3;
    std::cout << "[浮点比较] (0.1+0.2) == 0.3: "
              << (x == y ? "true" : "false") << std::endl;

    // 6. 安全的浮点比较
    double epsilon = 1e-9;
    bool safe_eq = std::abs(x - y) < epsilon;
    std::cout << "[安全比较] approx equal: "
              << (safe_eq ? "true" : "false") << std::endl;

    // 7. 溢出
    int big = INT_MAX;
    std::cout << "[溢出] INT_MAX = " << big
              << ", +1 = " << big + 1 << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -Wall -Wextra -o conversion conversion.cpp
./conversion
```

```text
[隐式转换] 9.99 -> int: 9
[static_cast] 7 / 2 = 3.5
[整数除法] 7 / 2 = 3
[有符号转无符号] -1 -> 4294967295
[浮点比较] (0.1+0.2) == 0.3: false
[安全比较] approx equal: true
[溢出] INT_MAX = 2147483647, +1 = -2147483648
```

Looking through it line by line, each output corresponds to one of the rules discussed earlier. Pay special attention to the comparison between line 3 and line 2—the same `-1` yields completely different results depending on whether `static_cast` is used.

## Run Online

Run the comprehensive example below online. Predict each line of output in your head first, then compare it with the actual result:

<OnlineCompilerDemo
  title="Type Conversion Comprehensive Demo"
  source-path="code/examples/vol1/03_type_conversion.cpp"
  description="Observe the actual behavior of implicit conversion, static_cast, integer division traps, floating-point precision, and overflow."
  allow-run
/>

## Try It Yourself

Now that the theory is covered, it's your turn. The following exercises build upon each other progressively. We recommend writing, compiling, and running each one yourself.

### Exercise 1: Predict the Output

Without compiling or running, write down the output of the following code on paper, then verify it with a compiler:

```cpp
#include <iostream>

int main()
{
    int a = 10;
    int b = 3;
    double c = a / b;
    double d = static_cast<double>(a) / b;

    std::cout << c << std::endl;
    std::cout << d << std::endl;

    unsigned int x = 10;
    int y = -1;
    std::cout << (x > y ? "x > y" : "x <= y") << std::endl;

    return 0;
}
```

The actual output of the third line is `false`—that's right, intuitively `-1 < 1u` should hold true, but when mixing signed and unsigned numbers in a comparison, `-1` is implicitly converted to an unsigned number (becoming `4294967295`), so the actual comparison is `4294967295 < 1`, which naturally evaluates to `false`. If you predicted `false`, congratulations, you already understand this trap; if you predicted `true`, go back and reread the "Signed and Unsigned Collisions" section.

### Exercise 2: Fix the Temperature Converter

The following code is intended to convert Celsius to Fahrenheit, but the results are sometimes incorrect. Find the problem and fix it:

```cpp
#include <iostream>

int main()
{
    int celsius = 25;
    // 公式：F = C * 9 / 5 + 32
    int fahrenheit = celsius * 9 / 5 + 32;
    std::cout << celsius << " C = " << fahrenheit << " F" << std::endl;
    return 0;
}
```

Hint: Try changing `int celsius` to `double celsius`, and see whether `5 / 9` yields `0` or `0.555...`.

### Exercise 3: Write a Safe Temperature Converter

Write a complete temperature conversion program that reads a Celsius temperature from user input (supporting decimals), correctly converts it to Fahrenheit, and prints the result. You must use the correct types and `static_cast`, and format the output to one decimal place. Expected behavior:

```text
请输入摄氏温度: 36.5
36.5 C = 97.7 F
```

## Summary

In this chapter, we walked through C++'s type conversion mechanisms. Implicit conversions operate silently behind the scenes in the compiler, covering integer promotion, arithmetic conversions, assignment conversions, and boolean conversions—when you don't understand the rules, they are an invisible source of bugs. `static_cast` is the workhorse for everyday casting, offering better safety and clearer intent than C-style casts. On the numerical precision front, integer division truncation, the inability to directly compare floating-point numbers, and integer overflow are all high-frequency traps.

Keep a few core principles in mind: when both sides of a division are integers, the result is always an integer; never compare floating-point numbers with `==`—use the difference against an epsilon to determine approximate equality; and be extra careful when mixing signed and unsigned arithmetic, making sure to enable compiler warnings. In the next chapter, we will learn the basics of `const`—how to make the compiler help us enforce the bottom line of "values that shouldn't change."
