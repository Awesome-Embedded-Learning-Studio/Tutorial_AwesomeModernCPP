---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: The semantics and pitfalls of capture by value, capture by reference,
  and init capture
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Lambda Basics: The Elegant Expression of Anonymous Functions'
reading_time_minutes: 15
related:
- Generic Lambdas and Template Lambdas
tags:
- host
- cpp-modern
- intermediate
- lambda
title: Deep Dive into Lambda Capture
translation:
  source: documents/vol2-modern-features/ch03-lambda/02-lambda-capture.md
  source_hash: 57ec95d9960c0c258340895058ac75c1a92b854f094e53a0767c115dd35350a7
  translated_at: '2026-09-27T05:01:28+00:00'
  engine: anthropic
  token_count: 7200
---
# Lambda Capture: What Do [=] and [&] Actually Capture

In the previous article we raced through the basic syntax of lambdas and briefly mentioned that a capture list exists. But a few questions have probably been nagging at you: what exactly does capture by value copy? Under the hood, is capture by reference really just storing a pointer? What traps do default captures like `[=]` and `[&]` hide? And what exactly is so great about C++14's init capture? In this article we take the capture mechanism apart from end to end—not just how to use it, but what the compiler does behind the scenes and which usages blow up at runtime.

---

## Capture by Value—Copy a Member into the Closure Object

The semantics of capture by value are refreshingly direct: at the moment the lambda is created, each captured variable is copied and stored as a member variable of the closure type. Any later modification of the outer variable has no effect on the copy inside the lambda.

```cpp
void demo_value_capture() {
    int threshold = 100;

    // threshold is copied into the closure object
    auto is_high = [threshold](int value) {
        return value > threshold;
    };

    threshold = 200;             // modify the outer variable
    bool result = is_high(150);  // false — the threshold inside the lambda is still 100
}
```

From the compiler's point of view, the lambda above gets translated roughly into this closure type:

```cpp
struct ClosureType {
    int threshold;  // the captured variable becomes a member

    bool operator()(int value) const {
        return value > threshold;
    }
};

auto is_high = ClosureType{100};  // copies threshold at construction
```

Notice that `const`—members captured by value are `const` inside `operator()` by default, so you cannot modify them. If you really do need to modify the captured copy inside the lambda, add the `mutable` keyword:

```cpp
int counter = 0;

// Compile error: counter is a const int inside the lambda
// auto bad = [counter]() { counter++; };

// With mutable: modifying the lambda's internal copy is allowed
auto make_counter = [counter]() mutable {
    return ++counter;   // modifies the closure object's own counter, not the outer one
};

std::cout << make_counter() << "\n";  // 1
std::cout << make_counter() << "\n";  // 2
std::cout << counter << "\n";         // 0 — the outer counter was never touched
```

`mutable` tells the compiler: this lambda's `operator()` is not `const`. Every call may modify state inside the closure object. That is why each call to `make_counter()` increments—the closure object maintains its own independent state.

---

## Capture by Reference—Storing the Address of the Original Variable

Capture by reference is no mystery either: what the compiler stores in the closure type is a pointer to the captured variable (or a reference—the two are practically equivalent at the implementation level). We can verify this with `sizeof`: a closure object that captures by reference is the size of a pointer (8 bytes on a 64-bit system). Reads and writes to the captured variable inside the lambda are in fact operations on the original variable.

```cpp
void demo_ref_capture() {
    int sum = 0;

    auto accumulate = [&sum](int value) {
        sum += value;   // directly modifies the outer sum
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
    int& sum;  // a reference is what gets stored

    void operator()(int value) const {
        sum += value;  // modifies the outer variable through the reference
    }
};
```

Here is a delightful detail: `operator()` is `const`, yet we modified an outer variable through `sum`. That works because the reference itself (the stored address) is `const`—you cannot point the reference at another object—but the value of the object it binds to is modifiable. It is the same story as `int* const ptr`: you cannot change the pointer, but you can change `*ptr`.

> **Verify it**: you can run `code/volumn_codes/vol2/ch03-lambda/test_ref_capture_impl.cpp` to check the low-level implementation details of reference capture and its `const` semantics.

The biggest advantage of capture by reference is zero copying—for large objects (a `std::vector` or `std::string`, say) it avoids needless duplication. But the biggest risk lives in exactly the same place: **the referenced variable must outlive the lambda**.

Put the memory layouts of the two capture styles side by side, and the dangling-reference risk becomes plain to see:

![Memory layout of closure objects under value capture vs. reference capture](./02-lambda-capture-layout.drawio)

---

## Default Capture—The Hidden Risks of `[=]` and `[&]`

When a lot of variables need capturing, listing them one by one does get tedious. C++ offers two default capture modes: `[=]` means every outer variable you use gets captured by value, and `[&]` means all of them get captured by reference.

```cpp
void demo_default_capture() {
    int a = 1, b = 2, c = 3;

    // capture everything by value
    auto sum = [=]() { return a + b + c; };   // 6

    // capture everything by reference
    auto increment = [&]() { a++; b++; c++; };
    increment();   // a=2, b=3, c=4
}
```

On top of a default capture you can also give individual variables a different mode—mixed capture:

```cpp
void demo_mixed_capture() {
    int threshold = 100;
    int count = 0;
    double factor = 1.5;

    // default capture by value, but count by reference
    auto process = [=, &count](int value) {
        if (value > threshold) {
            count++;
            return static_cast<int>(value * factor);
        }
        return value;
    };
}
```

Sounds convenient, but `[=]` and `[&]` hide a few less-than-obvious traps. The `[=]` default value capture does not capture the `this` pointer—wait, no, hold on: before C++20, `[=]` actually could implicitly capture `this`, and that bred a classic bug: you thought you were capturing the value of a member variable, but what got captured was the `this` pointer, so `this->member` inside the lambda still reached the original object's member. C++20 corrected this behavior: `[=]` no longer implicitly captures `this`; you must explicitly write `[=, this]` or `[=, *this]`.

> **Verify it**: you can run `code/volumn_codes/vol2/ch03-lambda/test_cxx20_default_capture.cpp` to watch how C++17 and C++20 differ on default-capturing `this` (C++20 emits a warning).

Our advice: **in production code, spell out the variable names you capture** and go easy on `[=]` and `[&]`. The explicit version pays off in code review—you can see at a glance which external state a lambda depends on, and you avoid unintentionally capturing things you should not. (Capture-everything is fine only when the code itself is trivially simple; otherwise, not knowing exactly what you grabbed is a problem waiting to happen.)

---

## C++14 Init Capture—A Lambda with Its Own State

C++14 introduced init capture, sometimes also called generalized lambda capture. The syntax is `name = expression` inside the capture list, where `name` is a brand-new variable name and `expression` is the initializing expression. That variable belongs entirely to the closure object and has nothing to do with the outside world:

```cpp
void demo_init_capture() {
    int base = 10;

    // capture the result of base + 5, not base itself
    auto lam = [value = base + 5]() {
        return value * 2;   // value == 15
    };
}
```

Where init capture shines most is **move capture**—moving move-only types (`std::unique_ptr`, `std::thread`, and friends) into the closure object:

```cpp
#include <memory>

auto make_handler() {
    auto ptr = std::make_unique<int>(42);

    // move the unique_ptr into the lambda
    return [p = std::move(ptr)]() {
        return *p;   // p is owned exclusively by the lambda
    };
}
```

To pull off the same effect in C++11, you had to hand-write a functor class with the `unique_ptr` as a member variable. C++14's init capture makes the whole thing perfectly natural.

Another common use replaces the `mutable` counter with an init capture, which reads more clearly:

```cpp
// C++11 style: mutable required
int x = 0;
auto counter_old = [x]() mutable { return ++x; };

// C++14 style: init capture, clearer semantics
auto counter_new = [count = 0]() mutable { return ++count; };
```

The second version wins because `count` is purely the lambda's own state, unrelated to the outer variable `x`—the name alone tells you it is an independent counter.

---

## C++17 `*this` Capture—Capturing the Whole Object by Value

When you write a lambda inside a member function and want to capture the current object, the traditional spelling is `[this]`. But `[this]` captures a pointer: if the lambda outlives the object itself, you are left holding a dangling `this` pointer. C++17 introduced `[*this]`, which captures the entire object by value—storing a copy of the object inside the closure type:

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
        // [*this]: copies the whole Sensor object into the closure
        // even if the original Sensor is destroyed, the lambda stays safe
        return [*this]() mutable {
            return ++reading_;
        };
    }

    std::function<int()> make_reader_unsafe() {
        // [this]: stores only a pointer; it dangles once the object is destroyed
        return [this]() {
            return ++reading_;   // danger!
        };
    }
};

void demo_star_this() {
    std::function<int()> reader;

    {
        Sensor s("temperature");
        reader = s.make_reader();      // [*this]: safe
        // reader_unsafe = s.make_reader_unsafe();  // [this]: dangerous
    }
    // s has been destroyed

    std::cout << reader() << "\n";     // safe: the lambda holds a copy of s
    std::cout << reader() << "\n";     // 2
}
```

The cost of `[*this]` is copying the entire object. If the object is big (holding a `std::vector`, a large `std::array`, and so on), that copy can be far from cheap. But for small configuration objects and value objects, the safety this copy buys is more than worth it.

**Note**: `[*this]` requires the lambda's surrounding context to be a member function where `this` is dereferenceable. You cannot use `[*this]` in a static member function or a non-member function.

---

## Capture Traps—Dangling References and Lifetimes

Lifecycle problems are the most common—and most headache-inducing—source of capture-related bugs. Let's walk through a few classic trap scenarios.

### Returning a Lambda That Captures by Reference

```cpp
// Classic trap: returning a lambda that references a local variable
auto make_dangling() {
    int count = 0;
    return [&count]() { return ++count; };
    // count is destroyed after the function returns; the lambda holds a dangling reference
}

auto bad = make_dangling();
// bad() is undefined behavior!
```

The fix is simple—swap the reference capture for a value capture or an init capture:

```cpp
auto make_safe() {
    int count = 0;
    return [count]() mutable { return ++count; };    // value capture: safe
}

auto make_safe2() {
    return [count = 0]() mutable { return ++count; }; // init capture: clearer
}
```

### Reference Capture in Loops

This trap shows up especially often in asynchronous programming and event systems:

```cpp
#include <vector>
#include <functional>

std::vector<std::function<void()>> handlers;

void demo_loop_trap() {
    for (int i = 0; i < 5; ++i) {
        // wrong: every lambda references the same i; after the loop ends, i == 5
        handlers.push_back([&i]() {
            std::cout << i << " ";   // all of them print 5
        });
    }

    handlers.clear();

    for (int i = 0; i < 5; ++i) {
        // correct: each lambda gets its own copy of i
        handlers.push_back([i]() {
            std::cout << i << " ";   // prints 0 1 2 3 4
        });
    }
}
```

### The Risks of Capturing `this`

```cpp
class Device {
    std::string name_ = "sensor";

public:
    auto get_handler() {
        // if the Device object is destroyed before the lambda runs, this dangles
        return [this]() { return name_; };
    }

    // safer: capture the members you need, not this
    auto get_handler_safe() {
        return [name = name_]() { return name; };
    }

    // C++17, safest: capture the whole object by value
    auto get_handler_safest() {
        return [*this]() { return name_; };
    }
};
```

---

## Analyzing the Size of Lambda Objects

Once you understand how captures are stored under the hood, the size of a lambda object is easy to reason about—it is the sum of the sizes of all captured variables (plus, possibly, some alignment padding). A standard lambda carries no vtable pointer; the closure type is a plain class type. We can verify this with `sizeof`:

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
    // usually 1 byte (the empty-class special case)

    std::cout << "capture_int:   " << sizeof(capture_int) << " bytes\n";
    // usually 4 bytes (one int)

    std::cout << "capture_ref:   " << sizeof(capture_ref) << " bytes\n";
    // usually 8 bytes (one pointer, on a 64-bit system)

    std::cout << "capture_both:  " << sizeof(capture_both) << " bytes\n";
    // usually 16 bytes (int + double reference/pointer, accounting for alignment)
}
```

This verification program is right below—hit "Run It" and see for yourself (on a 64-bit system):

<OnlineCompilerDemo
  title="Hands-On: The Size of a Lambda Closure"
  source-path="code/examples/vol2/34_lambda_capture_size.cpp"
  description="Check closure sizes online: no capture is 1 byte, capturing an int by value is 4 bytes, reference capture is a pointer at 8 bytes, and mixed capture pads out to 16 bytes with alignment."
  run-options="-std=c++17"
  allow-run
/>

One point worth noting: a capture-free lambda is usually 1 byte rather than 0 bytes—C++ does not allow zero-sized objects (otherwise the addresses of elements in an array would be indistinguishable). Reference capture, meanwhile, stores a pointer, which takes 8 bytes on a 64-bit system.

> **Verify it**: you can run `code/volumn_codes/vol2/ch03-lambda/test_capture_size.cpp` to see the actual size of closure objects under each capture style.

When you store a lambda in a `std::function`, the footprint grows beyond that—a `std::function` typically carries its own SBO buffer (32-64 bytes) plus type-erasure management overhead. That is why, in the previous article, we said to "prefer `auto` for storing lambdas".

---

## Performance—When It Inlines, and When It Cannot

A lambda's performance profile is tightly bound to how it captures and how it is stored.

When a lambda is invoked through a type known at compile time (`auto` or a template parameter), the compiler sees the full closure type and the `operator()` implementation and can inline it perfectly. At that point the difference between value capture and reference capture is essentially zero—even though value capture adds one copy, the optimizer can usually eliminate that copying cost.

But once the lambda goes into a `std::function`, the story changes. `std::function`'s type erasure introduces a layer of indirection that the compiler cannot inline across. And if the captured state exceeds the `std::function` SBO buffer size, a heap allocation kicks in on top.

```cpp
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>

void benchmark_lambda_styles() {
    std::vector<int> data(1'000'000);
    int threshold = 50;

    // Style 1: auto + algorithm template parameter — fully inlined
    auto start = std::chrono::high_resolution_clock::now();
    auto count1 = std::count_if(data.begin(), data.end(),
                               [threshold](int x) { return x > threshold; });
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "auto lambda: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";

    // Style 2: std::function — indirect-call overhead
    std::function<bool(int)> pred = [threshold](int x) { return x > threshold; };
    start = std::chrono::high_resolution_clock::now();
    auto count2 = std::count_if(data.begin(), data.end(), pred);
    end = std::chrono::high_resolution_clock::now();
    std::cout << "std::function: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";
}
```

With optimizations enabled (-O2/-O3), the `auto` version is typically about 2-3x faster than the `std::function` version (exact numbers depend on the compiler, the optimization level, and the lambda's complexity). Benchmarks (GCC 13.2.0, -O3) show that when processing 10 million elements, the `auto` version takes about 6-7 ms while the `std::function` version takes about 14-15 ms. The trend is consistent: **when you do not need runtime polymorphism, passing lambdas via templates or `auto` is the optimal choice.**

> **Verify it**: you can run `code/volumn_codes/vol2/ch03-lambda/benchmark_performance.cpp` to reproduce this performance test (compile with -O3 optimization).

---

## Choosing a Capture Mode—A Decision Guide

Let's boil the choice of capture mode down to a few simple rules:

For small immutable data (`int`, `float`, simple structs), value capture is the safest default. It keeps the lambda free of external state, thread-safe, and immune to lifetime problems. For large objects (`std::vector`, `std::string`) that the lambda only reads without modifying, reference capture plus `const` is the zero-copy solution; if the lambda needs to hold the object independently, use an init capture `name = std::move(obj)` to move it into the closure. For outer variables that must be modified inside the lambda (accumulators, state updates), reference capture is the most natural choice—just make sure the variable lives long enough.

Inside a member function, `[this]` is convenient as long as the lambda does not escape the object's lifetime; if the lambda may outlive the object, use `[*this]` (C++17) or init-capture the member variables you need. In production code, we strongly recommend listing captured variable names explicitly and avoiding `[=]` and `[&]`—explicit code makes code review easier and cuts down on accidental captures.

---

## Run It Online

Run the lambda capture examples online and compare the effects of the different capture modes:

<OnlineCompilerDemo
  title="Lambda Capture: Value Capture, Reference Capture, and Closure Size"
  source-path="code/examples/vol2/09_lambda_capture.cpp"
  description="Run online and compare how value capture, reference capture, mutable, and init capture behave differently."
  allow-run
/>

## References

- [Lambda capture - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
- [C++14 generalized lambda capture](https://en.cppreference.com/w/cpp/language/lambda#Captures)
- [C++17 capture *this](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
