---
chapter: 3
cpp_standard:
- 11
- 14
- 17
description: From syntax elements to STL integration, master the core usage of C++
  lambda expressions.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
reading_time_minutes: 13
related:
- Lambda 捕获机制深入
- std::function 与类型擦除
tags:
- host
- cpp-modern
- intermediate
- lambda
title: 'Lambda Basics: The Elegant Expression of Anonymous Functions'
translation:
  engine: anthropic
  source: documents/vol2-modern-features/ch03-lambda/01-lambda-basics.md
  source_hash: 3abc7c7cdb24fe6b67142db1aae2a2c9f2f64a1ddd7f7bf831d492ed68237f55
  token_count: 2432
  translated_at: '2026-05-26T11:25:32.750031+00:00'
---
# Lambda Basics: The Elegant Expression of Anonymous Functions

## Introduction

When writing sorting logic, I always found C function pointers and C++98 functors a bit awkward. Function pointers either pollute the global namespace or require passing around `static` member functions alongside a `void*` context; functors can encapsulate state inside a class, but defining a complete class for a two-line comparison logic feels like overkill (wow, the most OOP episode ever). C++11 introduced lambda expressions, which are essentially anonymous function objects defined right at the point of use—no need to jump to the top of the file to declare them, no extra symbols generated for the compiler, and the logic sits right next to the call site, making it immediately clear to anyone reading the code.

> **Learning Objectives**
>
> - Understand the syntactic elements of lambda expressions and the closure types the compiler generates behind the scenes
> - Master the use of lambda expressions with STL algorithms
> - Understand the basic semantics of value capture and reference capture
> - Know when to use `auto` and when to use `std::function`

---

## Breaking Down Lambda Syntax

The full syntax of a lambda expression looks a bit intimidating, but when broken down, each part is quite intuitive:

```cpp
[capture](parameters) -> return_type { body }
```

`capture` is the capture list, which determines how the lambda accesses variables in the enclosing scope; `parameters` is identical to a normal function's parameter list; `-> return_type` is the trailing return type, which in C++11 can only be omitted for the compiler to deduce under specific conditions (detailed in the next section); `body` is the function body. Let's start with the simplest lambda and gradually add complexity:

```cpp
// 什么都不做的 lambda,纯摆烂的
auto do_nothing = []() {};

// 简单返回一个值
auto forty_two = []() { return 42; };

// 带参数
auto double_it = [](int x) { return x * 2; };

// 实际使用：像普通函数一样调用
int result = double_it(21);  // result == 42
```

You'll notice that we use `auto` to receive the lambda—this is because every lambda expression generates a unique, unnamed class type (the so-called closure type), and there's no way for you to write out this type's name directly. `auto` is the most natural choice here.

---

## Return Type Deduction

C++11's lambda return type deduction rules are relatively strict: the compiler can only automatically deduce the return type when the lambda body meets the following conditions:

1. The function body consists of a single `return` statement, or
2. All `return` statements return expressions that deduce to the same type

When these conditions are met, you can omit the trailing return type:

```cpp
// 自动推导为 int
auto square = [](int x) { return x * x; };

// 自动推导为 double（因为有 static_cast<double>）
auto divide = [](int a, int b) -> double {
    return static_cast<double>(a) / b;
};
```

If the function body is more complex—for example, having multiple branches with different return paths—the compiler might fail to deduce the type, or the deduced result might not match your expectations. In such cases, explicitly specifying the return type is the safest approach:

```cpp
auto classify = [](int x) -> int {
    if (x > 0) {
        return x * 2;
    } else if (x < 0) {
        return -x;
    }
    return 0;   // 如果没有这条，某些编译器可能报警告
};
```

My advice is to omit the return type for simple lambdas and write it out explicitly for complex ones. Omitting it makes the code more compact, but only if it doesn't leave the reader guessing what the return type is.

---

## As STL Algorithm Arguments—Lambda's Main Battleground

The most common scenario for lambda expressions is serving as predicates or operation functions for STL algorithms. In the past, you either passed a global function pointer or wrote a functor class; now, you can simply write a lambda right at the call site, making the logic clear at a glance:

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

void process_data() {
    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // 找出第一个超过阈值的读数
    auto it = std::find_if(readings.begin(), readings.end(),
                          [](int value) { return value > 50; });

    // 统计有多少个异常值
    int anomaly_count = std::count_if(readings.begin(), readings.end(),
                                     [](int value) { return value > 80; });
    std::cout << "Anomalies: " << anomaly_count << "\n";

    // 原地翻倍
    std::transform(readings.begin(), readings.end(), readings.begin(),
                  [](int value) { return value * 2; });

    // 自定义排序：降序
    std::sort(readings.begin(), readings.end(),
             [](int a, int b) { return a > b; });
}
```

Previously, you had to define `is_even` somewhere else, forcing readers to jump around to find the definition. Now, the lambda is written right next to the algorithm call, so a quick glance reveals exactly what the predicate is doing.

---

## Capturing External Variables—Letting Lambda "See" the Outside

By default, a lambda cannot access any variables from the enclosing scope. This is an intentional design choice: lambdas want a clean sandbox that doesn't accidentally touch external state. When you do need to access external variables, you explicitly declare them through the capture list:

```cpp
int threshold = 50;

// 编译错误：threshold 不在 lambda 的作用域内
// auto check = [](int value) { return value > threshold; };

// 值捕获：复制一份 threshold 到闭包对象中
auto by_value = [threshold](int value) { return value > threshold; };

// 引用捕获：直接引用外部的 threshold
auto by_ref = [&threshold](int value) { return value > threshold; };
```

Value capture copies the variable at the exact moment the lambda is created; subsequent external modifications won't affect the copy inside the lambda. Reference capture allows the lambda to operate directly on the original variable. Both approaches have their use cases and their own pitfalls—we'll dive deep into this in the next chapter. For now, just remember one thing: **when you only need to read and not write, value capture is the safest default choice.**

There are also two common default capture syntaxes: `=` means value-capturing all used external variables, and `&` means reference-capturing all used external variables. While convenient to use, in production code I recommend explicitly listing the variable names to be captured, avoiding accidentally capturing things that shouldn't be captured.

```cpp
int a = 1, b = 2, c = 3;

// 全值捕获
auto sum_all = [=]() { return a + b + c; };  // 6

// 全引用捕获——可以修改外部变量
auto increment_all = [&]() { a++; b++; c++; };
increment_all();  // a=2, b=3, c=4

// 混合捕获：a 值捕获，b 引用捕获
auto mixed = [a, &b]() { return a + b; };
```

---

## Lambda's Type—Demystifying the Closure Type

As mentioned earlier, every lambda expression produces a unique, anonymous class type (the closure type). This class type has an `operator()` member function, with parameters and return values exactly as you wrote them in the lambda. The standard only specifies the behavior; the concrete implementation is up to the compiler. Conceptually, you can think of the lambda as the compiler generating a class like this:

```cpp
// 你写的 lambda
auto greet = [](const std::string& name) -> std::string {
    return "Hello, " + name;
};

// 编译器概念上生成的类（简化版）
struct /* 编译器生成的唯一名字 */ {
    std::string operator()(const std::string& name) const {
        return "Hello, " + name;
    }
};
auto greet = /* 上面那个类的实例 */{};
```

In actual implementations, the compiler adds corresponding data members based on the lambda's capture list, and decides whether `operator()` is `const` based on the `mutable` keyword. The method for generating the type name is left to each compiler (for example, GCC uses mangled names, Clang uses `<lambda_...>`, etc.), and consistency across compilers is not guaranteed.

This is why you can't directly write out a lambda's type name—this name is generated internally by the compiler, and it differs across compilers and even across translation units. Therefore, when storing a lambda, you either use `auto` (type known at compile time) or `std::function` (runtime type erasure, with extra overhead).

Passing lambdas via template parameters is a common practice for zero-overhead abstraction—the compiler can see the complete lambda type and has the opportunity to perform inline optimization:

```cpp
template<typename Func>
void call_func(Func f) {
    f();
}

call_func([]() { /* ... */ });  // 类型对编译器可见，可能内联
```

The key word here is "may": whether inlining actually happens depends on the compiler's optimization strategy, the complexity of the lambda, compiler flags, and other factors. But compared to the runtime indirect call of `std::function`, template parameters at least give the compiler a chance to optimize.

> **About the overhead of `std::function`**: `std::function` internally uses type erasure and Small Buffer Optimization (SBO). In libstdc++, a `std::function` object typically occupies 32 bytes (on 64-bit systems), even if the stored lambda only needs 1 byte. The call involves an extra layer of virtual-function-style indirect jump, which can prevent inlining. If you don't need runtime polymorphism, prefer `auto` or template parameters. We'll dive deep into this in Chapter 4, "Type Erasure and std::function."

---

## Hands-on: An Event Handling System

Let's use lambdas to build a simple event handling system. This is a very common requirement in real-world projects—registering callbacks, triggering callbacks, where callbacks might come from different modules, each with its own context:

```cpp
#include <cstdint>
#include <functional>
#include <array>
#include <iostream>

class EventDispatcher {
public:
    using Handler = std::function<void(uint32_t)>;

    void on_event(int id, Handler handler) {
        if (id >= 0 && id < static_cast<int>(handlers_.size())) {
            handlers_[id] = std::move(handler);
        }
    }

    void trigger(int id, uint32_t timestamp) {
        if (id >= 0 && id < static_cast<int>(handlers_.size()) && handlers_[id]) {
            handlers_[id](timestamp);
        }
    }

private:
    std::array<Handler, 8> handlers_;
};

// 使用示例
void setup_system() {
    EventDispatcher dispatcher;
    int press_count = 0;
    uint32_t last_press_time = 0;

    // 注册按键回调：引用捕获 press_count 和 last_press_time
    dispatcher.on_event(0, [&](uint32_t timestamp) {
        if (timestamp - last_press_time > 50) {   // 50ms 防抖
            press_count++;
            last_press_time = timestamp;
            std::cout << "Press #" << press_count
                      << " at " << timestamp << "ms\n";
        }
    });

    // 注册超时回调：值捕获 threshold
    uint32_t threshold = 1000;
    dispatcher.on_event(1, [threshold](uint32_t timestamp) {
        if (timestamp > threshold) {
            std::cout << "Timeout at " << timestamp << "ms\n";
        }
    });

    // 模拟事件触发
    dispatcher.trigger(0, 100);
    dispatcher.trigger(0, 160);   // 距上次 60ms，通过防抖
    dispatcher.trigger(0, 180);   // 距上次 20ms，被防抖过滤
    dispatcher.trigger(1, 1200);
}
```

Output:

```text
Press #1 at 100ms
Press #2 at 160ms
Timeout at 1200ms
```

As you can see, using lambdas as callbacks is very natural—the capture list brings in the necessary context variables, the function body contains the business logic, and you just pass it in when registering. Compared to the C-style `void*` paired with `reinterpret_cast`, both type safety and readability are significantly better.

---

## C++14 Generic Lambdas

C++14 brought a very practical enhancement to lambdas: parameter types can use `auto`. This turns the lambda into a templated function object—the compiler generates a separate instance of `operator()` for each different parameter type:

```cpp
// 泛型 lambda：可以接受任何支持 operator+ 的类型
auto add = [](auto a, auto b) { return a + b; };

int xi = add(3, 4);              // int operator+(int, int)
double xd = add(3.5, 2.5);       // double operator+(double, double)
std::string xs = add(std::string("hello"), std::string(" world"));
```

The closure type the compiler generates behind the scenes looks roughly like this:

```cpp
struct GenericClosure {
    template<typename T1, typename T2>
    auto operator()(T1 a, T2 b) const {
        return a + b;
    }
};
```

Generic lambdas are especially handy when writing generic algorithms and utility functions, eliminating the need to wrap a lambda in an outer template function. We'll explore this in depth in Chapter 3, "Generic Lambdas and Template Lambdas."

---

## Pitfalls and Warnings

### Don't Make Lambda Bodies Too Long

The advantage of lambdas lies in their local definition and compact logic. If a lambda exceeds five to seven lines, you should consider extracting it into a named function or a functor. Lambdas longer than this actually hurt readability—readers have to scroll through several screens within an algorithm's argument list, which defeats the original purpose of "logic at the point of use."

### The Lifetime Trap of Reference Capture

This is one of the most common sources of lambda bugs: a reference-captured variable has already been destroyed by the time the lambda executes. A typical scenario is creating a lambda inside a function and returning it:

```cpp
// 危险！返回的 lambda 引用了局部变量 local
auto make_bad_lambda() {
    int local = 42;
    return [&local]() { return local; };   // local 在函数返回后销毁
}

// 安全：值捕获
auto make_safe_lambda() {
    int local = 42;
    return [local]() { return local; };    // lambda 持有副本
}
```

Reference capture itself isn't wrong, but you must ensure that the referenced object outlives the lambda. In scenarios like event systems and asynchronous callbacks, this constraint is particularly easy to overlook.

### Prefer `auto` Over `std::function` for Storing Lambdas

Unless you need runtime polymorphism (such as putting different types of callbacks into the same container), don't use `std::function` to store lambdas. `auto` directly holds the closure type, with a size equal to the captured data members (captureless lambdas are typically just 1 byte), giving the compiler a chance to inline; `std::function` performs type erasure, has a fixed overhead (32–64 bytes), and adds an extra layer of indirect jump during invocation.

```cpp
// 编译期类型已知，大小=1字节（无捕获），可能内联
auto f = [](int x) { return x * 2; };

// 类型擦除，大小=32字节（libstdc++），运行时间接调用
std::function<int(int)> g = [](int x) { return x * 2; };
```

This difference can be important on performance-critical paths, but avoid premature optimization: if the code isn't on a hot path, the convenience of `std::function` might be more important.

---

## Run Online

Run the Lambda event handling system example online to observe the actual behavior of reference capture and value capture:

<OnlineCompilerDemo
  title="Lambda Basics: Event Handling System"
  source-path="code/examples/vol2/08_lambda_basics.cpp"
  description="Run online and observe the actual behavior of Lambda's reference capture and value capture in event dispatching."
  allow-run
/>

## Summary

Lambda expressions are among the most practical features in modern C++. They have reduced the cost of "defining a function at the point of use" to an absolute minimum—no extra naming needed, no class definitions required, no separation of declaration and implementation. Here's a recap of the core points:

- The syntax of a lambda is `[captures](params) -> ret { body }`, and most of the time you can omit the return type
- A lambda's type is a unique closure type generated by the compiler, and using `auto` to store it is the most natural approach
- The biggest use case for lambdas is as predicates and operation arguments for STL algorithms
- Value capture copies variables, reference capture references variables, and each has its own safety boundaries
- C++14's `auto` parameters turn lambdas into templated function objects

In the next chapter, we'll dive deep into lambda's capture mechanism—what actually happens under the hood with value capture and reference capture, what problem C++14's init capture solves, and those capture traps that keep you debugging until two in the morning.

## References

- [Lambda expressions (C++11) - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++14 generic lambdas - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Generic_lambdas)
