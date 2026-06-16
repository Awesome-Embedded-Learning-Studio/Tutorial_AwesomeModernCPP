---
chapter: 3
cpp_standard:
- 11
- 14
- 17
description: Master core C++ lambda expression usage, from syntax elements to STL
  integration
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
  source: documents/vol2-modern-features/ch03-lambda/01-lambda-basics.md
  source_hash: f93bd347851164729c377c501086ca9d4bbffe70f0cae70b82a5dd4f9cf6a809
  translated_at: '2026-06-16T03:56:51.543924+00:00'
  engine: anthropic
  token_count: 2427
---
# Lambda Basics: The Elegant Expression of Anonymous Functions

## Introduction

When writing sorting logic, I have always found C function pointers and C++98 functors somewhat awkward. Function pointers either pollute the global namespace or require passing `static` member functions along with a `void*` context back and forth. Functors allow encapsulating state within a class, but defining a complete class just for a two-line comparison logic feels like overkill (ah, the most OOP episode). C++11 introduced lambda expressions, which are essentially anonymous function objects defined at the point of use—no need to jump to the top of the file to declare them, no need for the compiler to generate extra symbols, and the logic is written right next to the call site, making it immediately clear to anyone reading the code.

> **Learning Objectives**
>
> - Understand the syntactic elements of lambda expressions and the closure types behind the compiler.
> - Master the use of lambdas with STL algorithms.
> - Understand the basic semantics of capture by value and capture by reference.
> - Know when to use `auto` and when to use `std::function`.

---

## Deconstructing Lambda Syntax

The full syntax of a lambda expression looks a bit intimidating, but each part is quite intuitive when broken down:

```cpp
[capture_list](parameters) -> return_type { function_body }
```

The `capture_list` determines how the lambda accesses variables from the outer scope; `parameters` are identical to a normal function's parameter list; `return_type` is the trailing return type, which in C++11 can only be omitted for the compiler to deduce under specific conditions (see the next section); and `function_body` is the body of the function. Let's start with the simplest lambda and gradually add complexity:

```cpp
auto lambda = []() { return 42; };
```

You will notice that we use `auto` to receive the lambda. This is because each lambda expression generates a unique, unnamed class type (the so-called closure type), so you cannot directly write the name of this type. `auto` is the most natural choice here.

---

## Return Type Deduction

C++11's rules for lambda return type deduction are relatively strict: the compiler can automatically deduce the return type only when the lambda body meets the following conditions:

1. The function body consists of a single `return` statement, or
2. All `return` statements return expressions of the same deduced type.

When these conditions are met, you can omit the return type:

```cpp
auto simple = [](int x) { return x * 2; }; // Deduced as int
```

If the function body is complex, for example, having multiple branches with different return paths, the compiler may not be able to deduce it, or the result may differ from your expectations. In such cases, explicitly specifying the return type is the safest approach:

```cpp
auto complex = [](int x) -> int {
    if (x > 0) {
        return x;
    } else {
        return 0;
    }
};
```

My advice is: omit the return type for simple lambdas, and write it out explicitly for complex ones. Omitting it makes the code more compact, provided you don't force the reader to guess the return type.

---

## As Arguments for STL Algorithms—The Main Battleground

The most common scenario for lambda expressions is serving as predicates or operation functions for STL algorithms. Previously, you either passed a global function pointer or wrote a functor class. Now, you can simply write the lambda at the call site, making the logic clear at a glance:

```cpp
std::vector<int> vec{4, 1, 3, 5, 2};
std::sort(vec.begin(), vec.end(), [](int a, int b) {
    return a > b; // Descending order
});
```

Previously, you had to define the comparison logic elsewhere, causing the reader to jump back and forth to find the definition. Now, the lambda is written right next to the algorithm call, so a quick glance reveals what the predicate does.

---

## Capturing External Variables—Letting Lambda "See" the Outside

By default, a lambda cannot access any variables from the outer scope. This is an intentional design: the lambda provides a clean sandbox that won't accidentally touch external state. When you do need to access external variables, you declare them explicitly via the capture list:

```cpp
int factor = 10;
auto multiply = [factor](int x) { return x * factor; }; // Capture by value
```

Capture by value copies the variable at the moment the lambda is created; subsequent external modifications do not affect the copy inside the lambda. Capture by reference allows the lambda to operate directly on the original variable. Both methods have their use cases and their pitfalls—we will discuss this in detail in the next chapter. For now, just remember one thing: **when you only read and do not write, capture by value is the safest default choice.**

There are also two common default capture modes: `[=]` means capture by value all used external variables, and `[&]` means capture by reference all used external variables. While convenient, in production code, I suggest explicitly listing the variable names to be captured to avoid unintentionally capturing variables that shouldn't be captured.

```cpp
int a = 1, b = 2;
auto explicit_capture = [a, &b]() { b = a + b; }; // Explicit is better
```

---

## The Type of Lambda—Unveiling the Closure Type

As mentioned earlier, each lambda expression produces a unique, anonymous class type (closure type). This class type has a `operator()` member function, with parameters and return values matching what you wrote in the lambda. The standard only mandates the behavior; the specific implementation is up to the compiler. Conceptually, you can think of the lambda as the compiler generating a class like this:

```cpp
class CompilerGeneratedName {
public:
    int operator()(int x) const {
        return x * 2;
    }
};
```

In actual implementation, the compiler adds corresponding data members based on the lambda's capture list and determines whether `operator()` is `const` based on the `mutable` keyword. The generation of the type name is decided by each compiler (e.g., GCC uses mangling, Clang uses a different scheme), and consistency across compilers is not guaranteed.

This is why you cannot directly write the type name of a lambda—this name is generated internally by the compiler and varies across compilers and compilation units. Therefore, when storing a lambda, use either `auto` (type known at compile time) or `std::function` (type erasure at runtime with extra overhead).

Passing a lambda via a template parameter is a common practice for zero-overhead abstraction—the compiler sees the complete lambda type and has the opportunity to perform inline optimization:

```cpp
template <typename Func>
void apply_template(Func f) {
    // The compiler knows the exact type of Func here
    f(10); // Likely inlined
}
```

The key here is "possible": whether inlining actually happens depends on the compiler's optimization strategy, the complexity of the lambda, compiler options, etc. But compared to the runtime indirect call of `std::function`, template parameters at least give the compiler a chance to optimize.

> **About `std::function` overhead**: `std::function` uses type erasure and Small Buffer Optimization (SBO). In libstdc++, a `std::function` object typically occupies 32 bytes (on 64-bit systems), even if the stored lambda only needs 1 byte. The call involves an extra layer of virtual-function-style indirection, which may prevent inlining. If runtime polymorphism is not needed, prefer `auto` or template parameters. We will dive deeper into this in Chapter 4, "Type Erasure and std::function".

---

## In Practice: An Event Handling System

Let's use lambdas to build a simple event handling system. This is a common requirement in real projects—registering callbacks, triggering callbacks, where callbacks might come from different modules with their own contexts:

```cpp
#include <iostream>
#include <functional>
#include <vector>
#include <string>

class EventSystem {
public:
    using Callback = std::function<void()>;
    using EventID = size_t;

    EventID subscribe(Callback cb) {
        EventID id = next_id++;
        callbacks.push_back({id, std::move(cb)});
        return id;
    }

    void trigger(EventID id) {
        for (auto& entry : callbacks) {
            if (entry.first == id && entry.second) {
                entry.second();
            }
        }
    }

private:
    std::vector<std::pair<EventID, Callback>> callbacks;
    EventID next_id = 0;
};

int main() {
    EventSystem sys;

    // Module A handles button clicks
    int click_count = 0;
    auto click_handler = [&click_count]() {
        click_count++;
        std::cout << "Button clicked! Total: " << click_count << "\n";
    };
    auto click_id = sys.subscribe(click_handler);

    // Module B handles timer events
    int timer_value = 100;
    auto timer_handler = [timer_value]() {
        std::cout << "Timer expired with value: " << timer_value << "\n";
    };
    auto timer_id = sys.subscribe(timer_handler);

    // Simulate events
    sys.trigger(click_id);
    sys.trigger(click_id);
    sys.trigger(timer_id);
}
```

Output:

```text
Button clicked! Total: 1
Button clicked! Total: 2
Timer expired with value: 100
```

You can see that using lambdas as callbacks is very natural—the capture list brings in the necessary context variables, the body contains the business logic, and it's passed in during registration. Compared to C-style `void*` pointers paired with `reinterpret_cast` casts, both type safety and readability are significantly improved.

---

## C++14 Generic Lambdas

C++14 brought a very practical enhancement to lambdas: parameter types can be `auto`. This turns the lambda into a template function object—the compiler generates a separate instance of `operator()` for different argument types:

```cpp
auto generic = [](auto x, auto y) {
    return x + y;
};

generic(1, 2);    // Instantiates for int
generic(1.0, 2.0); // Instantiates for double
```

The closure type generated by the compiler behind the scenes looks roughly like this:

```cpp
class CompilerGeneratedName {
public:
    template <typename T, typename U>
    auto operator()(T x, U y) const {
        return x + y;
    }
};
```

Generic lambdas are particularly useful when writing generic algorithms and utility functions, eliminating the need to wrap a template function around the lambda. We will explore this in depth in Chapter 3, "Generic Lambdas and Template Lambdas".

---

## Caveats and Pitfall Warnings

### Don't Make Lambda Bodies Too Long

The advantage of lambdas lies in local definition and compact logic. If a lambda exceeds 5-7 lines, you should consider extracting it into a named function or a functor. Lambdas longer than this actually reduce readability—the reader has to scroll through several screens within the algorithm's argument list, which violates the original intent of "logic at the point of use".

### The Lifetime Trap of Reference Capture

This is one of the most common sources of lambda bugs: the variable captured by reference has been destroyed by the time the lambda executes. A typical scenario is creating a lambda inside a function and returning it:

```cpp
// DANGER: Returning a lambda that captures a local variable by reference
auto get_callback() {
    int local = 10;
    return [&local]() { // local is destroyed when get_callback returns
        return local;
    };
}
```

Reference capture itself is not wrong, but you must ensure that the referenced object outlives the lambda. In scenarios like event systems and asynchronous callbacks, this constraint is easily overlooked.

### Prefer `auto` Over `std::function` for Storing Lambdas

Unless you need runtime polymorphism (e.g., putting different types of callbacks into the same container), do not use `std::function` to store lambdas. `auto` directly holds the closure type, with a size equal to the captured data members (capture-less lambdas are typically just 1 byte), giving the compiler a chance to inline optimizations. `std::function` performs type erasure, has a fixed overhead (32-64 bytes), and adds an extra layer of indirection during the call.

```cpp
auto lambda = []() { /* ... */ };
auto stored_auto = lambda; // Zero overhead, exact type
std::function<void()> stored_func = lambda; // Type erasure, overhead
```

This difference can be significant on performance-critical paths, but avoid premature optimization: if the code isn't a hot path, the convenience of `std::function` might be more important.

---

## Run Online

Run the Lambda Event Handling System example online to observe the actual behavior of reference capture and value capture:

<OnlineCompilerDemo
  title="Lambda Basics: Event Handling System"
  source-path="code/examples/vol2/08_lambda_basics.cpp"
  description="Run online and observe the actual behavior of Lambda's reference capture and value capture in event dispatch."
  allow-run
/>

## Summary

Lambda expressions are one of the most practical features in modern C++. They have minimized the cost of "defining functions at the point of use"—no extra naming, no class definitions, and no need to separate declarations and implementations. Key takeaways:

- Lambda syntax is `[capture](params) -> ret { body }`; the return type can often be omitted.
- The type of a lambda is a unique closure type generated by the compiler; storing it with `auto` is the most natural approach.
- The primary use of lambdas is as predicates and operation arguments for STL algorithms.
- Capture by value copies variables; capture by reference references variables; each has its own safety boundaries.
- C++14's `auto` parameters turn lambdas into template function objects.

In the next chapter, we will dive deep into lambda's capture mechanism—what actually happens at the底层 for value and reference capture, what problems C++14's init capture solves, and those capture traps that keep you debugging until 2 AM.

## References

- [Lambda expressions (C++11) - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++14 generic lambdas - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Generic_lambdas)
