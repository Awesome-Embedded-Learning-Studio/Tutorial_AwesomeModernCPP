---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: The semantics and pitfalls of capture by value, capture by reference, and init capture
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 3: Lambda Basics: Elegant Anonymous Functions'
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
  source_hash: 96ff96341246516c78f4896150ac93b9b635a4433394023ab28057adeb8bc492
  translated_at: '2026-09-27T10:30:22+00:00'
  engine: anthropic
  token_count: 3600
---
# Deep Dive into Lambda Capture

In the previous article we walked through the basic syntax of lambdas and got a quick feel for capture by value and capture by reference; the details were left for this article. What do `[=]` and `[&]` actually capture? Is what capture by value copies in really just a copy of the variable? Does capture by reference, under the hood, merely store a pointer? We'll crack these three questions open — the answers are all hiding in the code the compiler generates. In this article we take the capture mechanism apart from end to end, and learn to recognize the patterns that blow up at runtime.

---

## Capture by Value — Copy a Duplicate into the Closure Object

The semantics of capture by value are dead simple: the moment the lambda is created, each captured variable is copied and stored as a member variable of the closure type. Whatever you change on the outside afterwards can't touch the copy inside the lambda.

```cpp
void demo_value_capture() {
    int threshold = 100;

    // threshold is copied into the closure object
    auto is_high = [threshold](int value) {
        return value > threshold;
    };

    threshold = 200;             // modify the outer variable
    bool result = is_high(150);  // true; the threshold inside the lambda is still the copy holding 100
}
```

From the compiler's point of view, the lambda we wrote above gets translated roughly into this closure type:

```cpp
struct ClosureType {
    int threshold;  // the captured variable became a member

    bool operator()(int value) const {
        return value > threshold;
    }
};

auto is_high = ClosureType{100};  // threshold is copied at construction
```

Note that `const`: members brought in by value capture are `const` inside `operator()` by default, and you can't modify them. If you really do need to modify the captured copy inside the lambda, it's time to bring out the `mutable` keyword:

```cpp
int counter = 0;

// Compile error: counter is a const int inside the lambda
// auto bad = [counter]() { counter++; };

// Add mutable: modifying the copy inside the lambda is now allowed
auto make_counter = [counter]() mutable {
    return ++counter;   // modifies the closure object's own counter, not the outer one
};

std::cout << make_counter() << "\n";  // 1
std::cout << make_counter() << "\n";  // 2
std::cout << counter << "\n";         // 0 — the outer counter was never touched
```

Adding `mutable` is our way of telling the compiler: this lambda's `operator()` is no longer `const`, and every call may touch the state inside the closure object. That's why each call to `make_counter()` increments — the closure object maintains its own independent state, and the outer `counter` is never touched from beginning to end.

---

## Capture by Reference — Store the Address of the Original Variable

The semantics of capture by reference aren't hard to guess either: what the compiler stores in the closure type is a pointer to the captured variable (in the underlying implementation, a reference is essentially equivalent to a pointer). We can verify this with `sizeof`: a closure object that captures by reference is exactly the size of a pointer — 8 bytes on a 64-bit system. Reads and writes to the captured variable inside the lambda are in fact reads and writes to the original outer variable.

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

Let's translate this one into a closure type as well:

```cpp
struct ClosureType {
    int& sum;  // what gets stored is a reference

    void operator()(int value) const {
        sum += value;  // modifies the outer variable through the reference
    }
};
```

There's a very interesting detail here: `operator()` is declared `const`, yet we modify the outer variable through `sum`. That works because the reference itself (the stored address) is `const` — you can't make it point to a different object — but the value of the object it binds to is yours to change as you please. It's the same logic as the familiar `int* const ptr`: the pointer itself can't move, but `*ptr` is fair game.

Mechanism talk aside, here comes the proof — the lambda and the hand-written `ClosureType` above run side by side; click "Try It Out" and see for yourself:

<OnlineCompilerDemo
  title="Hands-On Verification: the const semantics of reference capture"
  source-path="code/examples/vol2/58_ref_capture_const.cpp"
  description="Compare the lambda against a hand-written closure type online: both accumulate the outer variable to 60 — a const operator() modifies it all the same; a closure holding one reference is exactly one pointer, 8 bytes."
  run-options="-std=c++17"
  allow-run
/>

The biggest win of capture by reference is zero copies: large objects like `std::vector` and `std::string` aren't cheap to copy even once, and reference capture skips that cost entirely. If you only read and never write, capture by reference plus `const` is enough — not a single byte of copying to pay for. But the risk follows right along: **you must guarantee that the referenced variable outlives the lambda**.

Put the memory layouts of the two capture modes side by side, and the dangling reference risk becomes visible: the reference still points somewhere, but the object it points to has already been destroyed.

![Memory layout of closure objects under capture by value and capture by reference](./02-lambda-capture-layout.drawio)

---

## Default Capture — The Hidden Risks of `[=]` and `[&]`

Once the list of variables to capture grows long, listing them one by one stops being practical. So C++ simply offers two default captures: `[=]` means every outer variable the lambda uses gets captured by value, `[&]` means all of them get captured by reference.

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

On top of a default capture you can also give individual variables a different mode — we call this mixed capture:

```cpp
void demo_mixed_capture() {
    int threshold = 100;
    int count = 0;
    double factor = 1.5;

    // default capture by value, but count is captured by reference
    auto process = [=, &count](int value) {
        if (value > threshold) {
            count++;
            return static_cast<int>(value * factor);
        }
        return value;
    };
}
```

Convenient as they are, `[=]` and `[&]` hide a few inconspicuous problems. Take `[=]` capturing `this`. You may have heard the claim that `[=]` doesn't capture `this`. Hold on — before C++20, that claim was simply wrong: `[=]` really can implicitly capture `this`. The classic misunderstanding grows right out of this. You think what got copied in is the value of a member variable, but the only thing that came in is the `this` pointer. Access through `this->member` inside the lambda still reaches members of the original object. As of C++20 this spelling is officially deprecated — the compiler fires a warning at you, though the behavior stays compatible for now. To capture explicitly, write `[=, this]` or `[=, *this]`.

Want to see what that warning looks like? Click "Try It Out" below — the diagnostics panel carries the `-Wdeprecated` warning while the program still prints 42. To see how quiet C++17 is about it, click "Open in Godbolt", change `-std=c++20` to `c++17`, and compile again — the warning is gone.

<OnlineCompilerDemo
  title="Hands-On Verification: C++20 deprecates implicit this capture via [=]"
  source-path="code/examples/vol2/57_default_capture_this.cpp"
  description="Compiling online (GCC 15.2, -std=c++20) surfaces the -Wdeprecated warning plus the suggested fix; [=], [=, this] and [*this] all return 42 — the warning is about style, not behavior."
  run-options="-std=c++20"
  allow-run
/>

Our recommendation: **in production code, write out the names of the variables you capture, one by one** — avoid `[=]` and `[&]` whenever we can. The benefit of listing them explicitly is that during code review you can see at a glance which external state the lambda depends on, and nothing that shouldn't be captured gets swept in along the way.

> Of course, we're not saying default captures must never be written — provided your code is plain and simple enough. Otherwise you have no idea what the lambda ended up holding, and when something goes wrong, tracing it back is painful.

---

## C++14 Init Capture — A Lambda with Its Own State

C++14 introduced init capture; we sometimes also call it generalized lambda capture, which is the original English name. All we do is write `name = expression` in the capture list: `name` is a brand-new variable name, and `expression` is the expression used to initialize it. This new variable belongs entirely to the closure object and has nothing to do with the value of any outer variable:

```cpp
void demo_init_capture() {
    int base = 10;

    // capture the result of base + 5, not base itself
    auto lam = [value = base + 5]() {
        return value * 2;   // value == 15
    };
}
```

The most useful scenario for init capture is **move capture**: we can move move-only types like `std::unique_ptr` and `std::thread` into the closure object:

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

In C++11, to get the same effect you had to hand-write a functor class and make the `unique_ptr` one of its member variables. With C++14 init capture, the same thing reads perfectly naturally.

Another common use is replacing the `mutable` counter with init capture — the semantics come out much clearer:

```cpp
// C++11 style: mutable required
int x = 0;
auto counter_old = [x]() mutable { return ++x; };

// C++14 style: init capture, clearer semantics
auto counter_new = [count = 0]() mutable { return ++count; };
```

The nice thing about the second version is that `count` belongs entirely to the lambda and has nothing to do with the outer variable `x` — the name alone tells you it's an independent counter.

---

## C++17 `*this` Capture — Capture the Whole Object by Value

When you write a lambda inside a member function and want to capture the current object, the traditional spelling is `[this]`. But `[this]` captures a pointer: if the lambda outlives the object itself, all you're left holding is a dangling `this` pointer. C++17 introduced `[*this]`, which captures the whole object by value, storing a copy of the object inside the closure type:

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
        // [*this]: copies the entire Sensor object into the closure
        // even if the original Sensor is destroyed, the lambda stays safe
        return [*this]() mutable {
            return ++reading_;
        };
    }

    std::function<int()> make_reader_unsafe() {
        // [this]: stores only the pointer; it dangles once the object is destroyed
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

The price of `[*this]` is copying the entire object. If the object is large (a type holding a `std::vector` or a big `std::array`), that copy doesn't come cheap. But when the object is small — configuration objects, value objects, that kind of thing — paying one extra copy in exchange for safety is, we think, well worth it.

**Note**: `[*this]` comes with a precondition — the lambda must sit inside a member function where `this` can be dereferenced. Inside a static member function or a non-member function, `[*this]` simply isn't available.

---

## Dangling References and Lifetimes

The most common and most headache-inducing source of bugs in the capture mechanism is lifetime trouble: the lambda is still alive while the variables it references are already gone. Let's look straight at a few patterns that go wrong easily.

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

The fix is straightforward: replace reference capture with value capture or init capture:

```cpp
auto make_safe() {
    int count = 0;
    return [count]() mutable { return ++count; };    // value capture: safe
}

auto make_safe2() {
    return [count = 0]() mutable { return ++count; }; // init capture: even clearer
}
```

### Reference Capture in Loops

This is an error we run into especially easily in async programming and event systems:

```cpp
#include <vector>
#include <functional>

std::vector<std::function<void()>> handlers;

void demo_loop_trap() {
    for (int i = 0; i < 5; ++i) {
        // Wrong: all the lambdas reference the same i, which died with the loop
        handlers.push_back([&i]() {
            std::cout << i << " ";   // calling them later hits a dangling reference; in practice this often prints garbage
        });
    }

    handlers.clear();

    for (int i = 0; i < 5; ++i) {
        // Correct: each lambda gets its own copy of i
        handlers.push_back([i]() {
            std::cout << i << " ";   // prints 0 1 2 3 4
        });
    }
}
```

We turned the difference between `[&i]` and `[i]` in a loop into an animation: with `[&i]`, all five closures reference the same `i`, and once the loop ends, calling them prints garbage; with `[i]`, each closure gets its own copy of `i`, and the output is 0 1 2 3 4.

<Anim id="lambda-loop-capture" />

### The Risks of Capturing `this`

Let's lay out three ways to write the same requirement, from the riskiest to the most solid:

```cpp
class Device {
    std::string name_ = "sensor";

public:
    auto get_handler() {
        // If the Device object is destroyed before the lambda runs, this dangles
        return [this]() { return name_; };
    }

    // Safer: capture the members you need, not this
    auto get_handler_safe() {
        return [name = name_]() { return name; };
    }

    // C++17, safest: capture the entire object by value
    auto get_handler_safest() {
        return [*this]() { return name_; };
    }
};
```

---

## Analyzing the Size of Lambda Objects

With the underlying storage cleared up, the size of a lambda object is easy to work out: it is the sum of the sizes of all captured variables, possibly with some alignment padding in between. A standard lambda has no vtable pointer — the closure type is just a plain class type. We can verify this with `sizeof`:

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
    // usually 16 bytes (int + a double reference/pointer, with alignment)
}
```

We've put the verification program below — click "Try It Out" and it runs directly (on a 64-bit system):

<OnlineCompilerDemo
  title="Hands-On Verification: the size of lambda closures"
  source-path="code/examples/vol2/34_lambda_capture_size.cpp"
  description="Check closure sizes online: no capture is 1 byte, capturing an int by value is 4 bytes, reference capture stores a pointer at 8 bytes, and mixed capture with alignment comes to 16 bytes."
  run-options="-std=c++17"
  allow-run
/>

About that 1 byte for a captureless lambda, let's ask one more question: why is it not 0? Because C++ doesn't allow objects of size 0 — otherwise the elements of an array would have no distinguishable addresses. Reference capture stores a pointer, which takes 8 bytes on a 64-bit system.

Store the lambda in a `std::function` and the footprint grows beyond this: the `std::function` object itself is typically 32-64 bytes — that's where the small buffer optimization (SBO) mentioned in the previous article lives — plus the management overhead of type erasure on top. That's exactly why the "Storing a Lambda" section in that article urged you to use `auto` whenever you can.

---

## Performance — When It Inlines, and When It Cannot

How fast a lambda is depends closely on how it captures and how it is stored; let's look at two tiers. When a lambda is called through a type known at compile time (`auto` or a template parameter), the compiler sees the complete closure type and the `operator()` implementation, and can confidently inline the whole thing. At that point the difference between value capture and reference capture essentially drops to zero: even though value capture adds one copy, the compiler can usually eliminate that copying cost after optimization.

But once you store the lambda in a `std::function`, the story changes. Type erasure adds a layer of indirection to the call; the compiler can't see through that indirection, and inlining is off the table. And as soon as the captured content outgrows the SBO buffer, heap allocation enters the picture.

```cpp
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>

void benchmark_lambda_styles() {
    std::vector<int> data(1'000'000);
    int threshold = 50;

    // Style 1: auto + the algorithm's template parameter — fully inlined
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

With optimizations on (-O2/-O3), the `auto` version is consistently faster than `std::function` — by anywhere from 2-3x to nearly 10x: the newer the compiler and the more aggressive the inlining and vectorization, the faster the `auto` side gets. The sample code above uses 1 million elements; the benchmark program scales that up to 10 million and sits right below (with -O3 already configured) — click "Try It Out" and it runs:

<OnlineCompilerDemo
  title="Hands-On Measurement: the cost of auto vs std::function"
  source-path="code/examples/vol2/55_lambda_perf_benchmark.cpp"
  description="A count_if over 10 million ints (GCC 15.2, -O3): one run of ours measured auto at 6.3 ms vs std::function at 20.9 ms (3.3x); across 100 million iterations, value capture vs no capture came out 269 ms vs 268 ms — the copy cost is optimized away entirely. Absolute numbers drift with the environment; the ratio is the point."
  run-options="-O3 -std=c++17"
  allow-run
/>

The numbers drift from machine to machine, but the direction holds: `std::function`'s indirect call blocks inlining and costs a clear margin; the one extra copy a value capture pays is essentially erased after optimization — the near-identical millisecond figures over 100 million iterations are the proof. So the conclusion isn't hard to draw: when you don't need runtime polymorphism, passing lambdas via templates or `auto` is all you need.

---

## Choosing a Capture Mode — A Decision Guide

The choice of capture mode can be boiled down to a few simple rules:

For small immutable data (`int`, `float`, simple structs), capture by value directly — it is the safest default: the lambda depends on no external state, it is thread-safe, and there is no lifetime question. When it's a large object's turn (`std::vector`, `std::string`), we have to ask one question: does the lambda only read it, or does it want to take it away? For read-only cases, capture by reference plus `const` — the zero-copy arrangement we settled on earlier in the capture-by-reference section. For taking it away, we go back to init capture and move it into the closure with `name = std::move(obj)`. As for outer variables that the lambda needs to modify (accumulators, state updates), reference capture is the most natural fit — just make sure the variable's lifetime is long enough.

When you write a lambda inside a member function, `[this]` is convenient as long as the lambda doesn't escape the object's lifetime. If you can't tell whether the lambda might outlive the object, switch to `[*this]` (C++17), or use init capture to pull in just the member variables you need. And in production code, the advice from the default-capture section still stands: list the variable names explicitly. Explicit code makes code review easier and avoids a lot of accidental captures.

---

## Run It Online

The capture examples from this article are packed into an online version, where you can compare the effects of the different capture modes:

<OnlineCompilerDemo
  title="Lambda Capture: Value Capture, Reference Capture, and Closure Size"
  source-path="code/examples/vol2/09_lambda_capture.cpp"
  description="Run online and compare the behavioral differences of capture by value, capture by reference, mutable, and init capture."
  allow-run
/>

## References

- [Lambda capture - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
- [C++14 generalized lambda capture](https://en.cppreference.com/w/cpp/language/lambda#Captures)
- [C++17 capture *this](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
