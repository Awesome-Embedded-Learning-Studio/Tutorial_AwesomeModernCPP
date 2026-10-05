---
chapter: 3
cpp_standard:
- 11
- 14
- 17
description: From syntax building blocks to working with the STL, master the core
  usage of C++ lambda expressions
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
title: 'Lambda Basics: Elegant Anonymous Functions'
translation:
  source: documents/vol2-modern-features/ch03-lambda/01-lambda-basics.md
  source_hash: 8ce39a4e72bd76b33731c4cf891388d22614fa08bfd39579b4881377863f4060
  translated_at: '2026-09-27T10:20:12+00:00'
  engine: anthropic
  token_count: 3700
---
# Lambda Basics: Elegant Anonymous Functions

In the previous chapter we gave `constexpr` a thorough workout; this chapter comes back to code we write every day. When writing sorting logic, we have always found C function pointers and C++98 functors (functor — a class with an overloaded `operator()`) a bit awkward. A function pointer either gets defined in the global scope, dirtying the namespace, or gets ferried around as a `static` member function plus a `void*` context — we have been burned by both routes. Functors do encapsulate state inside a class, but solemnly defining a complete class for a two-line comparison is a cost we find completely not worth it (wow, peak OOP). The lambda expressions C++11 brought in made this simple. A lambda is essentially an anonymous function object defined in place, right where it is used: we don't jump to the top of the file to declare anything, we don't make the compiler generate extra symbols, and the logic sits next to the call site where anyone reading the code sees it at a glance.

---

## Breaking Down Lambda Syntax

The complete syntax of a lambda expression looks a little intimidating, but once we take it apart, every piece of it is intuitive:

```cpp
[capture](parameters) -> return_type { body }
```

Let's go through them one by one: `capture` is the capture list, and it decides how the lambda accesses variables from the enclosing scope. `parameters` is exactly the parameter list of a normal function. `-> return_type` is the trailing return type; in C++11 you may omit it only under certain conditions, and only when they hold will the compiler agree to deduce it for us (more in the next section). Finally, `body` is just the function body. Let's start from the simplest lambda possible and pile on ingredients step by step:

```cpp
// A lambda that does nothing — pure slacking off
auto do_nothing = []() {};

// Simply return a value
auto forty_two = []() { return 42; };

// With parameters
auto double_it = [](int x) { return x * 2; };

// Actual usage: call it like a normal function
int result = double_it(21);  // result == 42
```

You will notice that every variable receiving a lambda above uses `auto`. The reason lies in the type: each lambda expression generates a unique, unnamed class type — the so-called closure type — and there is no way for you to write that type's name down directly. `auto` is the natural choice here.

---

## Return Type Deduction

In the C++11 era, the conditions for return type deduction were fairly strict. The compiler agrees to deduce it for us automatically in only these two situations:

1. The body contains a single `return` statement, or
2. all the `return` statements return expressions that deduce to the same type

When the conditions hold, we can leave out `-> return_type`:

```cpp
// Deduced as int automatically
auto square = [](int x) { return x * x; };

// Deduced as double (because of the static_cast<double>)
auto divide = [](int a, int b) {
    return static_cast<double>(a) / b;
};
```

Once the lambda body gets more involved — say, multiple branches returning along different paths — the compiler may no longer be able to deduce it, or it may deduce something different from what you expect. In that case, spelling out the return type explicitly is the safest move:

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

Our advice: omit the return type for simple lambdas, and write it out explicitly for complex ones. Omitting it makes the code more compact — provided we don't leave the reader guessing for ages what the return type actually is.

---

## Using Lambdas with STL Algorithms

Where do we use lambdas the most in day-to-day code? As predicates or operation functions for STL algorithms. What is a predicate? A function that takes an element and answers yes or no — exactly the kind of thing `std::find_if` and `std::count_if` eat. Before, you had only two options: pass a global function pointer, or write a dedicated functor class. Now you just write the lambda right at the call site:

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

Think back to the old way: functions like `is_above_threshold()` had to be defined somewhere else, and readers had to flip through the file for ages to match things up. Now the condition sits right in the argument of `find_if`, and one glance tells us what it is filtering for.

We turned the step-by-step judging of find_if and count_if into an animation — you can watch frame by frame where find_if stops and how far count_if has counted.

<Anim id="lambda-predicate-scan" />

---

## The Capture List: How a Lambda Gets at External Variables

By default, a lambda is a sealed-off thing: it cannot touch the local variables of the enclosing scope. That is deliberate. Without a capture list, what the lambda body can use directly is its parameters, its own local variables, and global or static variables — the one thing it cannot reach, no matter what, is the outer local variables. When you genuinely need those outer local variables, you must declare them explicitly through the capture list:

```cpp
int threshold = 50;

// Compile error: threshold is not in the lambda's scope
// auto check = [](int value) { return value > threshold; };

// Capture by value: copies threshold into the closure object
auto by_value = [threshold](int value) { return value > threshold; };

// Capture by reference: refers directly to the outer threshold
auto by_ref = [&threshold](int value) { return value > threshold; };
```

Capture by value happens at the moment the lambda is created: the variable gets copied, and later modifications on the outside cannot reach the copy inside the closure. Capture by reference skips the copy step; what the lambda operates on is the original outer variable directly. Each approach has its use cases, and each has things to watch out for — we'll leave the details to the next article, "Deep Dive into Lambda Capture". For now you only need to hold on to one rule: **when you only read and never write, capture by value is the safest default**.

There are also two common default-capture forms: `[=]` captures by value every external variable the lambda uses, and `[&]` captures all of them by reference. Both are about saving effort, but in production code we still suggest listing the variable names you capture explicitly whenever possible, to avoid unintentionally capturing things you shouldn't.

```cpp
int a = 1, b = 2, c = 3;

// Capture everything by value
auto sum_all = [=]() { return a + b + c; };  // 6

// Capture everything by reference — can modify the outer variables
auto increment_all = [&]() { a++; b++; c++; };
increment_all();  // a=2, b=3, c=4

// Mixed capture: a by value, b by reference
auto mixed = [a, &b]() { return a + b; };
```

---

## The Type of a Lambda: The Closure Type

We mentioned earlier that every lambda expression produces a unique, anonymous class type (the closure type). `operator()` is a member function of it, with exactly the parameters and return value you wrote in the lambda. The standard only specifies the behavior; the concrete implementation is up to the compiler. Conceptually, you can understand a lambda as the compiler generating a class like this:

```cpp
// The lambda you write
auto greet = [](const std::string& name) -> std::string {
    return "Hello, " + name;
};

// The class the compiler conceptually generates (simplified)
struct /* a unique compiler-generated name */ {
    std::string operator()(const std::string& name) const {
        return "Hello, " + name;
    }
};
auto greet = /* an instance of the class above */{};
```

In a real implementation, the compiler adds the corresponding data members to the closure class based on the capture list, and the `mutable` keyword decides whether `operator()` is const. How these type names get picked is not up to us — each compiler makes its own call: GCC uses `_Z...` mangling, Clang uses names like `$_0...`, and once you cross compilers, consistency is off the table.

So you cannot write down a lambda's type name directly: the name is generated inside the compiler, and it differs across compilers and even across translation units. When we store a lambda, only two routes are open to us. With `auto`, the type is settled at compile time. With `std::function`, we rely on type erasure. Type erasure means a family of techniques that hide all sorts of closure types behind a uniform interface, making the concrete type invisible from the outside. It comes with runtime overhead — the details we'll shove off to the fourth article.

We turned this correspondence into an animation: you can play it, pause it, or step through it one frame at a time. The lambda in the example omits the trailing return type, so the parts that follow are just three — the capture list, the parameter list, and the body. Watch where each of them lands in the closure class:

<Anim id="lambda-anatomy" />

Passing a lambda as a template parameter is a common move within zero-overhead abstraction. The compiler sees the lambda's complete type, which gives it a chance to inline:

```cpp
template<typename Func>
void call_func(Func f) {
    f();
}

call_func([]() { /* ... */ });  // The type is visible to the compiler; inlining is possible
```

Let's be precise about this — what we said above is only "possible": whether inlining actually happens depends on the compiler's optimization strategy, the lambda's complexity, the compilation flags, and factors like these. But compared with the runtime indirect call through `std::function`, a template parameter at least gives the compiler the opportunity to optimize.

---

## Hands-On: An Event Handling System

Of the two routes for storing lambdas from the previous section, event handling puts exactly one of them to work: callbacks written by different modules each have their own closure type, so we use `std::function` to unify them behind the same interface and drop them all into a dispatcher together. Let's build a simple event handling system right now. Registering callbacks and triggering them are common needs in real projects; the callbacks may come from different modules, each with its own context:

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

    // Register the key-press callback: capture press_count and last_press_time by reference
    dispatcher.on_event(0, [&](uint32_t timestamp) {
        if (timestamp - last_press_time > 50) {   // 50ms debounce
            press_count++;
            last_press_time = timestamp;
            std::cout << "Press #" << press_count
                      << " at " << timestamp << "ms\n";
        }
    });

    // Register the timeout callback: capture threshold by value
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

We've put the demo program below — click "Try It Out" and it runs directly:

<OnlineCompilerDemo
  title="Hands-On Verification: a lambda event handling system"
  source-path="code/examples/vol2/33_event_dispatcher.cpp"
  description="Run the event handling system online. Note that the third key press (at 180ms) prints nothing — it comes only 20ms after the previous one, so the debounce logic filters it out."
  run-options="-std=c++17"
  allow-run
/>

As you can see, lambdas work very naturally as callbacks. Look at the key-press part: `[&]` brings in both `press_count` and `last_press_time`, the body modifies exactly those, and the debounce check and the counting are written in the same place. The third trigger prints nothing at all — that's the 50ms check doing its job.

---

## Generic Lambdas in C++14

C++14 brought lambdas a very practical enhancement: parameter types can be `auto`. The lambda thereby becomes a template function object in our hands — the compiler generates a separate instantiation of `operator()` for each argument type:

```cpp
// A generic lambda: accepts any type that supports operator+
auto add = [](auto a, auto b) { return a + b; };

int xi = add(3, 4);              // int operator+(int, int)
double xd = add(3.5, 2.5);       // double operator+(double, double)
std::string xs = add(std::string("hello"), std::string(" world"));
```

Here is roughly what the closure type the compiler generates behind the scenes looks like:

```cpp
struct GenericClosure {
    template<typename T1, typename T2>
    auto operator()(T1 a, T2 b) const {
        return a + b;
    }
};
```

Where generic lambdas shine is generic algorithms and utility functions — you no longer need to wrap the lambda in an outer template function. The rest of the topic we'll leave to the later article "Generic Lambdas and Template Lambdas".

---

## A Few Common Pitfalls

### Don't Let the Lambda Body Grow Too Long

A lambda's strength is in-place definition and compact logic. Once the lambda we're writing passes 5-7 lines, it's time to consider extracting it into a named function or a functor. Let it grow long, and whoever reads it has to scroll through several screens inside an algorithm's argument list, flipping back and forth between the lambda and its call site — and that is how readability gets hurt.

### The Lifetime Trap of Capture by Reference

It has a seat among the most common lambda bugs: a variable captured by reference is already destroyed by the time the lambda executes. The classic scenario is creating a lambda inside a function and then returning it:

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

Capture by reference is not wrong in itself, but you must guarantee that the referenced object outlives the lambda. In event systems, asynchronous callbacks, and scenarios like these, this constraint is remarkably easy to overlook.

### Storing a Lambda: `auto` or `std::function`

Unless you need runtime polymorphism (putting callbacks of different types into the same container, say), don't reach for `std::function` to store a lambda. `auto` gets you the closure type itself: the object's size equals the size of the captured data members (a captureless lambda is usually just 1 byte), and it leaves the compiler a chance to inline. `std::function` internally relies on two mechanisms, type erasure and the Small Buffer Optimization, which bring a fixed overhead (32-64 bytes), plus one more indirect jump to pay on every call.

```cpp
// Type known at compile time, size = 1 byte (no captures), inlining possible
auto f = [](int x) { return x * 2; };

// Type-erased, size = 32 bytes (libstdc++), runtime indirect call
std::function<int(int)> g = [](int x) { return x * 2; };
```

Put that difference on a performance-critical path and it can matter a lot — though let's not rush into premature optimization either: when the code isn't on a hot path, the convenience of `std::function` may well matter more.

---

## References

- [Lambda expressions (C++11) - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++14 generic lambdas - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Generic_lambdas)
