---
title: "Parameter Passing"
description: "Understand the differences between pass by value, pass by reference, and pass by const reference, and learn to choose the right passing style for each scenario"
chapter: 3
order: 2
difficulty: beginner
reading_time_minutes: 22
platform: host
prerequisites:
  - "Function Basics"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch03/02-pass-by-value-ref.md
  source_hash: 521b3e3777cb49c41c9a257726996c2be6cc72c710540fccae02284fec993f82
  translated_at: '2026-09-25T10:24:59+00:00'
  engine: anthropic
  token_count: 3400
---

# Parameter Passing: It's Not "Just Passing an Argument"

How data gets into a function and how results get back out directly determine a program's correctness and performance. You might think "it's just passing an argument, what's there to talk about"—but it is precisely these seemingly trivial details that create huge numbers of bugs and performance problems in real projects: copying a big object that had no business being copied tanks performance, or casually modifying the caller's data through a reference produces logic errors that are miserable to trace.

C++ has three core parameter passing mechanisms: pass by value, pass by reference, and pass by const reference. And here, our journey to get to know them begins.

## Pass by Value: The Function Gets a Copy

Pass by value is the most intuitive way to pass arguments: when the function is called, **the argument is copied**.

When the function is called, **the argument is copied**. When the function is called, **the argument is copied**. When the function is called, **the argument is copied**.

We do like to hammer this point home at this particular spot. Friends from the C world will smile knowingly: turns out C++ has this problem too. Yes.

The function body operates on that copy, and the original variable we passed in remains completely unaffected.

```cpp
#include <iostream>

void add_ten(int x)
{
    x += 10;
    std::cout << "函数内 x = " << x << std::endl;
}

int main()
{
    int value = 5;
    add_ten(value);
    std::cout << "函数外 value = " << value << std::endl;
    return 0;
}
```

Output:

```text
函数内 x = 15
函数外 value = 5
```

`value` is still 5, not one bit different—the parameter `x` in `add_ten` is a copy of `value`; we modified the copy, and the original variable came through unscratched. This isolation is often exactly what we want: modifications made inside the function don't leak to the outside.

But the cost of pass by value is just as obvious—every call copies. For fundamental types like `int` and `double` that take up only a few bytes, the copying overhead is negligible. But what if the argument we want to pass is a struct holding tens of thousands of elements?

```cpp
struct SensorData {
    int readings[10000];
    double timestamps[10000];
    char description[256];
};

void process(SensorData data)  // The entire struct gets copied
{
    // Process the data...
}
```

Every call to `process` makes the compiler copy the roughly 80 KB of data in `SensorData` in full. Call it frequently inside a loop and you get disaster-grade pointless copying. What's more, it may well not achieve the effect you wanted in the first place. **Passing non-fundamental objects by value is a rare sight.**

> But don't overdo it~ Some people say, I'll make my `int` a reference too, and my `double` while I'm at it... Let me tell you: doing that will in fact guarantee a performance regression (with optimizations off). Remember, it is for custom types that we suggest considering references. For fundamental types, pass by value boldly and with confidence—unless you genuinely want the changes applied to your parameter to be carried back out of the function. **We call this an in-parameter.**

## Pass by Reference: Operating Directly on the Original Data

The idea of pass by reference: no copying—the function gets its hands on the caller's original variable directly. Add `&` after the parameter type, and we've declared a reference parameter.

```cpp
void add_ten(int& x)
{
    x += 10;
}

int main()
{
    int value = 5;
    add_ten(value);
    // value is now 15
    return 0;
}
```

This time `value` becomes 15, because what we modified is the original: `x` is a reference to `value`, another name for `value` itself.

The most classic application of pass by reference is the `swap` function. In C you have to pass pointers; C++ has references, and our code comes out much cleaner:

```cpp
/// @brief Swap the values of two integers
/// @param a The first integer
/// @param b The second integer
void swap_values(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

int main()
{
    int x = 3;
    int y = 7;
    swap_values(x, y);
    std::cout << "x = " << x << ", y = " << y << std::endl;
    return 0;
}
```

Output:

```text
x = 7, y = 3
```

The swap worked. The calling syntax is perfectly natural, too—we don't need to take addresses and pass pointers the way C does.

But pass by reference also brings new constraints and pitfalls.

A non-const reference parameter can only bind to an lvalue—that is, a variable that has a name and an address. Literals and temporary values (rvalues) cannot be passed to a non-const reference. For instance, if our current `add_ten(int&)` is called as `add_ten(5)`, it fails to compile, because `5` is a literal with no memory address for the reference to bind to. By the same token, `swap_values(x, 3)` won't compile either: the numeric literal `3` cannot be overwritten, and the compiler stops it right there. If you ever run into a compile error like `cannot bind non-const lvalue reference to an rvalue`, this is most likely the issue.

## Pass by Const Reference: The Best of Both Worlds

The choice before us: pass by value is safe but pays a copying cost; pass by reference is efficient but can modify the original data. Is there a way that neither copies nor permits modification? There is—the const reference:

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
    // s += "!";  // Compile error: a const reference forbids modification
}
```

Look at `const std::string& s` in pieces: `&` means reference, so no copy happens; `const` means read-only, so the function cannot modify the original string through `s`. When you see `const` on the calling side, you know "this function won't touch my data"—the intent is crystal clear.

Const references have one more property we're about to put to use: they can bind to rvalues. The literals that a non-const reference refuses to take, a const reference takes them all:

```cpp
void print(const std::string& s);

print(std::string("hello"));  // OK: the const reference binds to a temporary object
print("world");               // OK: the const reference binds to an implicitly constructed temporary string
```

This makes `const T&` an extraordinarily flexible parameter type: it accepts lvalues and rvalues alike, avoids copies, and guarantees read-only access. We will reach for it again and again later on.

Looking back at the earlier example that copied the big struct, rewritten with a const reference:

```cpp
void process(const SensorData& data)  // Zero-copy, read-only access
{
    // Process the data...
}
```

The copying overhead is gone, `data` stays read-only inside the function, and there's no risk of accidentally modifying the data we passed in—the "best of both worlds" we spoke of earlier lands right here.

## How to Choose: A Decision Guide for Parameter Passing

Each of the three passing methods has its own home turf, so let's lay the decision rules out clearly. For fundamental types (`int`, `double`, `float`, pointers, and so on—usually no more than 8 bytes), just use pass by value. Copying these types costs almost nothing, pass by value is both safe and simple, and it is friendlier to compiler optimization. If you see someone write `void foo(const int& x)`, it's most likely over-optimization—passing a reference to an `int` is no faster than passing the `int` itself, and on some platforms it's actually slower (references are essentially pointers under the hood and require one extra level of indirection).

For larger or more complex types (`std::string`, `std::vector`, custom structs, and the like), if we only need to read the data without modifying it, use `const T&`; if we need to modify the caller's data (a `swap`, say, or filling in an output struct), use a non-const reference `T&`.

Let's summarize the decision rules in one table:

| Parameter type | Not modified | Needs modification |
|----------|--------|----------|
| Fundamental types | `T` (pass by value) | `T` (pass by value, then return it) |
| Non-trivial types | `const T&` | `T&` |

This rule applies in the vast majority of cases. Once you get to move semantics and perfect forwarding, you'll learn that finer-grained passing strategies exist (pass by value + move, for example), but at this stage, the table above is plenty to guide everyday coding.

## Return Values: How to Hand Results Back to the Caller

A function's return value also involves a choice of passing mechanism. In most cases, returning by value is simply the right call:

```cpp
std::string greet(const std::string& name)
{
    return "Hello, " + name + "!";
}
```

You might worry: doesn't returning a `std::string` incur a copy? In reality, modern C++ compilers perform two key optimizations: **RVO (Return Value Optimization) and NRVO (Named Return Value Optimization)**. Put simply, the compiler constructs the return value directly in the memory space the caller has reserved, skipping the intermediate copy or move. Since C++17, RVO is even guaranteed in certain cases. So `return "Hello, " + name + "!";` produces no extra string copy, and performance is nothing to worry about at all.

But if you try to return a reference to a local variable, things get dangerous:

```cpp
const int& get_value()
{
    int x = 42;
    return x;  // Returning a reference to a local variable — a dangling reference!
}
```

This code compiles, but at runtime it is undefined behavior. `x` is a local variable inside the function; once the function returns, `x`'s memory is reclaimed, and the reference we returned points at memory that no longer exists. Reading data through that reference might fetch garbage, might fetch an old value that "happens to still be there", or might segfault outright. The compiler won't complain (it's perfectly legal syntactically, though it may warn), which makes this bug extremely well hidden. The principle is simple: never return a reference or pointer to a local variable. Just return by value, and the compiler will optimize it for us.

## Output Parameters vs. Return Values

When a function needs to produce multiple results, old-style C code often uses reference parameters to "output" them—but at a call site like `divide(a, b, q, r)`, we can't tell inputs from outputs without reading the signature. Modern C++ prefers returning a struct directly:

```cpp
struct DivResult {
    int quotient;
    int remainder;
};

DivResult divide(int a, int b)
{
    return {a / b, a % b};
}
```

At the call site, `auto result = divide(a, b);` is clear at a glance, and `result.quotient` is far more readable than `result.first`. Output parameters still have their place when filling a large buffer with data, but most of the time we prefer return values.

## Hands-On Practice — passing.cpp

Let's tie this chapter's ideas together into one complete example program: it demonstrates the `swap` operation, a performance comparison of the different passing methods, and const references at work in string processing.

```cpp
// passing.cpp — Demonstrates pass by value, pass by reference, and pass by const reference

#include <iostream>
#include <string>
#include <chrono>

/// @brief Swap the values of two integers
void swap_values(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

struct BigData {
    int payload[4096];  // 16 KB
};

/// @brief Pass-by-value version: copies the entire BigData on every call
long sum_by_value(BigData data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief Const-reference version: zero copies
long sum_by_const_ref(const BigData& data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief Build a greeting; the const reference avoids a string copy
std::string build_greeting(const std::string& name)
{
    return "Hello, " + name + "! Welcome to Modern C++.";
}

int main()
{
    // swap demonstration
    int a = 10;
    int b = 20;
    std::cout << "交换前: a = " << a << ", b = " << b << std::endl;
    swap_values(a, b);
    std::cout << "交换后: a = " << a << ", b = " << b << std::endl;

    // performance comparison
    BigData data{};
    for (int i = 0; i < 4096; ++i) {
        data.payload[i] = i;
    }

    constexpr int kIterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    long result_value = 0;
    for (int i = 0; i < kIterations; ++i) {
        result_value = sum_by_value(data);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms_value = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - start)
                        .count();

    start = std::chrono::high_resolution_clock::now();
    long result_ref = 0;
    for (int i = 0; i < kIterations; ++i) {
        result_ref = sum_by_const_ref(data);
    }
    end = std::chrono::high_resolution_clock::now();
    auto ms_ref = std::chrono::duration_cast<std::chrono::milliseconds>(
                      end - start)
                      .count();

    std::cout << "\n--- 性能对比 (" << kIterations << " 次调用) ---"
              << std::endl;
    std::cout << "值传递: " << result_value
              << ", 耗时: " << ms_value << " ms" << std::endl;
    std::cout << "const引用: " << result_ref
              << ", 耗时: " << ms_ref << " ms" << std::endl;

    // string handling
    std::string name = "Charlie";
    std::cout << build_greeting(name) << std::endl;
    std::cout << build_greeting(std::string("World")) << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o passing passing.cpp
./passing
```

Expected output:

```text
交换前: a = 10, b = 20
交换后: a = 20, b = 10

--- 性能对比 (100000 次调用) ---
值传递: 8386560, 耗时: 680 ms
const引用: 8386560, 耗时: 190 ms
Hello, Charlie! Welcome to Modern C++.
Hello, World! Welcome to Modern C++.
```

Performance numbers vary with the machine and the optimization level, but the trend is consistent: pass by value copies 16 KB on every call, while the const-reference version dodges the copy and comes out several times faster. Note that we used `-O2`, and even then the compiler must obey the language semantics: if you tell it to copy, it copies.

The two calls to `build_greeting` also deserve our attention: the first passes the lvalue `name`, the second the temporary `std::string("World")`, and both are received through `const std::string&`—precisely the flexibility of const references.

## Run It Online

You can also run the parameter passing comparison online and observe the performance difference between pass by value and pass by const reference:

<OnlineCompilerDemo
  title="Parameter Passing Comparison: Pass by Value vs. Const Reference"
  source-path="code/examples/vol1/09_passing.cpp"
  description="Run online and compare the performance of pass-by-value copying a 16 KB struct versus the zero-copy const reference."
  allow-run
/>

## Try It Yourself

### Exercise 1: Implement swap

Write a `swap_values` function that swaps two `double`s, then write an overloaded version that swaps two `std::string`s. Use a `main` function to verify the results.

::: details Reference Solution

```cpp
#include <iostream>
#include <string>

void swap_values(double &first, double &second)
{
    double temporary = first;
    first = second;
    second = temporary;
}

void swap_values(std::string &first, std::string &second)
{
    std::string temporary = first;
    first = second;
    second = temporary;
}

int main()
{
    double first_number = 3.14;
    double second_number = 2.71;

    std::cout << "交换 double 前：" << first_number << ", "
              << second_number << '\n';
    swap_values(first_number, second_number);
    std::cout << "交换 double 后：" << first_number << ", "
              << second_number << '\n';

    std::string first_text = "hello";
    std::string second_text = "world";

    std::cout << "交换字符串前：" << first_text << ", "
              << second_text << '\n';
    swap_values(first_text, second_text);
    std::cout << "交换字符串后：" << first_text << ", "
              << second_text << '\n';

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Output:

```text
交换 double 前：3.14, 2.71
交换 double 后：2.71, 3.14
交换字符串前：hello, world
交换字符串后：world, hello
```

:::

### Exercise 2: Processing a Large Struct Efficiently

Define a struct `Measurement` that contains an array of at least 1000 `double` elements, then write two functions: one computes the average with pass by value, the other with pass by const reference. Time them separately and compare performance.

::: details Reference Solution

```cpp
#include <chrono>
#include <iostream>

#define kValueCount 1000
#define kIterations 100000

struct Measurement
{
    double values[kValueCount];
};

double average_by_value(Measurement measurement);
double average_by_const_ref(const Measurement &measurement);

int main()
{

    Measurement measurement{};

    for (int i = 0; i < kValueCount; ++i)
    {
        measurement.values[i] = static_cast<double>(i + 1);
    }

    double value_average = 0.0;

    const auto value_start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        value_average = average_by_value(measurement);
    }
    const auto value_end = std::chrono::steady_clock::now();
    const auto value_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            value_end - value_start)
            .count();
    double const_ref_average = 0.0;
    const auto const_ref_start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        const_ref_average = average_by_const_ref(measurement);
    }
    const auto const_ref_end = std::chrono::steady_clock::now();
    const auto const_ref_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            const_ref_end - const_ref_start)
            .count();
    const double expected_average =
        (1.0 + static_cast<double>(kValueCount)) / 2.0;
    std::cout << "Measurement 大小: " << sizeof(Measurement) << " 字节\n";
    std::cout << "值传递平均值: " << value_average
              << "（应为 " << expected_average << "）\n";
    std::cout << "const 引用平均值: " << const_ref_average
              << "（应为 " << expected_average << "）\n";
    std::cout << "值传递耗时: " << value_time << " 微秒\n";
    std::cout << "const 引用耗时: " << const_ref_time << " 微秒\n";
    if (const_ref_time > 0)
    {
        std::cout << "耗时比值（值传递 / const 引用）: "
                  << static_cast<double>(value_time) /
                         static_cast<double>(const_ref_time)
                  << "x\n";
    }

    return 0;
}

double average_by_value(Measurement measurement)
{
    double sum = 0.0;
    for (double value : measurement.values)
    {
        sum += value;
    }
    return sum / static_cast<double>(kValueCount);
}

double average_by_const_ref(const Measurement &measurement)
{
    double sum = 0.0;
    for (double value : measurement.values)
    {
        sum += value;
    }
    return sum / static_cast<double>(kValueCount);
}
```

Compile and run:

```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
Measurement 大小: 8000 字节
值传递平均值: 500.5（应为 500.5）
const 引用平均值: 500.5（应为 500.5）
值传递耗时: 264553 微秒
const 引用耗时: 253918 微秒
耗时比值（值传递 / const 引用）: 1.04188x
```

In this benchmark, the by-value parameter copies the complete `Measurement` (1000 `double`s) on every call, while `const Measurement&` passes only a reference and copies no object. The timings in the example are from one representative run; the exact values depend on the compiler, the hardware, and system scheduling. When we rerun it, we should focus on the overall trend rather than any single fixed number.

:::

### Exercise 3: Fix the Dangling Reference

What's wrong with the following code? Find the bug and fix it.

```cpp
const std::string& get_prefix()
{
    std::string prefix = "user_";
    return prefix;
}

int main()
{
    std::string name = get_prefix() + "admin";
    std::cout << name << std::endl;
    return 0;
}
```

Hint: think about what happens to the local variable `prefix` after the function returns.

::: details Reference Solution

```cpp
#include <iostream>
#include <string>

std::string get_prefix()
{
    std::string prefix = "user_";
    return prefix;
}

int main()
{
    std::string name = get_prefix() + "admin";
    std::cout << name << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Output:

```text
user_admin
```

In the original code, `prefix` is an ordinary local variable that exists only while `get_prefix` executes. Its lifetime ends when the function returns, so the `const std::string&` we receive points to an already-destroyed object, and any later read through that reference is undefined behavior.

The fix is to make `get_prefix` return by value. `prefix` remains an ordinary local variable that exists only during the function's execution; `return prefix` constructs and returns an independent `std::string` object, and what the caller receives is that return value rather than a reference to a local, so no dangling reference survives the function returning. At the same time, drop the `const` on the return type so that, if the compiler doesn't apply NRVO, it won't stand in the way of moving the object. The move constructor's signature is typically `std::string(std::string&&)`.

:::
