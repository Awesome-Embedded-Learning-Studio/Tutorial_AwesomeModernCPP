---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand when destructors are called, and get a first look at the RAII
  principle and the design rationale behind the Rule of Three.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 构造函数
reading_time_minutes: 10
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Destructors and Resource Management
translation:
  source: documents/vol1-fundamentals/ch06/03-destructors.md
  source_hash: f4bedcedad2176866b3f4b5181295ecec334fc2fda367727a71e5e7817e7e89f
  translated_at: '2026-06-16T03:44:23.507394+00:00'
  engine: anthropic
  token_count: 2395
---
# Destructors and Resource Management

Constructors are responsible for bringing an object into a valid state—allocating memory, opening files, initializing hardware. But all these resources share a common problem: they must be returned at some point. Memory allocated without freeing, files opened without closing, mutexes locked without unlocking—the program will slowly leak resources, eventually exhausting system quotas or falling into a deadlock.

C++ solves this problem with the destructor. Constructors and destructors form a perfect symmetry: one executes automatically when the object is born, the other when it dies. This pattern of "acquire at construction, release at destruction" has a famous name—RAII (Resource Acquisition Is Initialization)—and it is the cornerstone of C++ resource management.

In this chapter, we will break down destructors from start to finish—syntax, timing, the core idea of RAII, and a classic design guideline we cannot avoid: the Rule of Three.

## Destructor Syntax

Declaring a destructor is very simple: add a tilde `~` before the class name, with no parameters and no return type. A class can have only one destructor, and overloading is not supported.

```cpp
class MyClass {
public:
    ~MyClass() { // Destructor
        // Cleanup code here
    }
};
```

A destructor cannot accept parameters, so overloading is impossible; it also has no return value. These restrictions make sense—the runtime calls the destructor automatically, so the caller doesn't pass anything.

If you don't define a destructor, the compiler generates a default version that destructs non-static members in reverse order of their declaration. Classes containing only basic types don't need a handwritten destructor. However, if a class manages external resources—dynamic memory, file handles, network connections—you must write a destructor to release them. (This is normal, because the compiler doesn't know how you want to destroy your resources.)

## When is the Destructor Called

Understanding the timing is a prerequisite for using RAII correctly. **Stack objects** are automatically destructed when they leave scope, whether via normal return, early `return`, or exception stack unwinding:

```cpp
#include <iostream>

void func() {
    int* p = new int(42);
    std::cout << "Function start\n";
    // ... some code ...
    if (some_error) {
        delete p; // Manual cleanup required before early return
        return;
    }
    delete p;
}
```

**Heap objects** are destructed only when explicitly `delete`d—this is one of the main sources of resource leaks in C++:

```cpp
void leak() {
    int* p = new int(42);
    // If we return or throw here, p leaks!
    delete p;
}
```

> **Pitfall Warning**: For an object created with `new`, if you forget `delete`, the destructor never executes. Even if you remember to `delete` on the normal path, if an exception is thrown in between, the `delete` will be skipped. Modern C++ strongly recommends using smart pointers or stack objects instead of raw `new`/`delete`.

**Member objects** are destructed after the containing class's destructor body finishes, in strictly the reverse order of construction. Let's write a small program to verify this:

```cpp
#include <iostream>

class Member {
public:
    Member(const char* name) : name_(name) { std::cout << name_ << " constructed\n"; }
    ~Member() { std::cout << name_ << " destructed\n"; }
private:
    const char* name_;
};

class Container {
public:
    Container() : b_("B"), a_("A") {} // Initialization order is declaration order
    ~Container() { std::cout << "Container destructed\n"; }
private:
    Member a_; // Declared first
    Member b_; // Declared second
};

int main() {
    Container c;
}
```

Output:

```text
A constructed
B constructed
Container destructed
B destructed
A destructed
```

Construction is "first declared, first constructed"; destruction is strictly the reverse—"last constructed, first destructed"—ensuring resources are released at the correct levels.

## RAII—The Core Idea of C++ Resource Management

RAII stands for Resource Acquisition Is Initialization. The core concept is simple: **bind the resource lifecycle to the object lifecycle**. Acquire resources in the constructor, release them in the destructor. Because the destructor is guaranteed to be called when the object leaves scope (even if exceptions occur), the resource is guaranteed to be released correctly.

Let's look at a practical example—a `ScopedTimer` for measuring code block execution time:

```cpp
#include <iostream>
#include <chrono>

class ScopedTimer {
public:
    ScopedTimer(const char* name)
        : name_(name), start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        std::cout << name_ << " took " << duration.count() << " us\n";
    }
private:
    const char* name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

void complex_task() {
    ScopedTimer t("complex_task");
    // ... do work ...
} // Timer stops automatically here
```

You don't need to remember to "stop the timer" at the end of the function—the destructor does it for you automatically. Multiple `return` paths, exceptions—on every path, the timer is correctly destroyed. This is the power of RAII: **it makes "not leaking" the default behavior, rather than a "remember to do it" maintained by discipline**.

> **Pitfall Warning**: RAII relies on the object living on the stack (or as a global/static object), not a heap object created with raw `new`. If you `new` a RAII object but forget to `delete`, the destructor won't be called—RAII can't save you. Modern C++ advice: **keep objects on the stack whenever possible**; if you must use the heap, use smart pointers.

## Rule of Three—A Design Warning Signal

The Rule of Three is a classic design guideline: **if your class needs to customize any one of the following three, you almost certainly need to customize the other two as well**—destructor, copy constructor, copy assignment operator.

These three functions together determine "how an object is copied" and "how an object is destroyed." Writing a destructor usually means the class manages resources requiring manual release. The compiler-generated copy operations perform a shallow copy—after pointer members are copied, two objects point to the same resource, leading to double free upon destruction.

```cpp
class BadBuffer {
public:
    BadBuffer(size_t size) : data_(new int[size]), size_(size) {}
    ~BadBuffer() { delete[] data_; } // Manages memory

    // Compiler-generated copy constructor and assignment do shallow copy!
    // BadBuffer other = buf; // Disaster: both share data_
private:
    int* data_;
    size_t size_;
};
```

One fix is to simply forbid copying:

```cpp
class GoodBuffer {
public:
    GoodBuffer(const GoodBuffer&) = delete;
    GoodBuffer& operator=(const GoodBuffer&) = delete;
    // ... rest of the class ...
};
```

Here we are just previewing the concept. Once we cover move semantics, the Rule of Three expands to the Rule of Five. For now, just remember: **once you write a destructor, stop and think—can your class be safely copied? If not, delete it**.

## Virtual Destructors—The Invisible Trap of Polymorphism

If a class is to be inherited, and users manipulate derived class objects via base class pointers, the base class destructor must be `virtual`. Otherwise, when `delete`ing a base class pointer, the derived class destructor is completely skipped.

```cpp
class Base {
public:
    ~Base() { std::cout << "Base destroyed\n"; } // Not virtual!
};

class Derived : public Base {
public:
    Derived() : data_(new int[100]) {}
    ~Derived() {
        delete[] data_;
        std::cout << "Derived destroyed\n";
    }
private:
    int* data_;
};

int main() {
    Base* p = new Derived();
    delete p; // Undefined Behavior: Derived destructor not called
}
```

Output is only `Base destroyed`—the 400 bytes of memory pointed to by `data_` are silently leaked. The fix is simply adding `virtual` to the base class destructor:

```cpp
class Base {
public:
    virtual ~Base() { std::cout << "Base destroyed\n"; }
};
```

> **Pitfall Warning**: This rule applies when the class is used as a polymorphic base class. A safe rule of thumb: **as long as your class has `virtual` functions, the destructor should be `virtual`**. Conversely, a class without `virtual` functions doesn't need a virtual destructor—adding one only adds the overhead of a vtable pointer to every object. This topic will be expanded in the next chapter on inheritance and polymorphism.

## Practice: Destructors in Action

Now let's write a complete piece of code to combine `ScopedTimer` and `ScopedFile` to demonstrate the practical effect of RAII:

```cpp
#include <iostream>
#include <fstream>
#include <chrono>

class ScopedTimer {
public:
    ScopedTimer(const std::string& name)
        : name_(name), start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_);
        std::cout << "[" << name_ << "] took " << duration.count() << " ms\n";
    }
private:
    std::string name_;
    std::chrono::time_point<std::chrono::steady_clock> start_;
};

class ScopedFile {
public:
    ScopedFile(const std::string& filename) : file_(filename) {
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open file");
        }
        std::cout << "File " << filename << " opened\n";
    }

    ~ScopedFile() {
        if (file_.is_open()) {
            file_.close();
            std::cout << "File closed\n";
        }
    }

    std::ofstream& get_stream() { return file_; }

    // Disable copy (Rule of Three)
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

private:
    std::ofstream file_;
};

int main() {
    ScopedFile file("demo.txt");
    {
        ScopedTimer t("inner_block");
        auto& out = file.get_stream();
        out << "Hello RAII!\n";
        for (int i = 0; i < 1000; ++i) {
            out << "Number: " << i << "\n";
        }
    } // Timer ends here

    // Outer scope work
    file.get_stream() << "Done.\n";
} // File closes here
```

Compile and run:

```bash
g++ -std=c++20 raii_demo.cpp -o raii_demo
./raii_demo
```

Output:

```text
File demo.txt opened
[inner_block] took 15 ms
File closed
```

The inner `ScopedTimer` and `ScopedFile`'s internal stream buffer are destructed first, and the outer `ScopedFile` is destructed last. You can verify the file content:

```bash
cat demo.txt
```

The content is correct; we didn't manually write `file.close()`—the destructor handled all the cleanup for us.

## Exercises

**Exercise 1: Scope Log Timer**. Write a `LogTimer` class that records a timestamp upon construction (format `HH:MM:SS`) and prints "elapsed X seconds" upon destruction. Hint: Use `std::chrono` and `std::ctime`.

**Exercise 2: Simple File Handle**. Implement a `FileHandle` class that opens a file in the constructor and closes it automatically in the destructor. Provide a `get()` method (returning `FILE*`) and a `write()` method. Think about the Rule of Three: does this class need to disable copying? Why?

## Summary

In this chapter, we covered syntax, timing, and the central role of destructors in resource management. Destructors are called automatically when an object leaves scope or is `delete`d. RAII binds resource acquisition and release to the object lifecycle, making "no leaks" the default behavior. The Rule of Three reminds us to reconsider copy semantics when writing destructors. Virtual destructors are a hard requirement in polymorphic scenarios.

Next, we will look at another important class mechanism—static members.
