---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Deep dive into `unique_ptr` implementation principles, usage, and best
  practices
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 1: RAII 深入理解'
reading_time_minutes: 17
related:
- shared_ptr 详解
- 自定义删除器
tags:
- host
- cpp-modern
- intermediate
- unique_ptr
- 智能指针
title: 'unique_ptr Deep Dive: A Zero-Overhead Smart Pointer with Exclusive Ownership'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/02-unique-ptr.md
  source_hash: 87969e610bdf36639634ebf96ce8e6a76df739cb4cdeb207e4614bfa72f1af6e
  translated_at: '2026-06-16T03:55:20.972417+00:00'
  engine: anthropic
  token_count: 3500
---
# Deep Dive into unique_ptr: Zero-Overhead Smart Pointer with Exclusive Ownership

In the previous post, we discussed RAII—the cornerstone of C++ resource management. Now, let's look at the most direct manifestation of the RAII philosophy in the realm of smart pointers: `std::unique_ptr`. The design philosophy of this class can be summarized in a single sentence: **one object, one owner, zero overhead**. It doesn't bother with reference counting, atomic operations, or allocating extra control blocks—you give it an object, it manages it for you; you leave the scope, it deletes it for you. It's just that simple. (By the way, why do interviewers love this topic so much?)

But simple doesn't mean shallow. Behind `std::unique_ptr` lie topics like ownership semantics, move semantics, custom deleters, and Empty Base Optimization (EBO)—each worth a deep understanding. Today, we'll unpack all of these.

## Exclusive Ownership: Why No Copying

The core semantic of `std::unique_ptr` is "exclusive"—at any given moment, only one `std::unique_ptr` owns the object. This means copy construction and copy assignment are prohibited; only move operations are allowed. This isn't a limitation, but a precise expression of design: if copying were allowed, two `std::unique_ptr` instances would both believe they own the object. Upon leaving the scope, both would attempt to delete—double free, leading directly to undefined behavior (UB).

```cpp
#include <memory>
#include <iostream>

struct Widget {
    Widget() { std::cout << "Widget constructed\n"; }
    ~Widget() { std::cout << "Widget destroyed\n"; }
};

int main() {
    // Create a unique_ptr
    std::unique_ptr<Widget> ptr1 = std::make_unique<Widget>();

    // Transfer ownership via move
    std::unique_ptr<Widget> ptr2 = std::move(ptr1);

    // ptr1 is now null; ptr2 owns the object
    if (!ptr1) {
        std::cout << "ptr1 is empty\n";
    }

    // Error: cannot copy unique_ptr
    // std::unique_ptr<Widget> ptr3 = ptr2;

    return 0;
}
```

Output:

```text
Widget constructed
ptr1 is empty
Widget destroyed
```

This "non-copyable, movable" design perfectly maps to real-world ownership transfer—like handing a key to someone else; you no longer possess that key. At the code level, `std::move(ptr1)` transfers the raw pointer inside `ptr1` to `ptr2`, and then sets `ptr1` to null. The entire process involves no extra memory allocation and no reference counting overhead.

## make_unique vs new: Why C++14 Added This Function

C++11 introduced `std::unique_ptr` but forgot to provide `std::make_unique` (widely considered an oversight), which was added in C++14. So, what advantages does `std::make_unique` have over using `new` directly?

First is **exception safety**. Consider the following function call:

```cpp
void process(std::unique_ptr<Widget> ptr, int value);

// Dangerous approach (pre-C++17)
process(std::unique_ptr<Widget>(new Widget()), compute_value());
```

In the dangerous approach, the C++ compiler needs to complete the following steps sequentially before calling `process`: `new Widget`, construct the `std::unique_ptr`, and call `compute_value`. **Before C++17**, the C++ standard did not mandate the evaluation order of function arguments—the compiler might `new Widget`, then call `compute_value`, and finally construct the `std::unique_ptr`. If `compute_value` throws an exception, the `new`ed `Widget` leaks—because the `std::unique_ptr` hasn't taken over yet.

⚠️ **Important Update**: Starting from **C++17**, the standard mandates that function arguments must be evaluated left-to-right. Therefore, in C++17 and later, the "dangerous" approach is actually safe. However, `std::make_unique` still has other advantages (code conciseness, avoiding repeating type names) and compatibility with older standards, so it remains the recommended practice.

`std::make_unique` wraps allocation and construction in a single function call, eliminating this "intermediate state," making it exception-safe.

Second is **code conciseness**. `std::make_unique` avoids the appearance of naked `new` in code, reducing the chance of errors:

```cpp
// Concise and safe
auto ptr = std::make_unique<Widget>();
```

⚠️ `std::make_unique` has a limitation: it does not support custom deleters. If you need a custom deleter (e.g., managing memory allocated by `malloc` or C APIs), you must construct `std::unique_ptr` directly. We will discuss this issue in detail in the "Custom Deleters" section later.

## The Deep Relationship Between Move Semantics and unique_ptr

`std::unique_ptr` and move semantics are intimately linked. Before C++11, C++ only had copy semantics—making a "copy" of an object. But for `std::unique_ptr`, copying means "two pointers point to the same object," which violates exclusive ownership semantics. The introduction of move semantics solves this problem perfectly: moving isn't "copying," but "transferring"—the source object relinquishes ownership, and the target object takes over.

This allows `std::unique_ptr` to be stored in standard containers:

```cpp
#include <memory>
#include <vector>

int main() {
    std::vector<std::unique_ptr<Widget>> widgets;
    widgets.reserve(3);

    widgets.push_back(std::make_unique<Widget>());
    widgets.push_back(std::make_unique<Widget>());
    widgets.push_back(std::make_unique<Widget>());

    // Vector expansion automatically moves unique_ptrs
    widgets.emplace_back(std::make_unique<Widget>());

    return 0;
}
```

Here is an important detail: `std::unique_ptr`'s move constructor and move assignment operator are marked `noexcept`. This has a direct impact on `std::vector` behavior—when a vector expands, if the element's move constructor is `noexcept`, the vector will prefer moving; otherwise, it falls back to copying (but `std::unique_ptr` isn't copyable, so it must move). Therefore, the `noexcept` nature of `std::unique_ptr`'s move operations is the key guarantee for safely storing it in containers.

You can run `unique_ptr_vector.cpp` to verify this. This example shows how the vector safely moves objects managed by `std::unique_ptr` during expansion and verifies that all elements remain valid after resizing.

## unique_ptr<T[]>: Array Version

`std::unique_ptr` has a partial specialization for arrays, `std::unique_ptr<T[]>`, which calls `delete[]` instead of `delete` upon destruction.

```cpp
std::unique_ptr<int[]> arr = std::make_unique<int[]>(10);
arr[0] = 100;
```

However, honestly, scenarios requiring manual management of dynamic arrays in C++ are very rare. If you need a fixed-size array, using `std::array` or `std::vector` is almost always a better choice. `std::unique_ptr<T[]>` is primarily used to interface with C APIs that return dynamically allocated arrays, like:

```cpp
// Assuming a C API: int* get_buffer(size_t size);
void buffer_deleter(int* p) {
    // C API cleanup function
    c_api_free(p);
}

std::unique_ptr<int[], void(*)(int*)> buf(get_buffer(1024), buffer_deleter);
```

⚠️ I strongly suggest: do not use `std::unique_ptr<T[]>` to replace `std::vector`. `std::vector` provides `size()`, iterators, bounds checking (via `at()`), etc., whereas `std::unique_ptr<T[]>` offers nothing beyond automatic release.

## Custom Deleters Basics

The second template parameter of `std::unique_ptr` is the deleter type. By default, it's `std::default_delete`, which internally simply performs `delete`. However, you can replace it with any callable object—function pointer, lambda, functor—provided it matches the `void(T*)` signature.

The most common scenario is managing resources returned by C APIs:

```cpp
// Managing FILE* from C standard library
auto file_closer = [](FILE* f) { fclose(f); };
std::unique_ptr<FILE, decltype(file_closer)> log_file(fopen("log.txt", "w"), file_closer);

// Using fprintf...
fprintf(log_file.get(), "Hello, %s!\n", "World");
```

Function objects (functors) as deleters are also a common choice, especially when you want the deleter type to have a name:

```cpp
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleDeleter>;
UniqueHandle h(CreateFile(...));
```

For a deeper discussion on custom deleters (stateful deleters, EBO optimization, deleters in `std::shared_ptr`), we will expand on this in the dedicated article "Custom Deleters and Intrusive Reference Counting."

## Proof of Zero Overhead: sizeof and Assembly Analysis

`std::unique_ptr` is often touted as a "zero-overhead abstraction," but this isn't marketing fluff—we can verify it with actual code. First, let's compare `sizeof`:

```cpp
#include <memory>
#include <iostream>

int main() {
    std::unique_ptr<int> up1;
    int* raw = nullptr;
    std::unique_ptr<int, void(*)(int*)> up2(nullptr, [](int*) {});

    std::cout << "sizeof(raw): " << sizeof(raw) << "\n";
    std::cout << "sizeof(unique_ptr): " << sizeof(up1) << "\n";
    std::cout << "sizeof(unique_ptr with func ptr): " << sizeof(up2) << "\n";

    return 0;
}
```

Typical output on a 64-bit platform:

```text
sizeof(raw): 8
sizeof(unique_ptr): 8
sizeof(unique_ptr with func ptr): 16
```

`std::unique_ptr` with a default deleter or stateless function object is exactly the same size as a raw pointer—8 bytes. This is the magic of Empty Base Optimization (EBO): `std::unique_ptr` usually inherits from the deleter type. When the deleter is an empty class (no data members), the compiler optimizes its size to zero, so `std::unique_ptr` only needs to store that one raw pointer.

You can run `unique_ptr_sizeof.cpp` to verify this. Typical output on x86_64-linux (g++ 15.2.1):

```text
sizeof(raw ptr): 8
sizeof(unique_ptr): 8
sizeof(unique_ptr<func_ptr>): 16
sizeof(unique_ptr<stateless_lambda>): 8
```

As you can see, when using a stateless deleter, `std::unique_ptr` is exactly the same size as a raw pointer, whereas using a function pointer or stateful deleter adds overhead.

When using a function pointer as the deleter, `std::unique_ptr` needs to store an extra function pointer, so the size doubles—16 bytes. This is the prerequisite for "zero overhead": **the deleter must be stateless**.

Let's verify this from an assembly perspective. Here is a simple example:

```cpp
void raw_ptr_version() {
    int* p = new int(42);
    // ... use p ...
    delete p;
}

void unique_ptr_version() {
    auto p = std::make_unique<int>(42);
    // ... use p ...
}
```

With optimizations enabled (`-O2`), the assembly code generated for these two functions is almost identical. Check `unique_ptr_asm.s` compiled with `-O2 -S`, and you will see both functions generate:

```asm
; x86-64 example
mov     edi, 4
call    operator new(unsigned long)
; ... check for null ...
mov     dword ptr [rax], 42
; ... use the value ...
mov     rdi, rax
call    operator delete(void*)
```

The compiler inlines the construction and destruction of `std::unique_ptr`, and even eliminates `new` and `delete` (because the object's lifetime is short and has no side effects). This is the power of C++ abstraction: you gain safety and readability at the source level, but pay no price at the machine code level.

## PIMPL Idiom: Hiding Implementation Details

PIMPL (Pointer to Implementation) is a classic technique in C++ for reducing compilation dependencies. `std::unique_ptr`'s support for incomplete types makes it the best tool for implementing PIMPL.

Header file `widget.h`:

```cpp
#ifndef WIDGET_H
#define WIDGET_H

#include <memory>

class Widget {
public:
    Widget();
    ~Widget(); // Must be declared in header
    void work();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

#endif
```

Implementation file `widget.cpp`:

```cpp
#include "widget.h"
#include <iostream>

struct Widget::Impl {
    void do_work() {
        std::cout << "Working hard in Impl...\n";
    }
};

Widget::Widget() : pImpl(std::make_unique<Impl>()) {}

Widget::~Widget() = default; // Defined here, Impl is complete

void Widget::work() {
    pImpl->do_work();
}
```

The benefits of PIMPL are obvious: modifying the definition of `Impl` (like adding members or changing methods) only requires recompiling `widget.cpp`. All files including `widget.h` don't need to be recompiled. For large projects, this significantly reduces compilation time.

The complete PIMPL example code can be found in `pimpl_example/`:

- `pimpl_widget.h` - Public interface header
- `pimpl_widget.cpp` - Implementation (contains full definition of `Impl`)
- `pimpl_main.cpp` - User code example

You can compile and run it like this:

```bash
cd pimpl_example
cmake -B build
cmake --build build
./build/pimpl_example
```

This example demonstrates the key feature of the PIMPL pattern: the public interface exposes absolutely no implementation details, and modifying the `Impl` struct does not require recompiling user code.

⚠️ There are a few caveats when using `std::unique_ptr` with PIMPL. First, the destructor must be defined in the implementation file—because destruction requires `Impl` to be a complete type, while the header file only has a forward declaration. Second, the move constructor and move assignment should also be defaulted in the implementation file for the same reason. If you `= default` them in the header, the compiler will attempt to instantiate `std::unique_ptr`'s destructor in the header, where `Impl` is incomplete, causing a compilation error.

## Factory Functions Returning unique_ptr

Factory functions returning `std::unique_ptr` is a very common pattern. It is not only safe (callers can't forget to release), but also expresses clear ownership semantics: the factory creates the object, and the caller owns it exclusively.

```cpp
class Base {
public:
    virtual void interface() = 0;
    virtual ~Base() = default;
};

class Derived : public Base {
public:
    void interface() override { /* ... */ }
};

std::unique_ptr<Base> create_object() {
    return std::make_unique<Derived>();
}
```

This pattern has a clever feature: the factory function returns `std::unique_ptr<Base>` (base class pointer), but actually creates `Derived` or other (derived class objects). As long as `Base` has a virtual destructor (which we indeed declared), polymorphic destruction is safe.

It is worth noting that returning `std::unique_ptr` incurs no performance penalty. In modern compilers, Return Value Optimization (RVO) and move semantics ensure the whole process is zero-copy—the `std::unique_ptr` created in the factory function is directly "moved" into the caller's variable.

Specifically:

- C++11/14: Relies mainly on move semantics (move constructor).
- C++17: Guaranteed copy elision further optimizes this scenario.

In either case, no extra memory allocation or reference counting operations occur, and performance is equivalent to returning a raw pointer.

## release(), reset(), and get(): Three Key Operations

`std::unique_ptr` provides several methods for manual ownership management, and understanding their differences is crucial.

`get()` returns the internal raw pointer without transferring ownership. This is useful when you need to pass the pointer to a function that uses but does not own the object:

```cpp
void use_widget(Widget* w);
use_widget(ptr.get());
```

`release()` relinquishes ownership and returns the raw pointer—the `std::unique_ptr` becomes empty, but the object is not deleted. This is equivalent to "I'm giving you the object, you are responsible for releasing it":

```cpp
Widget* raw = ptr.release();
// ... use raw ...
delete raw; // Don't forget!
```

⚠️ `release()` is an operation that requires caution. Once you call it, you are back in the world of raw pointers—if you forget to `delete`, you get a memory leak. In most cases, using `std::move` to transfer ownership to another `std::unique_ptr` is the better choice.

`reset()` replaces the currently managed object. If no argument is passed, it simply releases the current object and sets the pointer to null:

```cpp
ptr.reset(); // Frees the Widget, ptr becomes null
ptr.reset(new Widget()); // Frees old Widget, manages new one
```

## Embedded Practice: Hardware Handle Management

In embedded development, `std::unique_ptr` combined with custom deleters can elegantly manage hardware resources. For example, managing a DMA buffer allocated via a HAL:

```cpp
// Custom deleter for HAL DMA buffer
auto dma_deleter = [](uint8_t* p) {
    if (p) {
        HAL_DMA_Free(p);
    }
};

using DmaBuffer = std::unique_ptr<uint8_t, decltype(dma_deleter)>;

DmaBuffer buffer(static_cast<uint8_t*>(HAL_DMA_Malloc(1024)), dma_deleter);

// Use buffer for DMA transfer...
// HAL_DMA_Start(buffer.get(), ...);
```

The benefit of this approach is that any return path—whether it's a normal return, error return, or exception—will correctly release the DMA buffer. In complex driver code, this automatic management significantly reduces bug rates.

The next chapter turns to `std::shared_ptr`—a completely different ownership model: shared ownership. That's where the real complexity begins.

## Reference Resources

- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
- [Empty Base Optimization and unique_ptr](https://www.cppstories.com/2021/no-unique-address/)
- Herb Sutter, *GotW #89: Smart Pointers*
