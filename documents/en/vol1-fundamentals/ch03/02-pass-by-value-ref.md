---
title: Parameter passing method
description: Understand the differences between pass-by-value, pass-by-reference,
  and pass-by-const-reference, and learn to choose the correct parameter passing method
  for different scenarios.
chapter: 3
order: 2
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- 函数基础
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
---
# Parameter Passing

How data "enters" a function and how the processed result "comes out" directly determine a program's correctness and performance. You might think "it's just passing a parameter, what's there to discuss?" but it's precisely these seemingly trivial details that create a massive number of bugs and performance issues in real-world projects—copying a large object that shouldn't be copied causes performance to plummet, or accidentally modifying the caller's data through a reference leads to hard-to-trace logic errors.

In this chapter, we will thoroughly understand the three core parameter passing methods in C++: pass by value, pass by reference, and pass by const reference. It's not complicated, but we must nail down the fundamentals.

## Pass by Value — The Function Gets a Copy

Pass by value is the most intuitive way to pass parameters: when a function is called, the actual argument is copied, the function body operates on this copy, and the original variable remains completely unaffected.

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

`value` is still 5, completely unchanged — `add_ten`'s parameter `x` is a copy of `value`, so we are modifying the copy, leaving the original variable perfectly intact. This isolation is often exactly what we want: modifications inside the function don't leak to the outside.

But the cost of pass by value is also obvious — every call requires a copy. For basic types like `int` and `double` that are only a few bytes, the copying overhead is negligible. But what if the parameter is a struct containing tens of thousands of elements?

```cpp
struct SensorData {
    int readings[10000];
    double timestamps[10000];
    char description[256];
};

void process(SensorData data)  // 整个结构体被拷贝一份
{
    // 处理数据...
}
```

Every time we call `process`, the compiler has to completely copy roughly 80 KB of data from `SensorData`. Calling this frequently in a loop? That's a disaster of pointless copying.

> **Pitfall Warning**: Pass by value won't cause logic errors, but it can cause performance disasters. When a function receives a large object as a value parameter and is called frequently in a hot loop, a performance issue is almost guaranteed.

## Pass by Reference — Directly Operating on Original Data

The core idea of pass by reference is: no copying, let the function directly access the caller's original variable. Adding `&` after the parameter type declares a reference parameter.

```cpp
void add_ten(int& x)
{
    x += 10;
}

int main()
{
    int value = 5;
    add_ten(value);
    // value 现在是 15
    return 0;
}
```

This time `value` becomes 15. `x` is a reference to `value` — not a copy, but another name for `value` itself.

The most classic use case for pass by reference is a `swap` function. In C, we had to pass pointers; with C++ references, the code is much cleaner:

```cpp
/// @brief 交换两个整数的值
/// @param a 第一个整数
/// @param b 第二个整数
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

The swap succeeded. Note that the calling syntax is very natural — no need to take addresses and pass pointers like in C.

But pass by reference also brings new constraints and pitfalls.

> **Pitfall Warning**: A non-const reference parameter can only bind to an lvalue — that is, a variable with a name and an address. Literals and temporary values (rvalues) cannot be passed to a non-const reference. For example, `add_ten(5)` will result in a compilation error because `5` is a literal with no memory address for the reference to bind to. Similarly, `swap_values(x, 3)` won't compile — you can't swap a numeric literal somewhere else. If you see a compilation error like `cannot bind non-const lvalue reference to an rvalue`, it's most likely this issue.

## Pass by Const Reference — The Best of Both Worlds

Pass by value is safe but has copying overhead; pass by reference is efficient but allows modifying the original data. Is there a way to avoid copying while also preventing modification? Yes — const reference:

```cpp
void print(const std::string& s)
{
    std::cout << s << std::endl;
    // s += "!";  // 编译错误，const 引用不允许修改
}
```

`const std::string& s` does two things: `&` means it's a reference, so no copy occurs; `const` means it's read-only, so the function cannot modify the original string through `s`. When a caller sees `const`, they know "this function won't touch my data," making the intent very clear.

Const reference has another important characteristic: it can bind to an rvalue. A non-const reference cannot bind to a literal, but a const reference can:

```cpp
void print(const std::string& s);

print(std::string("hello"));  // OK：const 引用绑定到临时对象
print("world");               // OK：const 引用绑定到隐式构造的临时 string
```

This makes `const T&` an extremely flexible parameter type — it can accept both lvalues and rvalues, while avoiding copies and guaranteeing read-only access.

Let's look back at the earlier example of copying a large struct, and rewrite it using const reference:

```cpp
void process(const SensorData& data)  // 零拷贝，只读访问
{
    // 处理数据...
}
```

The copying overhead is gone, and `data` is read-only inside the function, preventing accidental modification of the caller's data. This is the "best of both worlds" we mentioned earlier.

## How to Choose — A Decision Guide for Parameter Passing

Each of the three parameter passing methods has its own applicable scenarios. Let's clarify the decision rules. For basic types (`int`, `double`, `float`, pointers, etc., typically no larger than 8 bytes), use pass by value directly. The copying cost for these types is extremely low, pass by value is both safe and simple, and it's more friendly to compiler optimizations. If you see someone write `void foo(const int& x)`, it's probably over-optimization — passing a `int` by reference is not faster than passing the `int` itself, and on some platforms, it's actually slower (since references are essentially implemented as pointers, requiring an extra level of indirection).

For larger or more complex types (`std::string`, `std::vector`, custom structs, etc.), if the function only reads the data without modifying it, use `const T&`. If the function needs to modify the caller's data (such as `swap`, or filling an output struct), use a non-const reference `T&`.

Summarized in a table:

| Parameter Type | No Modification Needed | Modification Needed |
|----------|--------|----------|
| Basic types | T (pass by value) | T (pass by value, then return) |
| Non-trivial types | `const T&` | `T&` |

This rule applies in the vast majority of cases. Once you learn move semantics and perfect forwarding, you'll know there are even more refined parameter passing strategies (like pass by value + move), but at this stage, the table above is sufficient to guide your daily coding.

## Return Values — How to Hand Results Back to the Caller

Function return values also involve choosing a passing method. In most cases, simply returning by value is the right choice:

```cpp
std::string greet(const std::string& name)
{
    return "Hello, " + name + "!";
}
```

You might worry: won't returning a `std::string` cause a copy? In reality, modern C++ compilers perform two key optimizations — RVO (Return Value Optimization) and NRVO (Named Return Value Optimization). Simply put, the compiler constructs the return value directly in the memory space reserved by the caller, eliminating the intermediate copy or move operations. Starting from C++17, RVO is even mandatory in certain situations. So `return "Hello, " + name + "!";` does not produce extra string copies, and there is absolutely no need to worry about performance.

But if you try to return a reference to a local variable, things get dangerous:

```cpp
const int& get_value()
{
    int x = 42;
    return x;  // 返回局部变量的引用——悬垂引用！
}
```

> **Pitfall Warning**: This code compiles, but its runtime behavior is undefined behavior (UB). `x` is a local variable inside the function, and once the function returns, `x`'s memory is reclaimed — the reference you returned points to a block of memory that no longer exists. Reading data through this reference might yield garbage values, might yield old values that "happen to still be there," or might cause a segmentation fault directly. The compiler won't report an error (it's syntactically perfectly legal, though it might warn you), so this bug is extremely insidious. The principle is simple: never return a reference or pointer to a local variable. Just return by value, and the compiler will optimize it for you.

## Output Parameters vs. Return Values

When a function needs to produce multiple results, old-style C code often uses reference parameters for "output," but at the call site, `divide(a, b, q, r)` without looking at the signature, it's impossible to distinguish inputs from outputs. Modern C++ prefers returning a struct directly:

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

At the call site, `auto result = divide(a, b);` is clear at a glance; `result.quotient` is much more readable than `result.first`. Output parameters still make sense in scenarios where you're filling a large buffer with data, but most of the time, you should prefer return values.

## Hands-On Practice — passing.cpp

Now let's tie together the concepts from this chapter and write a complete example program. This program will demonstrate `swap` operations, a performance comparison of different parameter passing methods, and the use of const references in string processing.

```cpp
// passing.cpp —— 演示值传递、引用传递和 const 引用传递

#include <iostream>
#include <string>
#include <chrono>

/// @brief 交换两个整数的值
void swap_values(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

struct BigData {
    int payload[4096];  // 16 KB
};

/// @brief 值传递版本：每次调用拷贝整个 BigData
long sum_by_value(BigData data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief const 引用版本：零拷贝
long sum_by_const_ref(const BigData& data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief 拼接问候语，const 引用避免字符串拷贝
std::string build_greeting(const std::string& name)
{
    return "Hello, " + name + "! Welcome to Modern C++.";
}

int main()
{
    // swap 演示
    int a = 10;
    int b = 20;
    std::cout << "交换前: a = " << a << ", b = " << b << std::endl;
    swap_values(a, b);
    std::cout << "交换后: a = " << a << ", b = " << b << std::endl;

    // 性能对比
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

    // 字符串处理
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

Performance data will vary depending on the machine and compiler optimization level, but the trend is consistent: pass by value copies 16 KB each time, while the const reference version avoids the copy and is several times faster. Note that we used `-O2`; even so, the compiler must obey the language semantics — if you tell it to copy, it has to copy.

The two calls to `build_greeting` are also worth noting: the first passes an lvalue `name`, and the second passes a temporary object `std::string("World")` — both can be received by `const std::string&`, which is exactly the flexibility of const references.

## Try It Yourself

### Exercise 1: Implement swap

Write a `swap_values` function to swap the values of two `double`s, then write an overloaded version to swap two `std::string`s. Use a `main` function to verify the results.

### Exercise 2: Efficiently Handle Large Structs

Define a struct `Measurement` that contains an array of at least 1000 `double` elements. Write two functions: one that calculates the average using pass by value, and another that calculates the average using pass by const reference. Time them separately and compare the performance.

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

Hint: Think about what happens to the local variable `prefix` after the function returns.

## Summary

In this chapter, we clarified the three core parameter passing methods in C++. Pass by value copies the actual argument, and the function operates on a copy. For basic types, this is simple and safe, but for large objects, it incurs non-negligible performance overhead. Pass by reference lets the function directly access the caller's original variable, offering zero-copy and the ability to modify data, making it suitable for scenarios like `swap` where the actual argument needs to be changed, but a non-const reference cannot bind to an rvalue. Pass by const reference combines zero-copy with read-only safety; `const T&` can bind to both lvalues and rvalues, making it the standard approach for read-only parameters of non-trivial types.

For return values, simply return by value. Modern compilers' RVO/NRVO optimizations will eliminate unnecessary copies. Never return a reference to a local variable — that's a classic source of dangling references. When a function needs to output multiple results, prefer returning a struct over using output parameters.

In the next chapter, we will learn about function overloading and default arguments — allowing the same function name to exhibit different behaviors based on the types or number of arguments, which is one of the foundations of C++ polymorphism.
