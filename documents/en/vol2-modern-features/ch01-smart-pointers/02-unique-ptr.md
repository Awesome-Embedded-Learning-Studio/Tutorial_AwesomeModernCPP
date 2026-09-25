---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: A deep dive into unique_ptr's implementation principles, usage, and best
  practices
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Deep Dive into RAII: The Cornerstone of Resource Management'
reading_time_minutes: 17
related:
- 'Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
- 'Custom Deleters and Intrusive Reference Counting'
tags:
- host
- cpp-modern
- intermediate
- unique_ptr
- 智能指针
title: 'Deep Dive into unique_ptr: A Zero-Overhead Smart Pointer with Exclusive Ownership'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/02-unique-ptr.md
  source_hash: f125fba833ef1d415ea74acf0055fd3c279aa941392688ebea07d794bf98e65d
  translated_at: '2026-09-25T14:31:09+00:00'
  engine: anthropic
  token_count: 3300
---
# Deep Dive into unique_ptr: A Zero-Overhead Smart Pointer with Exclusive Ownership

In the previous article we talked about RAII—the cornerstone of C++ resource management. Now let's look at the most direct embodiment of the RAII idea in the smart pointer world: `std::unique_ptr`. The design philosophy of this class boils down to one sentence: **one object, one owner, zero overhead**. It doesn't do reference counting, doesn't perform atomic operations, doesn't allocate any extra control block—you hand it an object, and it manages the object for you; you leave the scope, and it deletes the object for you. That simple. (By the way, why do interviews love quizzing on this thing so much?)

But simple doesn't mean shallow. The topics sitting behind `unique_ptr`—ownership semantics, move semantics, custom deleters, Empty Base Optimization (EBO)—each deserves a deep understanding. Today we'll take all of them apart.

## Exclusive Ownership: Why Copying Is Not Allowed

The most fundamental semantic of `unique_ptr` is "exclusive"—at any given moment, exactly one `unique_ptr` owns the object. This means copy construction and copy assignment are not permitted; only moves are. This isn't some restriction imposed from outside—it's design expressing itself precisely: if copying were allowed, two `unique_ptr`s would each believe they own the object, and both would attempt to delete it upon leaving scope—a double free, leading straight to undefined behavior.

```cpp
#include <memory>
#include <iostream>

struct Widget {
    int value;
    explicit Widget(int v) : value(v) {
        std::cout << "Widget(" << value << ") 构造\n";
    }
    ~Widget() {
        std::cout << "~Widget(" << value << ") 析构\n";
    }
};

void ownership_demo() {
    auto p1 = std::make_unique<Widget>(42);
    // auto p2 = p1;              // Compile error! unique_ptr is not copyable
    auto p2 = std::move(p1);      // OK: ownership transfers from p1 to p2

    // Now p1 == nullptr, and p2 owns the object
    std::cout << "p1: " << p1.get() << "\n";  // Output: 0 or nullptr
    std::cout << "p2: " << p2.get() << "\n";  // Output: a valid address
    std::cout << "p2->value: " << p2->value << "\n";  // Output: 42
}   // p2 is destroyed, and the Widget is deleted automatically
```

Output:

```text
Widget(42) 构造
p1: 0
p2: 0x55a3c8f42eb0
p2->value: 42
~Widget(42) 析构
```

We've turned the three segments—exclusive ownership, copy rejected, ownership handed over via move—into an animation. You can play it, pause it, or single-step through it with the step button to see every move clearly:

<Anim id="unique-ownership" />

This "non-copyable, movable" design maps perfectly onto ownership transfer in real life—hand your key to someone, and you no longer have that key. At the code level, `std::move` hands the raw pointer inside `p1` over to `p2`, then sets `p1` to null. No additional memory allocation happens along the way, and no reference-counting overhead either.

## make_unique vs new: Why C++14 Added This Function

C++11 introduced `std::unique_ptr` but forgot to provide `std::make_unique` (widely regarded as an oversight); C++14 filled the gap. So what advantages does `make_unique` have over a direct `new`?

First, **exception safety**. Consider this function call:

```cpp
// Suppose we have a function with this signature
void process(std::unique_ptr<Widget> ptr, int computed_value);

// The dangerous version (C++11 style)
process(std::unique_ptr<Widget>(new Widget(42)), compute_something());

// The safe version (C++14 style)
process(std::make_unique<Widget>(42), compute_something());
```

In the dangerous version, before calling `process` the C++ compiler has to get three things done in sequence: `new Widget(42)`, construct the `unique_ptr`, and call `compute_something()`. **Before C++17**, the C++ standard did not specify the evaluation order of function arguments—the compiler could `new` first, then call `compute_something()`, and construct the `unique_ptr` last. If `compute_something()` throws an exception, the `Widget` from that `new` leaks—because the `unique_ptr` hasn't taken it over yet.

**Important update**: starting with **C++17**, the standard mandates that function arguments be evaluated left to right. So in C++17 and later, the dangerous version is actually safe as well. That said, `make_unique` still has other advantages (more concise code, no repeated type names) and works with older standards, so it remains the recommended practice.

`make_unique` wraps allocation and construction inside a single function call, so this kind of "intermediate state" never exists—which is what makes it exception-safe.

Second, **code conciseness**. `make_unique` keeps bare `new` out of your code, reducing the chance of mistakes:

```cpp
// Comparison
auto p1 = std::unique_ptr<Widget>(new Widget(42));  // Verbose, and easy to forget the unique_ptr wrapper
auto p2 = std::make_unique<Widget>(42);              // Concise, impossible to forget the management
```

`make_unique` has one limitation: it doesn't support custom deleters. If you need a custom deleter (say, to manage a `FILE*` or `malloc`-allocated memory), you must construct the `unique_ptr` directly. We'll discuss this issue in detail in the later "Custom Deleters" article.

## The Deep Connection Between Move Semantics and unique_ptr

`unique_ptr` and move semantics are tightly bound together. Before C++11, C++ had copy semantics only—making a "replica" of an object. But for `unique_ptr`, copying would mean "two pointers pointing at the same object", which violates exclusive ownership. The introduction of move semantics solved exactly this problem: moving is not "replicating" but "transferring"—the source object gives up ownership, and the destination object takes over.

This is what allows `unique_ptr` to live inside standard containers:

```cpp
#include <memory>
#include <vector>
#include <iostream>

struct Sensor {
    int id;
    explicit Sensor(int i) : id(i) {}
};

int main() {
    std::vector<std::unique_ptr<Sensor>> sensors;

    // push_back needs to move, because unique_ptr is not copyable
    sensors.push_back(std::make_unique<Sensor>(1));
    sensors.push_back(std::make_unique<Sensor>(2));
    sensors.push_back(std::make_unique<Sensor>(3));

    // When the vector grows, the unique_ptrs inside are transferred via move construction
    // This is also why unique_ptr's move operations are marked noexcept
    for (const auto& s : sensors) {
        std::cout << "Sensor id: " << s->id << "\n";
    }

    // Returning a unique_ptr from a function also goes through a move (or RVO)
    auto make_sensor = [](int id) -> std::unique_ptr<Sensor> {
        return std::make_unique<Sensor>(id);
    };

    auto s = make_sensor(99);
    std::cout << "Created sensor " << s->id << "\n";
}
```

There's an important detail here: both the move constructor and the move assignment operator of `unique_ptr` are marked `noexcept`. This has a direct impact on how `std::vector` behaves—when a vector grows, it prefers moves if the element's move constructor is `noexcept`; otherwise it falls back to copying (but `unique_ptr` cannot be copied, so it must move). Noexcept move operations are therefore the key guarantee that lets `unique_ptr` sit safely inside containers.

## unique_ptr<T[]>: The Array Version

`unique_ptr` has a partial specialization for arrays, `unique_ptr<T[]>`, whose destructor calls `delete[]` instead of `delete`.

```cpp
auto arr = std::make_unique<int[]>(64);  // Allocate 64 ints
arr[0] = 42;
arr[1] = 17;
// Automatically calls delete[] on destruction
```

Honestly, though, scenarios in C++ where you need to hand-manage dynamic arrays have become very rare. If you need a fixed-size array, `std::array` or `std::vector` is almost always the better choice. `unique_ptr<T[]>` is mainly for interfacing with C APIs that return dynamically allocated arrays, for example:

```cpp
// Suppose some C API returns a malloc-allocated array
extern "C" int* create_buffer(size_t size);
extern "C" void free_buffer(int* buf);

auto buffer = std::unique_ptr<int[], void(*)(int*)>(
    create_buffer(1024),
    [](int* p) { free_buffer(p); }
);
buffer[0] = 42;
```

Our strong recommendation: don't use `unique_ptr<T[]>` as a replacement for `std::vector`. `vector` gives you `size()`, iterators, bounds checking (via `at()`), and more, while `unique_ptr<T[]>` gives you nothing but automatic release.

## Custom Deleter Basics

The second template parameter of `unique_ptr` is the deleter's type. The default is `std::default_delete<T>`, which simply does a `delete ptr` inside. But you can replace it with any callable—function pointer, lambda, function object—as long as it has the `void operator()(T*)` signature.

The most common scenario is managing resources returned from C APIs:

```cpp
#include <cstdio>
#include <memory>

// Function pointer as the deleter
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;

FilePtr open_file(const char* path, const char* mode) {
    FILE* f = std::fopen(path, mode);
    return FilePtr(f, &std::fclose);
}

// Lambda as the deleter (captureless → stateless → zero overhead)
auto make_closer = []() {
    auto deleter = [](FILE* f) noexcept { if (f) std::fclose(f); };
    return std::unique_ptr<FILE, decltype(deleter)>(std::fopen("/tmp/log", "w"), deleter);
};
```

Function objects (functors) as deleters are also a common choice, especially when you want the deleter type to have a name:

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// Managing malloc-allocated memory
auto buf = std::unique_ptr<char, FreeDeleter>(
    static_cast<char*>(std::malloc(256))
);
```

For a deeper discussion of custom deleters (stateful deleters, EBO, deleters in `shared_ptr`, and so on), we'll dedicate the "Custom Deleters and Intrusive Reference Counting" article to the topic.

## Proving Zero Overhead: sizeof and Assembly Analysis

`unique_ptr` is often advertised as a "zero-overhead abstraction", but that's no marketing slogan—we can verify it with real code. First, a `sizeof` comparison:

```cpp
#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

// Stateful deleter: carries a data member, so EBO can't apply
struct StatefulDeleter {
    int extra;
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                             " << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):                  " << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>):    " << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>):   " << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
    std::cout << "sizeof(unique_ptr<int, StatefulDeleter>): " << sizeof(std::unique_ptr<int, StatefulDeleter>) << "\n";
}
```

Output on a 64-bit platform (GCC 16.1.1, x86_64):

```text
sizeof(int*):                             8
sizeof(unique_ptr<int>):                  8
sizeof(unique_ptr<int, EmptyDeleter>):    8
sizeof(unique_ptr<int, void(*)(int*)>):   16
sizeof(unique_ptr<int, StatefulDeleter>): 16
```

With the default deleter or a stateless function object, a `unique_ptr` is exactly as large as a raw pointer—8 bytes. That's Empty Base Optimization (EBO) at work: internally, `unique_ptr` typically inherits from the deleter type, and when the deleter is an empty class (no data members), the compiler optimizes its size down to 0, leaving the `unique_ptr` with just the one raw pointer to store. The moment the deleter carries state—a function pointer needs its address stored, `StatefulDeleter` needs its `extra` stored—EBO can't help, and the size climbs to 16 bytes.

And when a function pointer serves as the deleter, the `unique_ptr` has to store that extra function pointer, so the size doubles—16 bytes. That's the precondition for "zero overhead": **the deleter must be stateless**.

Let's verify it from the assembly angle next. Here's a simple example:

```cpp
// Manage an int with unique_ptr
int use_unique_ptr() {
    auto p = std::make_unique<int>(42);
    return *p;
}

// The equivalent raw-pointer version
int use_raw_ptr() {
    int* p = new int(42);
    int v = *p;
    delete p;
    return v;
}
```

With optimizations enabled (`-O2`), the assembly generated for these two functions is nearly identical. Save the two functions above into a file, compile with `g++ -std=c++17 -O2 -S`, and you'll see that both produce:

```asm
movl    $42, %eax
ret
```

The compiler inlines the `unique_ptr` construction and destruction away, and even the `new` and `delete` are eliminated (the object's lifetime is short and free of side effects). This is the power of C++ abstraction: you gain safety and readability at the source level, yet pay nothing at the machine-code level.

## The PIMPL Idiom: Hiding Implementation Details

PIMPL (Pointer to Implementation) is a classic technique in C++ for reducing compile-time dependencies. `unique_ptr`'s support for incomplete types makes it the best tool for implementing PIMPL.

Header `widget.h`:

```cpp
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();  // Must be declared here and defined in the implementation file

    Widget(Widget&&) noexcept;
    Widget& operator=(Widget&&) noexcept;

    // Copying disabled (or implement a deep copy yourself)
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    void do_something();

private:
    struct Impl;                  // Forward declaration, an incomplete type
    std::unique_ptr<Impl> impl_;  // unique_ptr supports incomplete types
};
```

Implementation file `widget.cpp`:

```cpp
#include "widget.h"
#include <iostream>
#include <string>

// The real implementation is defined here—includers of the header see none of these details
struct Widget::Impl {
    std::string name;
    int count;

    Impl() : name("default"), count(0) {}

    void do_work() {
        ++count;
        std::cout << name << " working (count=" << count << ")\n";
    }
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}

Widget::~Widget() = default;  // Impl is a complete type here, so delete runs correctly

Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;

void Widget::do_something() {
    impl_->do_work();
}
```

The benefit of PIMPL is plain to see: modify `Impl`'s definition (add a member, change a method) and only `widget.cpp` needs recompiling—every file that includes `widget.h` stays untouched. For large projects, this can significantly shorten build times.

Compile `widget.h`, `widget.cpp`, and the user code that uses `Widget` separately and then link them, and you can watch PIMPL in action: modify the `Impl` struct and only `widget.cpp` gets rebuilt; all files that merely include `widget.h` skip recompilation.

A few gotchas come with using `unique_ptr` for PIMPL. First, `~Widget()` must be defined in the implementation file—destruction requires `Impl` to be a complete type, while the header only holds a forward declaration. Second, the move constructor and move assignment should also be `= default`-ed in the implementation file, for the same reason. If you `= default` them in the header, the compiler will try to instantiate `unique_ptr<Impl>`'s destructor right there, where `Impl` is still incomplete, and you get a compile error.

## Factory Functions Returning unique_ptr

Factory functions returning `unique_ptr` is a very common pattern. It is not only safe (the caller cannot possibly forget to release) but also expresses crisp ownership semantics: the factory creates the object, the caller owns it exclusively.

```cpp
#include <memory>
#include <string>

class Logger {
public:
    virtual ~Logger() = default;
    virtual void log(const std::string& msg) = 0;
};

class ConsoleLogger : public Logger {
public:
    void log(const std::string& msg) override {
        std::cout << "[LOG] " << msg << "\n";
    }
};

class FileLogger : public Logger {
public:
    explicit FileLogger(const std::string& path) : path_(path) {}
    void log(const std::string& msg) override {
        // Write to the file (implementation omitted)
    }
private:
    std::string path_;
};

// Factory function: returns unique_ptr<Logger>
std::unique_ptr<Logger> create_logger(bool use_file, const std::string& path = "") {
    if (use_file) {
        return std::make_unique<FileLogger>(path);
    }
    return std::make_unique<ConsoleLogger>();
}

// Usage
void application() {
    auto logger = create_logger(true, "/tmp/app.log");
    logger->log("Application started");

    // Ownership can also be handed to other components via move
    // set_global_logger(std::move(logger));
}
```

There's another neat touch to this pattern: the factory function returns a `unique_ptr<Logger>` (a base-class pointer), while what's actually created is a `ConsoleLogger` or `FileLogger` (a derived-class object). As long as `Logger` has a virtual destructor (and we did declare `virtual ~Logger() = default`), polymorphic destruction is safe.

Worth noting: returning a `unique_ptr` brings no performance penalty. On modern compilers, return value optimization (RVO) and move semantics keep the whole trip copy-free—the `unique_ptr` created inside the factory function is simply "carried over" into the caller's variable.

Concretely:

- C++11/14: relies mainly on move semantics (the move constructor)
- C++17: guaranteed copy elision optimizes this case even further

In either case, no extra memory allocation or reference counting happens, and performance is on par with returning a raw pointer directly.

## release(), reset(), and get(): Three Key Operations

`unique_ptr` provides a few methods for manually managing ownership, and understanding the differences between them really matters.

`get()` returns the internal raw pointer without transferring ownership. This is useful when you need to pass the pointer to a function that merely uses it but doesn't own it:

```cpp
void print_widget(const Widget* w);

auto p = std::make_unique<Widget>(42);
print_widget(p.get());  // Passed to a read-only function; p still owns the object
```

`release()` gives up ownership and returns the raw pointer—the `unique_ptr` becomes empty, but the object is not deleted. It amounts to saying "I've handed the object to you; releasing it is your responsibility":

```cpp
auto p = std::make_unique<Widget>(42);
Widget* raw = p.release();  // p becomes nullptr, raw points to the object
// ... use raw ...
delete raw;  // You must release it manually
```

`release()` is an operation that calls for caution. The moment you call it, you're back in the raw-pointer world—forget the `delete`, and you've leaked memory. In most cases, transferring ownership to another `unique_ptr` with `std::move()` is the better choice.

`reset()` replaces the currently managed object. Called without arguments, it simply releases the current object and empties the pointer:

```cpp
auto p = std::make_unique<Widget>(1);
p.reset(new Widget(2));  // Frees Widget(1), takes over Widget(2)
p.reset();               // Frees Widget(2), p becomes nullptr
```

## Embedded Practice: Hardware Handle Management

In embedded development, `unique_ptr` paired with a custom deleter manages hardware resources elegantly. For example, managing a DMA buffer allocated through the HAL:

```cpp
struct DmaBuffer {
    void* data;
    size_t size;
};

struct DmaDeleter {
    void operator()(DmaBuffer* buf) noexcept {
        if (buf) {
            hal_dma_free(buf->data);  // Free the DMA buffer
            delete buf;
        }
    }
};

using UniqueDmaBuffer = std::unique_ptr<DmaBuffer, DmaDeleter>;

UniqueDmaBuffer allocate_dma_buffer(size_t size) {
    void* data = hal_dma_alloc(size);
    if (!data) return nullptr;
    return UniqueDmaBuffer(new DmaBuffer{data, size});
}
```

The beauty of this style: every return path—normal return, error return, or exception—releases the DMA buffer correctly. In complex driver code, this kind of automatic management can noticeably cut the bug rate.

The next article moves on to `shared_ptr`—a completely different ownership model: shared ownership. That's where the real complexity begins.

## References

- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
- [Empty Base Optimization and unique_ptr](https://www.cppstories.com/2021/no-unique-address/)
- Herb Sutter, *GotW #89: Smart Pointers*
