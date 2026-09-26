---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the rules of implicit and explicit conversion in C++, master the use of static_cast, and steer clear of the classic type conversion pitfalls.
difficulty: beginner
order: 2
platform: host
prerequisites:
- Basic Data Types
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Type Conversion
translation:
  source: documents/vol1-fundamentals/ch01/02-type-conversion.md
  source_hash: bc52b0f3dd6f6e144d1b194013f655c400f7ef1f5a00f27b628913aca73706de
  translated_at: '2026-09-25T10:01:32+00:00'
  engine: anthropic
  token_count: 3000
---
# Type Conversion: The Decisions Your Compiler Quietly Makes Behind Your Back

After you've written a few lines of C++, you are bound to run into situations like this: an `int` that needs to become a `double`, a `double` that needs to be truncated into an `int`, or a signed number being compared against an unsigned one. **Type conversion is practically everywhere in real programs—and if you don't understand its rules, the compiler will quietly make decisions behind your back, and one late night you'll harvest a bug you can't make heads or tails of, plus endless phone calls from your boss.**

> Bugs around type conversion share one especially nasty property: under the default settings, they usually cause neither a compile error nor a crash—instead, they silently hand you a wrong calculation result. And then your frontend colleague files a complaint about your code. So my advice is: let's just treat warnings as errors. That's exactly how my CFbox project constrains its pipeline—to keep weird corner cases from blowing up into results we don't want.

## Implicit Conversion — The Compiler's Backroom Deals

Implicit conversion means the compiler decides "the types don't match here, but I know how to handle it," and performs the conversion for you automatically—no extra code required on your part. That sounds thoughtful, but if you don't know the rules, it's like an assistant who acts on their own initiative: well-meaning, and perfectly capable of botching the job.

### Integer Promotion and Arithmetic Conversion (Just Get a Rough Impression for Now)

C++ implicit conversion has a few core rules. Really!

The first is **integer promotion**: integer types smaller than `int` (`bool`, `char`, `short`, and friends) are automatically promoted to `int` when they take part in an operation. Add two `char`s together and the result type is `int`, not `char`—because on many CPUs `int` is the native arithmetic width, which makes it the most efficient.

The second is the **arithmetic conversions**: when two values of different types are combined in one operation, the compiler "leans toward the larger type." Add an `int` and a `double`, and the `int` is first converted to `double`; the result is a `double`. But in the other direction, assigning a `double` to an `int` truncates the fractional part—not rounded, just chopped clean off.

Let's walk through a combined example that runs past all of these implicit conversions:

```cpp
#include <iostream>

int main()
{
    // Assignment conversion: double -> int, the fractional part is simply truncated
    double pi = 3.14159;
    int truncated = pi;
    std::cout << "3.14159 -> int: " << truncated << std::endl;  // 3

    // Arithmetic conversion: int + double -> double
    int i = 5;
    double d = 2.5;
    auto result = i + d; // Not sure what the type is? Hover over the word auto and the IDE will tell you
    std::cout << "5 + 2.5 = " << result << " (double)" << std::endl;  // 7.5

    // Boolean conversion: zero -> false, non-zero -> true
    bool b1 = 42;   // true, prints as 1
    bool b2 = -3;   // true
    bool b3 = 0;    // false, prints as 0
    std::cout << "42->" << b1 << ", -3->" << b2 << ", 0->" << b3
              << std::endl;  // 1, 1, 0
    return 0;
}
```

## Classic Implicit Conversion Disasters

Knowing the rules is one thing; actually getting bitten by them is another. Let's look at two typical cases that show up constantly in real projects.

### The Collision of Signed and Unsigned

```cpp
int a = -1;
unsigned int b = a;  // signed to unsigned
// a = -1, b = 4294967295
```

The binary representation of `-1` is all `1`s (two's complement), so once it is interpreted as an unsigned integer it becomes `4294967295` (that is, `2^32 - 1`). The compiler won't say a single word to warn you. What's scarier: if you compare a signed value with an unsigned one, the compiler implicitly converts the signed value to unsigned for the comparison, and the result will leave you thoroughly confused.

> Comparing signed and unsigned numbers is a particularly prolific source of bugs. Say you compare an `int i` against `vector.size()` (which returns `size_t`, an unsigned type): if `i` is negative, it gets converted into an enormous unsigned number and the comparison result flips completely. **Many compilers warn about this once `-Wall -Wextra` are enabled, so make absolutely sure those warning options are turned on.**

### Overflow — The "Small Number" You Think You See Isn't Necessarily Small

```cpp
short s = 32767;   // the maximum value of short (assuming 16 bits)
s = s + 1;         // overflow! the result is -32768
```

The largest positive number a `short` can represent is `32767`; add `1` and it overflows. During the computation `s` is promoted to `int` and the intermediate result `32768` sits comfortably within the `int` range, but when the value is assigned back to `short` it gets truncated, and the result wraps around to `-32768`.

## C-Style Casts — Usable, but Don't

In C, explicit type conversion has two spellings: `(int)d` and `int(d)`. Both are still legal in C++, but they are a "brute-force" instrument—the compiler will almost never refuse you, no matter whether the conversion makes sense. C++ instead provides four named cast operators, each with a clearly defined purpose. Next up: the one we reach for most in day-to-day code.

## static_cast — The Workhorse of Everyday Casting

`static_cast` is the cast operator we use the most. Its syntax is `static_cast<target type>(expression)`. It checks at compile time, handles most "reasonable" conversions, and refuses the clearly unreasonable ones.

```cpp
#include <iostream>

int main()
{
    int i = 42;
    double d = static_cast<double>(i);       // int -> double, result 42
    double pi = 3.14159;
    int truncated = static_cast<int>(pi);    // double -> int, result 3

    std::cout << d << " " << truncated << std::endl;
    return 0;
}
```

You might ask: how is this different from plain assignment? The difference is **explicit intent**. A `static_cast` announces loudly to everyone reading the code, "a type conversion is genuinely needed here, and I know exactly what I'm doing," whereas an implicit conversion happens on the quiet. Another important difference is that `static_cast` performs compile-time checks—if you try to turn an `int*` into a `double*`, `static_cast` rejects it with a hard error, because no reasonable conversion path exists between those two pointer types.

## reinterpret_cast — Reinterpreting the Underlying Bit Pattern

Among the things `static_cast` cannot do, one big category is "treat a stretch of memory as another type." For instance, you receive a `void*` pointer and need to turn it back into an `int*` before you can dereference it; or you want to inspect the underlying bit pattern of a `float` as a `uint32_t`. These operations step outside the type system's safety guarantees, and the compiler has no way to check their validity for you—this is where `reinterpret_cast` comes in.

```cpp
#include <iostream>
#include <cstdint>

int main()
{
    // Scenario 1: converting between void* and a typed pointer
    int value = 100;
    void* pv = &value;
    int* pi = reinterpret_cast<int*>(pv);
    std::cout << *pi << std::endl;  // 100

    // Scenario 2: inspecting a float's underlying bit pattern
    float f = 1.0f;
    uint32_t bits = reinterpret_cast<uint32_t&>(f);
    // the IEEE 754 representation of 1.0f: 0x3f800000
    std::cout << std::hex << bits << std::endl;

    return 0;
}
```

The name `reinterpret_cast` says it all—"reinterpret." It doesn't change the underlying binary data; it just tells the compiler, "please view this memory as another type." That is precisely why it is also the most dangerous cast operator: use it wrong and you land directly in undefined behavior.

Most uses of `reinterpret_cast` are undefined behavior or implementation-defined behavior: cast an `int*` to a `double*` and dereference it, and with the alignment requirements and sizes failing to line up, the outcome is completely unpredictable. Its genuinely safe use cases are actually few—converting between `void*` and raw pointer types, low-level byte inspection through `unsigned char`, and a handful of serialization and hardware-register-access scenarios. We'll meet it more often in embedded development; host-side application code has essentially no use for it. A simple rule of thumb: **95% of the explicit casts in everyday development are covered by `static_cast`**—if you catch yourself reaching for `reinterpret_cast`, stop first and ask whether something has gone wrong in the design.

## const_cast and dynamic_cast (A Quick Introduction)

`const_cast` removes or adds `const` qualification—if the original object was `const` to begin with, forcibly stripping the `const` and writing to it is undefined behavior. `dynamic_cast` performs safe downcasts within an inheritance hierarchy, checking the object's real type at runtime; we'll discuss it in detail once we've covered object-oriented programming.

## Numerical Precision — Those Moments That Make You Doubt Life

Another big topic that type conversion drags in is numerical precision. Let's look at the three most classic scenarios.

### The Integer Division Trap

```cpp
int a = 5, b = 2;
double result = a / b;           // integer division! the result is 2, not 2.5
double correct = static_cast<double>(a) / b;  // correct: 5.0 / 2 = 2.5
```

Both operands of `a / b` are `int`, so integer division is performed and the result is an `int` too. The variable on the left may be a `double`, but that only converts the result `2` into `2.0`. Assignment happens after the operation—to get a floating-point result, you must convert at least one operand to a floating-point type before the division.

Integer division truncation is one of the most common beginner mistakes, especially when computing averages or percentages—as long as both sides of the division sign are integers, the result is guaranteed to be an integer; for a floating-point result, convert at least one of the numerator or the denominator to `double`.

### The Unreliability of Floating-Point Comparison

```cpp
#include <iostream>
#include <cmath>

int main()
{
    double a = 0.1 + 0.2;
    double b = 0.3;

    // Direct comparison: false! because 0.1+0.2 is actually stored as 0.30000000000000004
    std::cout << std::boolalpha << (a == b) << std::endl;  // false

    // The right approach: check whether the difference is small enough
    double epsilon = 1e-9;
    bool approx_equal = std::abs(a - b) < epsilon;
    std::cout << approx_equal << std::endl;  // true
    return 0;
}
```

`0.1 + 0.2` does not equal `0.3`—because `0.1` and `0.2` cannot be represented exactly in binary floating point, the `double` can only store an approximation. The right approach is to check whether the difference between the two floating-point numbers is smaller than a sufficiently tiny threshold (epsilon).

### Integer Overflow — The Consequences of Going Out of Range

```cpp
#include <climits>

int max_int = INT_MAX;     // 2147483647
int overflow = max_int + 1;  // undefined behavior! usually -2147483648

unsigned char uc = 255;
uc = uc + 1;               // well-defined wrap-around, becomes 0
```

Signed integer overflow is **undefined behavior** in C++—the compiler is allowed to do anything with such code. Most implementations do wrap around to a negative number, but you cannot rely on that behavior. Unsigned integer overflow, by contrast, is well-defined wrap-around behavior; in embedded development it is sometimes used deliberately (ring buffers, for example), but it has to be a conscious choice.

## A Combined Example — conversion.cpp

Now let's fold the preceding topics into one complete program, covering implicit conversion, `static_cast`, integer division, floating-point comparison, and overflow. I suggest you read through the code yourself first and predict each line's output, and only then look at the actual result.

```cpp
// conversion.cpp — a combined demonstration of type conversion
// Platform: host
// Standard: C++11

#include <iostream>
#include <cmath>
#include <climits>

int main()
{
    // 1. Implicit conversion: double -> int
    double price = 9.99;
    int rounded = price;
    std::cout << "[隐式转换] 9.99 -> int: " << rounded << std::endl;

    // 2. static_cast: explicit conversion
    int count = 7;
    double avg = static_cast<double>(count) / 2;
    std::cout << "[static_cast] 7 / 2 = " << avg << std::endl;

    // 3. The integer division trap
    int wrong = count / 2;
    std::cout << "[整数除法] 7 / 2 = " << wrong << std::endl;

    // 4. Signed and unsigned
    int neg = -1;
    unsigned int pos = static_cast<unsigned int>(neg);
    std::cout << "[有符号转无符号] -1 -> " << pos << std::endl;

    // 5. Floating-point precision
    double x = 0.1 + 0.2;
    double y = 0.3;
    std::cout << "[浮点比较] (0.1+0.2) == 0.3: "
              << (x == y ? "true" : "false") << std::endl;

    // 6. Safe floating-point comparison
    double epsilon = 1e-9;
    bool safe_eq = std::abs(x - y) < epsilon;
    std::cout << "[安全比较] approx equal: "
              << (safe_eq ? "true" : "false") << std::endl;

    // 7. Overflow
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

Reading line by line, every line of output corresponds to one of the rules covered earlier. Pay special attention to the contrast between lines 3 and 2—the same `7 / 2`, with or without `static_cast<double>`, gives completely different results.

## Run It Online

Run the combined example below online. Predict each line's output in your head first, then compare it with the actual result:

<OnlineCompilerDemo
  title="Type Conversion: A Combined Demonstration"
  source-path="code/examples/vol1/03_type_conversion.cpp"
  description="Watch the actual behavior of implicit conversion, static_cast, the integer division trap, floating-point precision, and overflow."
  allow-run
/>

## Try It Yourself

That's the theory done—now it's your turn on stage. The exercises below step up in difficulty one layer at a time; for each one, write the code, compile it, and run it yourself.

### Exercise 1: Predict the Output

Without compiling or running it, write down on paper what the following code prints, then verify with a compiler:

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

The third line actually prints `x <= y`—that's right, intuitively `10 > -1` should hold, but in a mixed signed/unsigned comparison `-1` is implicitly converted to unsigned (becoming `4294967295`), so what is really being compared is `10 > 4294967295`, and the answer is naturally `false`. If you predicted `x <= y`, congratulations—you've understood this trap. If you predicted `x > y`, go back to the section "The Collision of Signed and Unsigned".

### Exercise 2: Fix the Temperature Converter

The following code is meant to convert Celsius to Fahrenheit, but the result is sometimes wrong. Find the problem and fix it:

```cpp
#include <iostream>

int main()
{
    int celsius = 25;
    // Formula: F = C * 9 / 5 + 32
    int fahrenheit = celsius * 9 / 5 + 32;
    std::cout << celsius << " C = " << fahrenheit << " F" << std::endl;
    return 0;
}
```

Hint: try changing `celsius` to `26` and see whether `26 * 9 / 5` gives you `46.8` or `46`.

### Exercise 3: Write a Safe Temperature Converter

Write a complete temperature conversion program that reads a Celsius temperature from user input (decimals supported), converts it to Fahrenheit correctly, and prints it. Requirements: use the correct types and `static_cast`, and keep one decimal place in the output. Expected behavior:

```text
请输入摄氏温度: 36.5
36.5 C = 97.7 F
```

::: details Reference Answer

**main.cpp**

```cpp
#include <iostream>
#include <iomanip>

int main()
{
    double celsius = 0;
    std::cout << "请输入摄氏温度: ";
    std::cin >> celsius;
    double fahrenheit = celsius * 9 / 5 + 32;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << celsius << " C = " << fahrenheit << " F" << std::endl;
    return 0;
}
```

The requirements call out `static_cast` by name: since `celsius` is already declared as a `double`, `celsius * 9` promotes `9` to `double` before the multiplication, the whole expression is floating-point arithmetic, and `static_cast` never gets a turn on stage here. But if all you have is an `int` (say an upstream function hands you a whole number), you must write `static_cast<double>(celsius) * 9 / 5 + 32`—cast first, then multiply and divide—otherwise `(celsius * 9) / 5` still goes down the integer-division path and the decimals are simply lost.

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra main.cpp -o main && ./main
```

Run result:

```text
请输入摄氏温度: 36.5
36.5 C = 97.7 F
```

:::
