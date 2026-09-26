---
chapter: 3
cpp_standard:
- 11
- 14
- 17
description: From syntax elements to working with the STL, master the core usage
  of C++ lambda expressions
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 13
related:
- Deep Dive into Lambda Capture
- std::function, std::invoke, and Callable Objects
tags:
- host
- cpp-modern
- intermediate
- lambda
title: 'Lambda Basics: The Elegant Expression of Anonymous Functions'
translation:
  source: documents/vol2-modern-features/ch03-lambda/01-lambda-basics.md
  source_hash: 3216fd6df3c3f36c684f0bef37c1b6af744978980d27315b78aa2d7dc88045e4
  translated_at: '2026-09-25T15:11:29+00:00'
  engine: anthropic
  token_count: 3500
---
# Lambda Basics: The Elegant Expression of Anonymous Functions

When writing sorting logic, we have always found C function pointers and C++98 functors a bit awkward. A function pointer either sits in the global scope polluting the namespace, or you end up ferrying a `void*` context back and forth alongside a `static` member function. Functors can encapsulate state inside a class, but defining a complete class for a two-line comparison is cracking a walnut with a sledgehammer (wow, the most OOP episode yet). C++11 brought lambda expressions—essentially anonymous function objects that can be defined in place, right where they are used. No jumping to the top of the file to declare anything, no extra symbols generated for the compiler; the logic lives right next to the call site, and anyone reading the code sees it at a glance.

---

## Breaking Down the Lambda Syntax

The complete syntax of a lambda expression looks a little intimidating, but taken apart, every piece of it is intuitive:

```cpp
[capture](parameters) -> return_type { body }
```

`capture` is the capture clause, which decides how the lambda accesses variables from the enclosing scope; `parameters` is exactly the same as an ordinary function's parameter list; `-> return_type` is a trailing return type—in C++11 you may only omit it and let the compiler deduce it under specific conditions (see the next section); and `body` is just the function body. Let's start from the simplest possible lambda and add ingredients step by step:

```cpp
// A lambda that does absolutely nothing, pure slack
auto do_nothing = []() {};

// Simply returns a value
auto forty_two = []() { return 42; };

// With parameters
auto double_it = [](int x) { return x * 2; };

// Actual use: call it like a normal function
int result = double_it(21);  // result == 42
```

You will notice that the lambda is received with `auto`—that is because every lambda expression generates a unique, nameless class type (the so-called closure type), and there is no way for you to spell that type's name out directly. `auto` is simply the most natural choice here.

---

## Return Type Deduction

C++11's rules for lambda return type deduction are relatively strict: the compiler can deduce the return type automatically only when the lambda body satisfies the following conditions:

1. The function body contains a single `return` statement, or
2. The expressions returned by all `return` statements deduce to the same type

When these conditions hold, you can omit `-> return_type`:

```cpp
// Deduced as int automatically
auto square = [](int x) { return x * x; };

// Deduced as double automatically (because of the static_cast<double>)
auto divide = [](int a, int b) -> double {
    return static_cast<double>(a) / b;
};
```

If the function body is more complex—say several branches each returning along a different path—the compiler may fail to deduce, or may deduce something other than what you expected. At that point, specifying the return type explicitly is the safest move:

```cpp
auto classify = [](int x) -> int {
    if (x > 0) {
        return x * 2;
    } else if (x < 0) {
        return -x;
    }
    return 0;   // Without this line, some compilers may emit a warning
};
```

Our advice: omit the return type on simple lambdas, and write it out explicitly on complex ones. Omitting it keeps the code more compact—but on the condition that you never leave the reader guessing for ages what the return type actually is.

---

## As Arguments to STL Algorithms—the Lambda's Main Battlefield

The most common scenario for lambda expressions is serving as the predicate or operation function of an STL algorithm. You used to either pass a global function pointer or write a functor class; now you just write the lambda at the call site, and the logic is plain to see:

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

void process_data() {
    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // Find the first reading above the threshold
    auto it = std::find_if(readings.begin(), readings.end(),
                          [](int value) { return value > 50; });

    // Count how many anomalous values there are
    int anomaly_count = std::count_if(readings.begin(), readings.end(),
                                     [](int value) { return value > 80; });
    std::cout << "Anomalies: " << anomaly_count << "\n";

    // Double in place
    std::transform(readings.begin(), readings.end(), readings.begin(),
                  [](int value) { return value * 2; });

    // Custom sort: descending order
    std::sort(readings.begin(), readings.end(),
             [](int a, int b) { return a > b; });
}
```

You used to have to define `is_above_threshold()` somewhere else, and reading the code meant hopping around hunting for the definition. Now the lambda sits right next to the algorithm call—one glance tells you what the predicate is doing.

---

## Capturing External Variables—Letting the Lambda "See" Outside

By default, a lambda cannot access any variable from the enclosing scope. This is deliberate design: a lambda gets a clean sandbox and cannot accidentally touch external state. When you genuinely need to access an external variable, you declare it explicitly through the capture clause:

```cpp
int threshold = 50;

// Compile error: threshold is not in the lambda's scope
// auto check = [](int value) { return value > threshold; };

// Capture by value: copies threshold into the closure object
auto by_value = [threshold](int value) { return value > threshold; };

// Capture by reference: refers to the external threshold directly
auto by_ref = [&threshold](int value) { return value > threshold; };
```

Capture by value copies the variable at the instant the lambda is created; later modifications outside won't affect the copy inside the lambda. Capture by reference lets the lambda operate on the original variable directly. Each approach has its place, and each has its own pits—we'll devote the next article to that discussion. For now, remember just one thing: **when you only read and never write, capture by value is the safest default.**

There are also two common default-capture forms: `[=]` captures every external variable the lambda uses by value, and `[&]` captures them all by reference. They are convenient, but in production code we suggest listing the names of the variables to capture explicitly whenever possible, to avoid unintentionally capturing things you shouldn't.

```cpp
int a = 1, b = 2, c = 3;

// Capture everything by value
auto sum_all = [=]() { return a + b + c; };  // 6

// Capture everything by reference—can modify the external variables
auto increment_all = [&]() { a++; b++; c++; };
increment_all();  // a=2, b=3, c=4

// Mixed capture: a by value, b by reference
auto mixed = [a, &b]() { return a + b; };
```

---

## The Type of a Lambda—Closure Types Demystified

As mentioned earlier, every lambda expression produces a unique, anonymous class type (the closure type). That class type has an `operator()` member function whose parameters and return value are exactly the ones you wrote in the lambda. The standard only specifies the behavior; the concrete implementation is left to the compiler. Conceptually, you can understand a lambda as the compiler generating a class like this:

```cpp
// The lambda you wrote
auto greet = [](const std::string& name) -> std::string {
    return "Hello, " + name;
};

// The class the compiler conceptually generates (simplified)
struct /* a unique name generated by the compiler */ {
    std::string operator()(const std::string& name) const {
        return "Hello, " + name;
    }
};
auto greet = /* an instance of the class above */{};
```

In a real implementation, the compiler adds data members according to the lambda's capture clause, and decides whether `operator()` is const based on the `mutable` keyword. How the type name is generated is each compiler's own business (GCC uses `_Z...` mangling, Clang uses `$_0...`, and so on), and consistency across compilers is not guaranteed.

This is why you cannot write down a lambda's type name directly—the name is generated inside the compiler, and it differs across compilers and across translation units. So when storing a lambda, you either use `auto` (the type is known at compile time) or `std::function` (runtime type erasure, with extra overhead).

We have turned this correspondence into an animation—you can play it, pause it, or step through it one frame at a time to see where each of the three parts lands in the closure class:

<Anim id="lambda-anatomy" />

Passing a lambda as a template parameter is a common zero-overhead abstraction technique—the compiler sees the lambda's complete type and gets a chance to inline it:

```cpp
template<typename Func>
void call_func(Func f) {
    f();
}

call_func([]() { /* ... */ });  // The type is visible to the compiler, may be inlined
```

The operative word here is "may": whether inlining really happens depends on the compiler's optimization strategy, how complex the lambda is, the compile options, and other factors. But compared with `std::function`'s runtime indirect call, a template parameter at least gives the compiler the opportunity to optimize.

> **On the cost of `std::function`**: internally, `std::function` uses type erasure and the Small Buffer Optimization (SBO). In libstdc++, a `std::function` object typically occupies 32 bytes (on a 64-bit system), even when the lambda it stores needs only 1 byte. Each call adds a virtual-function-style indirect jump, which may prevent inlining. If you don't need runtime polymorphism, prefer `auto` or a template parameter. We'll expand on this in depth in the fourth article, "std::function, std::invoke, and Callable Objects".

---

## Hands-On: An Event Handling System

Let's use lambdas to build a simple event handling system—a very common requirement in real projects: registering callbacks and triggering them, where the callbacks may come from different modules, each with its own context:

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

// Usage example
void setup_system() {
    EventDispatcher dispatcher;
    int press_count = 0;
    uint32_t last_press_time = 0;

    // Register the key-press callback: captures press_count and last_press_time by reference
    dispatcher.on_event(0, [&](uint32_t timestamp) {
        if (timestamp - last_press_time > 50) {   // 50ms debounce
            press_count++;
            last_press_time = timestamp;
            std::cout << "Press #" << press_count
                      << " at " << timestamp << "ms\n";
        }
    });

    // Register the timeout callback: captures threshold by value
    uint32_t threshold = 1000;
    dispatcher.on_event(1, [threshold](uint32_t timestamp) {
        if (timestamp > threshold) {
            std::cout << "Timeout at " << timestamp << "ms\n";
        }
    });

    // Simulate event triggers
    dispatcher.trigger(0, 100);
    dispatcher.trigger(0, 160);   // 60ms since the last press, passes the debounce
    dispatcher.trigger(0, 180);   // 20ms since the last press, filtered out by the debounce
    dispatcher.trigger(1, 1200);
}
```

Output:

```text
Press #1 at 100ms
Press #2 at 160ms
Timeout at 1200ms
```

You can see how naturally lambdas work as callbacks—the capture clause pulls in the context variables you need, the body holds the business logic, and you just pass the whole thing in when registering. Compared with the C-style `void (*callback)(void* user_data)` paired with a `void*` cast, both type safety and readability are in a different league.

---

## Generic Lambdas in C++14

C++14 brought lambdas a very practical enhancement: parameter types can be `auto`. This turns the lambda into a template function object—the compiler generates a separate instantiation of `operator()` for each different argument type:

```cpp
// Generic lambda: accepts any type that supports operator+
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

Generic lambdas are especially handy when writing generic algorithms and utility functions—you no longer need to wrap the lambda in an outer template function. We'll explore this in depth in the third article, "Generic Lambdas and Template Lambdas".

---

## Common Pitfalls

### Don't Let Lambda Bodies Grow Too Long

A lambda's strength is in-place definition and compact logic. If a lambda exceeds 5-7 lines, it's time to consider extracting it into a named function or a functor. Lambdas beyond that length actually hurt readability—readers have to scroll through several screens inside an algorithm's argument list, which defeats the original intent of "logic at the point of use".

### The Lifetime Trap of Capture by Reference

This is one of the most common sources of lambda bugs: a variable captured by reference is already destroyed by the time the lambda executes. The classic scenario is creating a lambda inside a function and returning it:

```cpp
// Dangerous! The returned lambda refers to the local variable local
auto make_bad_lambda() {
    int local = 42;
    return [&local]() { return local; };   // local is destroyed after the function returns
}

// Safe: capture by value
auto make_safe_lambda() {
    int local = 42;
    return [local]() { return local; };    // the lambda holds a copy
}
```

Capture by reference is not wrong in itself—you just have to guarantee that the referenced object outlives the lambda. In event systems and asynchronous callbacks, this constraint is remarkably easy to overlook.

### Prefer `auto` over `std::function` for Storing Lambdas

Unless you need runtime polymorphism (for example, putting callbacks of different types into the same container), don't use `std::function` to store a lambda. `auto` holds the closure type directly; the type's size equals the size of the captured data members (a captureless lambda is usually just 1 byte), and it gives the compiler a chance to inline; `std::function` applies type erasure, with a fixed overhead (32-64 bytes) and an extra indirect jump on every call.

```cpp
// Type known at compile time, size = 1 byte (no captures), may be inlined
auto f = [](int x) { return x * 2; };

// Type-erased, size = 32 bytes (libstdc++), indirect call at runtime
std::function<int(int)> g = [](int x) { return x * 2; };
```

This difference can matter on performance-critical paths, but avoid premature optimization too: if the code isn't on a hot path, the convenience of `std::function` may well matter more.

---

## Run It Online

Run the lambda event handling system example online and observe the actual behavior of capture by reference and capture by value:

<OnlineCompilerDemo
  title="Lambda Basics: An Event Handling System"
  source-path="code/examples/vol2/08_lambda_basics.cpp"
  description="Run it online and observe how reference capture and value capture actually behave during event dispatch."
  allow-run
/>

## References

- [Lambda expressions (C++11) - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++14 generic lambdas - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Generic_lambdas)
