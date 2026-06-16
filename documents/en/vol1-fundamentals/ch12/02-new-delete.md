---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: Master new/delete usage and pitfalls, and understand the central role
  of RAII.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 内存布局
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Dynamic Memory Management
translation:
  source: documents/vol1-fundamentals/ch12/02-new-delete.md
  source_hash: 80e722d6ec632f866386473cc8439a75606e89a81c6d517f92ae03c52024a48f
  translated_at: '2026-06-16T03:47:49.967356+00:00'
  engine: anthropic
  token_count: 2535
---
# Dynamic Memory Management

In the previous chapter, we divided the program's memory space into four major areas: stack, heap, static area, and code segment. We clarified where data "lives" and how long it "survives." However, we left one suspense unresolved: How exactly do we manage dynamic memory on the heap? What happens behind the scenes with `new` and `delete`? Why has almost every previous chapter nagged us to "use smart pointers, don't write raw `new`/`delete`"?

In this chapter, we will answer these questions head-on. Dynamic memory is the greatest freedom C++ grants us—you can request memory of any size on demand at runtime, completely unconstrained by stack space limits. But this freedom brings the heaviest responsibility: every block of memory `new`'d must be properly `delete`'d, or it leaks; every `new` must correspond to the correct `delete`, or it is undefined behavior.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Correctly use `new`/`delete` and `new[]`/`delete[]` to avoid mismatch errors.
> - [ ] Use AddressSanitizer to detect memory leaks.
> - [ ] Understand how RAII binds heap resource lifetimes to stack objects.
> - [ ] Skillfully use `unique_ptr`, `shared_ptr`, `weak_ptr`, and their factory functions.
> - [ ] Understand the existence and use cases of `placement new`.

## Starting with new/delete

C++ uses `new` and `delete` to replace C's `malloc` and `free`. Simply put, `new` is a wrapper around `malloc` plus a constructor call; `delete` calls the destructor first, then reclaims the memory. This distinction is the fundamental watershed between C++ and C dynamic memory management.

When allocating a single object, for class types, `new` automatically calls the constructor, and `delete` automatically calls the destructor:

```cpp
struct Widget {
    Widget() { std::cout << "Constructed\n"; }
    ~Widget() { std::cout << "Destructed\n"; }
};

Widget* ptr = new Widget; // Allocates memory, then calls constructor
// ... use ptr ...
delete ptr;               // Calls destructor, then frees memory
```

When allocating an array, you must use `new[]`, and when freeing it, you must use the corresponding `delete[]`:

```cpp
Widget* arr = new Widget[10]; // Constructs 10 Widgets
// ... use arr ...
delete[] arr;                 // Destructs all 10, then frees memory
```

> **Warning**: Mismatching `new`/`delete` and `new[]`/`delete[]` is a classic error. Using `delete` to free an array allocated by `new[]` results in undefined behavior. For basic types like `int`, some platforms might "coincidentally" work without issues; but for class type arrays, `delete` (without the `[]`) will only call the destructor for the first element. The destructors for the remaining elements will never be called—if the destructors were responsible for releasing nested dynamic memory, the consequence is resource leakage. Make this an ironclad rule: `new` matches `delete`, `new[]` matches `delete[]`. It is better to write an extra `[]` than to rely on luck.

## Memory Leaks—The Silent Killer

How insidious can a memory leak be? Let's look at a simple scenario:

```cpp
void risky_function() {
    char* buffer = new char[4]; // Allocate 4 bytes
    if (some_condition) {
        return; // Oops! Forgot to delete buffer
    }
    delete[] buffer;
}
```

The function returns early, skipping `delete[]`, and those 4 bytes are lost forever. But even more insidious is exceptions: if code throws an exception between `new` and `delete`, control flow jumps directly to the `catch` block, completely bypassing `delete`. These leaks often don't appear during testing, but in production, a rare condition triggers an exception, and memory starts bleeding away drop by drop.

### Catching Leaks with AddressSanitizer

The good news is that modern compilers provide powerful runtime detection tools. AddressSanitizer (ASan) is a built-in memory error detector in GCC and Clang. Adding the `-fsanitize=address` compiler flag allows it to automatically detect leaks, out-of-bounds accesses, use-after-free, and more.

```bash
# Compile with ASan enabled
g++ -fsanitize=address -g leaky.cpp -o leaky
```

After compiling and running, ASan reports upon exit:

```text
=================================================================
==12345==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) object(s)
    #0 in operator new(unsigned long)
    #1 in risky_function() leaky.cpp:4
    #2 in main leaky.cpp:10

SUMMARY: AddressSanitizer: 4 byte(s) leaked
```

> **Warning**: ASan significantly slows down program execution (usually 2-5x slower) and increases memory usage (about 3-5x more), so it should only be used during debugging and testing. Be sure to remove `-fsanitize=address` in production builds. Additionally, ASan may conflict with some parallel debugging tools; if you encounter strange segmentation faults, try removing ASan to see if the tool itself is the issue.

## RAII: Binding Heap Resources to the Stack

The core problem with raw `new`/`delete` is that you must manually guarantee every block of memory is freed exactly once, whether via normal return, early `return`, or exception exit. C++'s answer is RAII—Resource Acquisition Is Initialization. The core idea is to bind the lifetime of a heap resource to a stack object: `new` in the constructor, `delete` in the destructor, utilizing the mechanism where destructors are automatically called when a stack object leaves scope.

```cpp
class SmartBuffer {
    char* data_;
public:
    explicit SmartBuffer(size_t size) : data_(new char[size]) {}
    ~SmartBuffer() { delete[] data_; } // Guaranteed to run
};
```

`SmartBuffer`'s destructor guarantees that `delete[]` will be executed—whether `risky_function` returns normally or exits due to an exception. In reality, we don't hand-write a wrapper class like `SmartBuffer` for every type; the standard library has already done this for us, and more thoroughly. These are smart pointers.

## Smart Pointers—The Standard Answer to RAII

C++11 introduced three smart pointers, all defined in the `<memory>` header, corresponding to different ownership semantics.

### unique_ptr—Exclusive Ownership

`unique_ptr` expresses "unique ownership": a block of memory can only be held by one `unique_ptr` at a time. It is not copyable, but it is movable—ownership can be transferred from one `unique_ptr` to another via `std::move`:

```cpp
#include <memory>

struct Widget { Widget() {} ~Widget() {} };

void unique_example() {
    // unique_ptr<Widget> p1 = new Widget; // ERROR! No implicit conversion
    std::unique_ptr<Widget> p1(new Widget); // OK, explicit

    std::unique_ptr<Widget> p2 = std::move(p1); // Transfer ownership
    // p1 is now null

    // p2 goes out of scope, Widget is automatically deleted
}
```

`std::make_unique` (C++14) is safer than directly using `new`—it combines allocation and construction in a single uninterruptible step, avoiding leaks in edge cases. C++11 projects can simply write `std::unique_ptr<Widget>(new Widget)`.

`unique_ptr` also supports custom deleters and array versions. A custom deleter allows you to perform custom actions when freeing memory, which is very useful in embedded development—for example, returning memory to a memory pool instead of the standard heap:

```cpp
// Custom deleter to return memory to a pool
auto pool_deleter = [](Widget* p) { memory_pool.release(p); };
std::unique_ptr<Widget, decltype(pool_deleter)> p(new Widget, pool_deleter);
```

The array version replaces `new[]`/`delete[]`: `std::unique_ptr<Widget[]>` automatically provides operator `[]`, and automatically calls `delete[]` when leaving scope.

### shared_ptr—Shared Ownership

`shared_ptr` allows multiple pointers to share ownership of the same memory block. Internally, it tracks via reference counting—incrementing on copy, decrementing on destruction, and automatically releasing when the count reaches zero.

```cpp
#include <memory>

void shared_example() {
    std::shared_ptr<Widget> p1 = std::make_shared<Widget>();
    std::shared_ptr<Widget> p2 = p1; // Both point to same Widget
    // ref_count == 2

    p1.reset(); // ref_count == 1
    p2.reset(); // ref_count == 0, Widget destroyed
}
```

`std::make_shared` is more efficient than `std::shared_ptr<T>(new T)`—it requires only one allocation to allocate both the control block and the object itself, whereas the latter requires two. Unless you need a custom deleter, you should prefer it.

> **Warning**: The reference counting of `shared_ptr` itself is thread-safe (atomic operations), but concurrent access to the pointed-to object is not—multiple threads reading and writing the same `shared_ptr` target is still a data race. Additionally, `shared_ptr` has performance overhead: memory overhead for the control block, atomic operation overhead for reference counting, and potential cache unfriendliness due to the object and control block not being on the same cache line. If your ownership semantics are unique, please use `unique_ptr`; do not abuse `shared_ptr` "just for safety."

### weak_ptr—Breaking Circular References

`shared_ptr` has a classic trap: circular references. If object A holds a `shared_ptr` to B, and object B also holds a `shared_ptr` to A, their reference counts will never reach zero, and the memory will never be released.

`weak_ptr` is designed to solve this problem. It is an "observer"—it can be constructed from a `shared_ptr` but does not increase the reference count. To access the object pointed to by a `weak_ptr`, you must first call `lock()` to promote it to a `shared_ptr`:

```cpp
struct Node {
    std::shared_ptr<Node> next;
    std::weak_ptr<Node> prev; // Breaks the cycle
};

// If prev were also a shared_ptr, p1->next and p2->prev would form a circular reference.
// Even if external p1 and p2 leave scope, the shared_ptrs they hold mutually keep the ref count at 1.
// With weak_ptr, the cycle is broken, and both nodes are released normally.
```

If `prev` were also a `shared_ptr`, `p1->next` and `p2->prev` would form a circular reference—even if external `p1` and `p2` leave scope, the `shared_ptr`s they hold mutually keep the reference count at 1, so they never destruct. Switching to `weak_ptr` breaks the cycle, allowing both nodes to be released normally.

## Placement New—Constructing Objects at a Specific Address

Ordinary `new` automatically finds memory on the heap, while `placement new` means "you provide the address, I just call the constructor." You are entirely responsible for allocating the memory.

```cpp
#include <new>

alignas(Widget) unsigned char buffer[sizeof(Widget)];

void placement_example() {
    // Construct Widget at the address of buffer
    Widget* p = new(buffer) Widget;

    // ... use p ...

    // Do NOT call delete p! Memory was not allocated with new.
    // Explicitly call destructor.
    p->~Widget();
}
```

`placement new` isn't used much in desktop development, but it's very valuable in embedded systems—it allows you to construct C++ objects in pre-allocated memory pools or shared memory. Note three points: buffer alignment must satisfy the object's requirements (`alignas` ensures this); the memory wasn't allocated with `new`, so you cannot call `delete`, you must explicitly call the destructor; explicitly calling a destructor is very rare in C++ and almost exclusive to this scenario.

## Hands-On—Raw Pointers vs. Smart Pointers

Let's integrate the previous content into a complete example—comparing raw pointers, smart pointers, and custom deleters.

```cpp
#include <iostream>
#include <memory>
#include <vector>

// Mock class
struct Resource {
    int data;
    Resource(int d) : data(d) { std::cout << "Acquire " << data << "\n"; }
    ~Resource() { std::cout << "Release " << data << "\n"; }
};

void raw_pointer_demo() {
    std::cout << "--- Raw Pointer ---\n";
    Resource* r = new Resource(100);
    // If we return early or throw here, we leak.
    // return;
    delete r;
}

void smart_pointer_demo() {
    std::cout << "--- Smart Pointer ---\n";
    auto r = std::make_unique<Resource>(200);
    // Even if we return early or throw here, r's destructor handles it.
    // return;
}

int main() {
    raw_pointer_demo();
    smart_pointer_demo();
    return 0;
}
```

Compile and run normally, output is as follows:

```text
--- Raw Pointer ---
Acquire 100
Release 100
--- Smart Pointer ---
Acquire 200
Release 200
```

If you uncomment the early `return` in `raw_pointer_demo`, ASan will report two leaks totaling 24 bytes. `smart_pointer_demo`, however, will never leak—this is the security of RAII.

## Exercises

### Exercise 1: Convert Raw Pointers to Smart Pointers

Rewrite the following code using smart pointers: use `unique_ptr` for single objects, and `shared_ptr` for shared objects.

```cpp
struct Device { void ping() {} };

void legacy_code() {
    Device* d1 = new Device;
    Device* d2 = new Device;
    // ... use d1, d2 ...
    delete d1;
    delete d2;
}
```

### Exercise 2: Implement a Simple Memory Pool with Custom Deleter

Implement a fixed-size memory pool class. Use `unique_ptr` with a custom deleter to manage objects allocated from the pool. Hint: the deleter doesn't have to `delete`; it can call `pool.free()` to return memory.

## Summary

In this chapter, we started from `new`/`delete` and walked a complete cognitive path. The problem with raw `new`/`delete` isn't complex syntax, but that you must guarantee `delete` is correctly executed on every possible exit path—normal return, early `return`, exception exit. Every omission is a potential memory leak. RAII fundamentally solves this by binding the lifetime of heap resources to stack objects.

`unique_ptr` is the default choice—zero overhead, exclusive ownership, non-copyable but movable. `shared_ptr` is for scenarios that truly require shared ownership, but be mindful of reference counting overhead and circular references. `weak_ptr` is the tool to break circular references; it observes but does not own. `std::make_unique` and `std::make_shared` are the preferred ways to create smart pointers. AddressSanitizer is a powerful tool for detecting memory issues and should always be enabled during development and testing.

With dynamic memory management mastered, our next step is to dive into a related topic—memory alignment and padding. Why does `sizeof` a struct with just a few fields always result in a few more bytes than the sum of the field sizes? The answer lies in the alignment rules.
