---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Value capture, reference capture, and init capture: semantics and pitfalls'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
reading_time_minutes: 15
related:
- 泛型 Lambda 与模板 Lambda
tags:
- host
- cpp-modern
- intermediate
- lambda
title: Deep Dive into Lambda Capture Mechanisms
translation:
  engine: anthropic
  source: documents/vol2-modern-features/ch03-lambda/02-lambda-capture.md
  source_hash: 238b6901776b29357aea77ccb803a22740c93b27178d477245c5cc602f8b55d1
  token_count: 3093
  translated_at: '2026-05-26T11:25:54.254751+00:00'
---
# A Deep Dive into Lambda Capture Mechanisms

## Introduction

In the previous chapter, we quickly went over the basic syntax of lambda expressions and briefly mentioned the existence of the capture list. But you probably still have a few questions in mind: what exactly does a capture by value copy? Is capture by reference just storing a pointer under the hood? What are the pitfalls of default captures like `[=]` and `[&]`? What makes C++14 init capture so great? In this chapter, we will tear down the capture mechanism from start to finish. We will not only cover "how to use it," but more importantly, explain "what the compiler does behind the scenes" and "which usages will blow up at runtime."

> **Learning Objectives**
>
> - Understand the underlying semantics of capture by value and capture by reference—what exactly the closure type stores
> - Master the usage and motivation behind C++14 init capture and C++17 `*this` capture
> - Identify and avoid common capture-related pitfalls (dangling references, lifetime issues)
> - Understand the size and performance impact of lambda objects

---

## Capture by Value — Copying into the Closure Object

The semantics of capture by value are very straightforward: at the exact moment the lambda is created, the captured variable is copied and stored as a member variable of the closure type. Any subsequent modifications to the external variable will not affect the copy inside the lambda.

```cpp
void demo_value_capture() {
    int threshold = 100;

    // threshold 被复制到闭包对象中
    auto is_high = [threshold](int value) {
        return value > threshold;
    };

    threshold = 200;             // 修改外部变量
    bool result = is_high(150);  // false，lambda 里的 threshold 还是 100
}
```

From the compiler's perspective, the lambda above is roughly translated into a closure type like this:

```cpp
struct ClosureType {
    int threshold;  // 被捕获的变量变成了成员

    bool operator()(int value) const {
        return value > threshold;
    }
};

auto is_high = ClosureType{100};  // 构造时复制 threshold
```

Notice that `const`—members captured by value are `const` inside the `operator()`, and you cannot modify them. If you genuinely need to modify the captured copy inside the lambda, you need to add the `mutable` keyword:

```cpp
int counter = 0;

// 编译错误：counter 在 lambda 内是 const int
// auto bad = [counter]() { counter++; };

// 加 mutable：允许修改 lambda 内部的副本
auto make_counter = [counter]() mutable {
    return ++counter;   // 修改的是闭包对象自己的 counter，不是外部的
};

std::cout << make_counter() << "\n";  // 1
std::cout << make_counter() << "\n";  // 2
std::cout << counter << "\n";         // 0——外部的 counter 没有被碰过
```

The meaning of `mutable` is to tell the compiler: this lambda's `operator()` is not `const`. Each call might modify the internal state of the closure object. This is also why every call to `make_counter()` increments the value—the closure object maintains its own independent state.

---

## Capture by Reference — Storing the Address of the Original Variable

The semantics of capture by reference are not mysterious either: what the compiler stores in the closure type is a pointer to the captured variable (or a reference, which is basically equivalent in terms of underlying implementation). We can verify this through `sizeof`: the size of a closure object using capture by reference equals the size of a pointer (8 bytes on a 64-bit system). Reads and writes to the captured variable inside the lambda are actually operations on the original variable.

```cpp
void demo_ref_capture() {
    int sum = 0;

    auto accumulate = [&sum](int value) {
        sum += value;   // 直接修改外部的 sum
    };

    accumulate(10);
    accumulate(20);
    accumulate(30);
    // sum == 60
}
```

The corresponding closure type looks roughly like this:

```cpp
struct ClosureType {
    int& sum;  // 存储的是引用

    void operator()(int value) const {
        sum += value;  // 通过引用修改外部变量
    }
};
```

Here is an interesting detail: `operator()` is `const`, yet we modified the external variable through `sum`. This is because the reference itself (the stored address) is `const`—you cannot make the reference point to another object—but the value of the object bound to the reference can be modified. This is the same principle as a `int* const ptr` not being able to change where it points, but being able to change the `*ptr`.

> **Verification**: You can run `code/volumn_codes/vol2/ch03-lambda/test_ref_capture_impl.cpp` to verify the underlying implementation details of capture by reference and the `const` semantics.

The biggest advantage of capture by reference is zero-copy—for large objects (like `std::vector` or `std::string`), capture by reference avoids unnecessary copies. But the biggest risk lies right here: **the referenced variable must outlive the lambda**.

---

## Default Captures — The Pitfalls of `[=]` and `[&]`

When there are many variables to capture, listing them one by one can indeed be annoying. C++ provides two default capture modes: `[=]` means all used external variables are captured by value, and `[&]` means they are all captured by reference.

```cpp
void demo_default_capture() {
    int a = 1, b = 2, c = 3;

    // 全值捕获
    auto sum = [=]() { return a + b + c; };   // 6

    // 全引用捕获
    auto increment = [&]() { a++; b++; c++; };
    increment();   // a=2, b=3, c=4
}
```

You can also specify a different capture method for individual variables on top of the default capture—mixed capture:

```cpp
void demo_mixed_capture() {
    int threshold = 100;
    int count = 0;
    double factor = 1.5;

    // 默认值捕获，但 count 按引用捕获
    auto process = [=, &count](int value) {
        if (value > threshold) {
            count++;
            return static_cast<int>(value * factor);
        }
        return value;
    };
}
```

This sounds convenient, but `[=]` and `[&]` have a few less obvious pitfalls. `[=]` default capture by value does not capture the `this` pointer—wait, actually, that's wrong. Before C++20, `[=]` could implicitly capture `this`, which led to a classic problem: you think you are capturing the value of a member variable by value, but you are actually capturing the `this` pointer, and accessing it via `this->member` inside the lambda still operates on the original object's member. C++20 fixed this behavior; `[=]` no longer implicitly captures `this`, and you need to explicitly write `[=, this]` or `[=, *this]`.

> **Verification**: You can run `code/volumn_codes/vol2/ch03-lambda/test_cxx20_default_capture.cpp` to observe the behavioral differences between C++17 and C++20 regarding default capture of `this` (C++20 will emit a warning).

My recommendation is: **try to explicitly list the variable names you want to capture in production code, and minimize the use of `[=]` and `[&]`**. The benefit of being explicit is that during code review, you can see at a glance which external states the lambda depends on, and it also avoids accidentally capturing things that shouldn't be captured. (Capturing everything is risky; unless your code is trivially simple, grabbing everything blindly can lead to problems.)

---

## C++14 Init Capture — Lambdas with Their Own State

C++14 introduced init capture, sometimes called generalized lambda capture. The syntax is to write `name = expression` in the capture list, where `name` is a new variable name and `expression` is the initialization expression. This variable belongs entirely to the closure object and has no relationship with the outside world:

```cpp
void demo_init_capture() {
    int base = 10;

    // 捕获 base + 5 的结果，而不是 base 本身
    auto lam = [value = base + 5]() {
        return value * 2;   // value == 15
    };
}
```

The most useful scenario for init capture is **move capture**—moving move-only types (like `std::unique_ptr`, `std::thread`, etc.) into the closure object:

```cpp
#include <memory>

auto make_handler() {
    auto ptr = std::make_unique<int>(42);

    // 把 unique_ptr 移入 lambda
    return [p = std::move(ptr)]() {
        return *p;   // p 是 lambda 独占的
    };
}
```

In C++11, to achieve the same effect, you had to hand-write a functor class and make `unique_ptr` a member variable. C++14's init capture makes this very natural.

Another common use case is using init capture to replace a `mutable` counter, which makes the semantics clearer:

```cpp
// C++11 风格：需要 mutable
int x = 0;
auto counter_old = [x]() mutable { return ++x; };

// C++14 风格：初始化捕获，语义更明确
auto counter_new = [count = 0]() mutable { return ++count; };
```

The benefit of the second version is that `count` is entirely the lambda's own state, with no relation to the external variable `x`—you can tell just from the name that this is an independent counter.

---

## C++17 `*this` Capture — Capturing the Entire Object by Value

When writing a lambda inside a member function, if you want to capture the current object, the traditional way is `[this]`. But `[this]` captures a pointer; if the lambda's lifetime exceeds the object itself, you end up with a dangling `this` pointer. C++17 introduced `[*this]`, which captures the entire object by value—storing a copy of the object in the closure type:

```cpp
#include <iostream>
#include <string>
#include <functional>

class Sensor {
    std::string name_;
    int reading_ = 0;

public:
    explicit Sensor(std::string name) : name_(std::move(name)) {}

    std::function<int()> make_reader() {
        // [*this]：复制整个 Sensor 对象到闭包中
        // 即使原始 Sensor 被销毁，lambda 仍然安全
        return [*this]() mutable {
            return ++reading_;
        };
    }

    std::function<int()> make_reader_unsafe() {
        // [this]：只存指针，对象销毁后变成悬垂指针
        return [this]() {
            return ++reading_;   // 危险！
        };
    }
};

void demo_star_this() {
    std::function<int()> reader;

    {
        Sensor s("temperature");
        reader = s.make_reader();      // [*this]：安全
        // reader_unsafe = s.make_reader_unsafe();  // [this]：危险
    }
    // s 已经销毁

    std::cout << reader() << "\n";     // 安全：lambda 持有 s 的副本
    std::cout << reader() << "\n";     // 2
}
```

The cost of `[*this]` is copying the entire object. If the object is large (contains `std::vector`, large `std::array`, etc.), this copy overhead might not be trivial. But for small configuration objects and value objects, the safety gained by this copy is well worth it.

⚠️ **Note**: `[*this]` requires the context where the current lambda resides to be a member function where `this` can be dereferenced. You cannot use `[*this]` in static member functions or non-member functions.

---

## Capture Pitfalls — Dangling References and Lifetimes

The most common and most headache-inducing source of bugs in the capture mechanism is lifetime issues. Let's look at a few classic trap scenarios.

### Returning a Lambda with Capture by Reference

```cpp
// 经典陷阱：返回引用了局部变量的 lambda
auto make_dangling() {
    int count = 0;
    return [&count]() { return ++count; };
    // count 在函数返回后销毁，lambda 持有的是悬垂引用
}

auto bad = make_dangling();
// bad() 是未定义行为！
```

The fix is simple—replace capture by reference with capture by value or init capture:

```cpp
auto make_safe() {
    int count = 0;
    return [count]() mutable { return ++count; };    // 值捕获：安全
}

auto make_safe2() {
    return [count = 0]() mutable { return ++count; }; // 初始化捕获：更清晰
}
```

### Capture by Reference in Loops

This trap is particularly common in asynchronous programming and event systems:

```cpp
#include <vector>
#include <functional>

std::vector<std::function<void()>> handlers;

void demo_loop_trap() {
    for (int i = 0; i < 5; ++i) {
        // 错误：所有 lambda 引用同一个 i，循环结束后 i == 5
        handlers.push_back([&i]() {
            std::cout << i << " ";   // 全部输出 5
        });
    }

    handlers.clear();

    for (int i = 0; i < 5; ++i) {
        // 正确：每个 lambda 有自己的 i 副本
        handlers.push_back([i]() {
            std::cout << i << " ";   // 输出 0 1 2 3 4
        });
    }
}
```

### The Pitfall of Capturing `this`

```cpp
class Device {
    std::string name_ = "sensor";

public:
    auto get_handler() {
        // 如果 Device 对象在 lambda 执行前被销毁，this 就悬垂了
        return [this]() { return name_; };
    }

    // 更安全的做法：捕获需要的成员，而不是 this
    auto get_handler_safe() {
        return [name = name_]() { return name; };
    }

    // C++17 最安全：按值捕获整个对象
    auto get_handler_safest() {
        return [*this]() { return name_; };
    }
};
```

---

## Lambda Object Size Analysis

Once we understand how the capture mechanism stores data under the hood, the size of a lambda object is easy to understand—it is simply the sum of the sizes of all captured variables (possibly plus some alignment padding). A standard lambda does not have a vtable pointer; the closure type is a normal class type. We can use `sizeof` to verify this:

```cpp
#include <iostream>

void demo_closure_size() {
    int a = 0;
    double b = 0.0;
    int& ref = a;

    auto no_capture = []() {};
    auto capture_int = [a]() { return a; };
    auto capture_ref = [&a]() { return a; };
    auto capture_both = [a, &b]() { return a + b; };

    std::cout << "no_capture:    " << sizeof(no_capture) << " bytes\n";
    // 通常 1 byte（空类特例）

    std::cout << "capture_int:   " << sizeof(capture_int) << " bytes\n";
    // 通常 4 bytes（一个 int）

    std::cout << "capture_ref:   " << sizeof(capture_ref) << " bytes\n";
    // 通常 8 bytes（一个指针，64 位系统）

    std::cout << "capture_both:  " << sizeof(capture_both) << " bytes\n";
    // 通常 16 bytes（int + double 引用/指针，考虑对齐）
}
```

Typical output (64-bit system, GCC):

```text
no_capture:    1 bytes
capture_int:   4 bytes
capture_ref:   8 bytes
capture_both:  16 bytes
```

One point worth noting: the size of a lambda with no captures is usually 1 byte instead of 0 bytes—C++ does not allow objects of size zero (otherwise, the addresses of elements in an array could not be distinguished). Capture by reference stores a pointer, which takes up 8 bytes on a 64-bit system.

> **Verification**: You can run `code/volumn_codes/vol2/ch03-lambda/test_capture_size.cpp` to view the actual sizes of closure objects under various capture methods.

When you store a lambda in a `std::function`, the storage space required is more than just that—a `std::function` typically has its own SBO buffer (32-64 bytes), plus the management overhead of type erasure. This is also why we said in the previous chapter, "prefer using `auto` to store lambdas."

---

## Performance Considerations — When to Inline, and When Not

The performance characteristics of a lambda are closely tied to its capture method and storage method.

When a lambda is called with a compile-time known type (like `auto` or a template parameter), the compiler can see the complete closure type and `operator()` implementation, allowing for perfect inlining. In this case, the difference between capture by value and capture by reference is basically zero—even though capture by value involves an extra copy, the compiler can usually eliminate this copy overhead after optimization.

However, if the lambda is stored in a `std::function`, the situation is different. The type erasure of `std::function` introduces a layer of indirection, and the compiler cannot inline across this indirection. Furthermore, if the captured content exceeds the SBO buffer size of `std::function`, it will trigger a heap allocation.

```cpp
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>

void benchmark_lambda_styles() {
    std::vector<int> data(1'000'000);
    int threshold = 50;

    // 风格 1：auto + 算法模板参数——完全内联
    auto start = std::chrono::high_resolution_clock::now();
    auto count1 = std::count_if(data.begin(), data.end(),
                               [threshold](int x) { return x > threshold; });
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "auto lambda: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";

    // 风格 2：std::function——有间接调用开销
    std::function<bool(int)> pred = [threshold](int x) { return x > threshold; };
    start = std::chrono::high_resolution_clock::now();
    auto count2 = std::count_if(data.begin(), data.end(), pred);
    end = std::chrono::high_resolution_clock::now();
    std::cout << "std::function: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";
}
```

With optimization enabled (-O2/-O3), the `auto` version is typically about 2-3 times faster than the `std::function` version (exact numbers depend on the compiler, optimization level, and lambda complexity). Benchmarks (GCC 13.2.0, -O3) show that when processing 10 million elements, the `auto` version takes about 6-7 milliseconds, while the `std::function` version takes about 14-15 milliseconds. The trend is consistent: **when you don't need runtime polymorphism, passing lambdas via templates or `auto` is the optimal choice.**

> **Verification**: You can run `code/volumn_codes/vol2/ch03-lambda/benchmark_performance.cpp` to reproduce this performance test (requires -O3 optimization at compile time).

---

## Choosing a Capture Method — A Decision Guide

Let's summarize the choice of capture methods into a few simple rules:

For small, immutable data (`int`, `float`, simple structs), capture by value is the safest default choice. It ensures the lambda does not depend on external state, is thread-safe, and has no lifetime issues. For large objects (`std::vector`, `std::string`), if the lambda only needs to read and not modify them internally, capture by reference combined with `const` is a zero-copy solution; if the lambda needs to independently own the object, use init capture `name = std::move(obj)` to move it into the closure. For external variables that need to be modified inside the lambda (accumulators, state updates), capture by reference is the most natural choice, but you must ensure the variable's lifetime is long enough.

In member functions, if the lambda does not escape the object's lifetime, `[this]` is convenient; if the lambda might outlive the object, use `[*this]` (C++17) or init capture the specific member variables needed. In production code, I strongly recommend explicitly listing the names of the captured variables and avoiding `[=]` and `[&]`—explicit code makes code review easier and reduces accidental captures.

---

## Run Online

Run the lambda capture mechanism examples online to compare the effects of different capture methods:

<OnlineCompilerDemo
  title="Lambda Capture Mechanisms: Capture by Value, Capture by Reference, and Closure Size"
  source-path="code/examples/vol2/09_lambda_capture.cpp"
  description="Run online and compare the behavioral differences between capture by value, capture by reference, mutable, and init capture."
  allow-run
/>

## Summary

The lambda capture mechanism is key to understanding lambda performance and safety. Core takeaways:

- Capture by value copies variables into the closure object; it is `const` by default, and `mutable` allows modifying the copy inside the closure
- Capture by reference stores the variable's address/reference; it is zero-copy but requires guaranteeing the lifetime
- C++14 init capture allows lambdas to have independent state and supports move capture
- C++17 `*this` capture copies the entire object by value, solving the dangling pointer problem of `[this]`
- The size of a lambda object equals the sum of the sizes of all captured variables
- When runtime polymorphism is not needed, passing lambdas via `auto` or template parameters yields the best performance

## References

- [Lambda capture - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
- [C++14 generalized lambda capture](https://en.cppreference.com/w/cpp/language/lambda#Captures)
- [C++17 capture *this](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
