---
title: Destructors and resource management
description: Understanding the timing of destructor calls, an initial introduction
  to the design concepts of the RAII principle and the Rule of Three
chapter: 6
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- 构造函数
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
---
# Destructors and Resource Management

Constructors bring an object into a valid state—allocating memory, opening files, initializing hardware. But all these resources share a common problem: they must be returned at some point. `malloc` without `free`, `fopen` without `fclose`, locking a mutex without unlocking—the program slowly leaks resources, eventually exhausting system quotas or falling into a dead lock.

C++ solves this problem with the destructor. Constructors and destructors form a perfect symmetry: one executes automatically when an object is born, and the other executes automatically when it dies. This pattern of "acquire at construction, release at destruction" has a famous name—RAII (Resource Acquisition Is Initialization)—and it is the cornerstone of C++ resource management.

In this chapter, we break down destructors from start to finish—the syntax, invocation timing, the core idea of RAII, and a classic design guideline you cannot avoid: the Rule of Three.

## Destructor Syntax

Declaring a destructor is very simple: place a tilde `~` in front of the class name, with no parameters and no return type. A class can have only one destructor, and overloading is not supported.

```cpp
class FileWriter {
private:
    FILE* file_handle;

public:
    FileWriter(const char* path, const char* mode)
        : file_handle(std::fopen(path, mode))
    {
        if (file_handle == nullptr) {
            std::cerr << "Failed to open: " << path << std::endl;
        }
    }

    ~FileWriter() {
        if (file_handle != nullptr) {
            std::fclose(file_handle);
            std::cout << "File closed by destructor" << std::endl;
        }
    }

    void write(const char* data) {
        if (file_handle) { std::fputs(data, file_handle); }
    }
};
```

A destructor cannot accept parameters, so it cannot be overloaded; it also has no return value. These restrictions are easy to understand—the destructor is called automatically by the runtime, and the caller does not need to pass anything.

If you do not define a destructor, the compiler generates a default version that destructs non-static members in reverse order of their declaration. Classes containing only fundamental types do not need a hand-written destructor, but if a class manages external resources—dynamic memory, file handles, network connections—you must write your own destructor to release them. (This is normal, because the compiler does not know how you want to destruct your resources.)

## When Is a Destructor Called

Understanding the invocation timing is a prerequisite for using RAII correctly. **Stack objects** are automatically destructed when they leave scope, whether through a normal return, an early `return`, or exception stack unwinding:

```cpp
void process() {
    FileWriter writer("log.txt", "w");
    writer.write("Processing started\n");
}   // writer 在这里析构，文件自动关闭
```

**Heap objects** are destructed only when explicitly `delete`—this is one of the main sources of resource leaks in C++:

```cpp
void leaky() {
    FileWriter* writer = new FileWriter("log.txt", "w");
    writer->write("Oops\n");
    // 忘了 delete —— 析构不调用，文件永远不会关闭
}
```

> **Pitfall warning**: Forgetting to `delete` an object that was `new` means the destructor will never execute. Even if you remember to `delete` on the normal path, as long as an exception is thrown in between, the `delete` will be skipped. Modern C++ strongly recommends using smart pointers or stack objects instead of raw `new`/`delete`.

The destruction of **member objects** occurs after the containing class's destructor body finishes executing, in the exact reverse order of construction. We write a small program to verify this:

```cpp
#include <iostream>

struct Tracer {
    const char* name;
    explicit Tracer(const char* n) : name(n) {
        std::cout << "  [" << name << "] constructed" << std::endl;
    }
    ~Tracer() {
        std::cout << "  [" << name << "] destructed" << std::endl;
    }
};

struct Container {
    Tracer member_a;
    Tracer member_b;
    Container() : member_a("member_a"), member_b("member_b") {
        std::cout << "  [Container] ctor body" << std::endl;
    }
    ~Container() {
        std::cout << "  [Container] dtor body" << std::endl;
    }
};

int main() {
    std::cout << "=== begin ===" << std::endl;
    {
        Tracer local("local");
        Container container;
        Tracer* heap = new Tracer("heap");
        delete heap;
    }
    std::cout << "=== end ===" << std::endl;
}
```

Runtime output:

```text
=== begin ===
  [local] constructed
  [member_a] constructed
  [member_b] constructed
  [Container] ctor body
  [heap] constructed
  [heap] destructed
  [Container] dtor body
  [member_b] destructed
  [member_a] destructed
  [local] destructed
=== end ===
```

Construction is `local -> member_a -> member_b -> Container body`, and destruction is strictly reversed—"last constructed, first destructed" ensures resources are released in the correct hierarchy.

## RAII—The Core Idea of C++ Resource Management

RAII stands for Resource Acquisition Is Initialization, and its core idea can be summed up in one sentence: **bind the resource's lifetime to the object's lifetime**. Acquire the resource at construction, release it at destruction. Because the destructor is guaranteed to be called when the object leaves scope (even if an exception occurs), the resource is guaranteed to be correctly released.

Let us look at a practical example—a `ScopedTimer` for measuring the execution time of a code block:

```cpp
#include <chrono>
#include <iostream>

class ScopedTimer {
private:
    const char* label_;
    std::chrono::steady_clock::time_point start_;

public:
    explicit ScopedTimer(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now())
    {
        std::cout << "[" << label_ << "] started" << std::endl;
    }

    ~ScopedTimer() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
        std::cout << "[" << label_ << "] elapsed: "
                  << us.count() << " us" << std::endl;
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

void heavy_computation() {
    ScopedTimer timer("heavy_computation");
    for (int i = 0; i < 1000000; ++i) {
        volatile int x = i * i;
    }
}  // timer 在这里析构，自动打印耗时
```

You do not need to remember to "stop the timer" at the end of the function—the destructor does it for you automatically. Multiple `return` paths, exceptions—on every path, the timer will be correctly destroyed. This is the power of RAII: **it makes "not leaking" the default behavior, rather than a "remember to do it" maintained by discipline**.

> **Pitfall warning**: The prerequisite for RAII is that the object must live on the stack (or be a global/static object), not a heap object created via raw `new`. If you `new` an RAII object but forget to `delete`, the destructor still will not be called—RAII cannot save you. Modern C++ recommends: **keep objects on the stack whenever possible**, and if you must use the heap, use smart pointers.

## Rule of Three—A Design Warning Signal

The Rule of Three is a classic design guideline: **if your class needs to customize any one of the following three, you almost certainly need to customize the other two as well**—the destructor, the copy constructor, and the copy assignment operator.

These three functions collectively determine "how an object is copied" and "how it is destroyed." Writing a destructor usually means the class manages a resource that requires manual release, but the compiler-generated copy operations only perform a shallow copy—after the pointer member is copied, both objects point to the same resource, leading to a double free upon destruction.

```cpp
class NaiveBuffer {
    int* data_;
    std::size_t size_;
public:
    explicit NaiveBuffer(std::size_t n) : size_(n), data_(new int[n]()) {}
    ~NaiveBuffer() { delete[] data_; }
    // 没有自定义拷贝——编译器生成的版本做浅拷贝
};

void bug_demo() {
    NaiveBuffer a(10);
    NaiveBuffer b = a;  // 浅拷贝：b.data_ == a.data_
    // 作用域结束时 double free —— 未定义行为！
}
```

One way to fix this is to simply forbid copying:

```cpp
class SafeBuffer {
    int* data_;
    std::size_t size_;
public:
    explicit SafeBuffer(std::size_t n) : size_(n), data_(new int[n]()) {}
    ~SafeBuffer() { delete[] data_; }
    SafeBuffer(const SafeBuffer&) = delete;
    SafeBuffer& operator=(const SafeBuffer&) = delete;
};
```

Here we are just previewing the concept. Once we cover move semantics, the Rule of Three will expand into the Rule of Five. For now, you only need to remember: **once you hand-write a destructor, stop and think—can your class be safely copied? If not, delete the copy operations**.

## Virtual Destructors—The Hidden Trap of Polymorphism

If a class will be inherited, and users manipulate derived class objects through a base class pointer, then the base class's destructor must be `virtual`. Otherwise, when `delete` the base class pointer, the derived class's destructor will be completely skipped.

```cpp
class Base {
public:
    ~Base() { std::cout << "~Base" << std::endl; }  // 非 virtual！
};

class Derived : public Base {
    int* resource_;
public:
    Derived() : resource_(new int[100]) {}
    ~Derived() { delete[] resource_; std::cout << "~Derived" << std::endl; }
};

void leak_demo() {
    Base* ptr = new Derived();
    delete ptr;  // 只调用 ~Base()，~Derived() 被跳过 → 内存泄漏
}
```

The output is only `~Base`—the 400 bytes of memory pointed to by `resource_` silently leak. The fix is simply to add `virtual` in front of the base class destructor:

```cpp
class Base {
public:
    virtual ~Base() { std::cout << "~Base" << std::endl; }
};
```

> **Pitfall warning**: The condition for applying this rule is that the class will be used as a polymorphic base class. A safe rule of thumb is: **as long as your class has `virtual` functions, the destructor should be `virtual`**. Conversely, a class without `virtual` functions does not need a virtual destructor—adding one instead incurs the overhead of a virtual function table pointer for every object. This topic will be explored in depth in the next chapter on inheritance and polymorphism.

## Hands-on: Destructors in Action

Now let us write a complete piece of code, chaining `ScopedTimer` and `FileWriter` together to demonstrate the practical effect of RAII:

```cpp
// destructor.cpp
// 编译：g++ -std=c++17 -o destructor destructor.cpp

#include <chrono>
#include <cstdio>
#include <iostream>

/// @brief 作用域计时器
class ScopedTimer {
    const char* label_;
    std::chrono::steady_clock::time_point start_;
public:
    explicit ScopedTimer(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now())
    { std::cout << "[" << label_ << "] started" << std::endl; }

    ~ScopedTimer() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
        std::cout << "[" << label_ << "] finished: "
                  << us.count() << " us" << std::endl;
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

/// @brief 自动管理 FILE* 的文件写入器
class FileWriter {
    FILE* handle_;
    const char* path_;
public:
    FileWriter(const char* path, const char* mode)
        : handle_(std::fopen(path, mode)), path_(path)
    {
        if (!handle_) std::cerr << "Error: cannot open " << path << std::endl;
    }

    ~FileWriter() {
        if (handle_) {
            std::fclose(handle_);
            std::cout << "[" << path_ << "] closed" << std::endl;
        }
    }

    void write_line(const char* text) {
        if (handle_) { std::fputs(text, handle_); std::fputc('\n', handle_); }
    }

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
};

int main() {
    std::cout << "--- RAII demo ---" << std::endl;
    ScopedTimer total("total");

    {
        ScopedTimer phase("phase 1: file writing");
        FileWriter writer("raii_demo.txt", "w");
        writer.write_line("Hello from RAII!");
        writer.write_line("No manual fclose needed.");
    }

    {
        ScopedTimer phase("phase 2: computation");
        volatile int sum = 0;
        for (int i = 0; i < 1000000; ++i) { sum += i; }
    }

    std::cout << "--- end of main ---" << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -o destructor destructor.cpp && ./destructor
```

Output:

```text
--- RAII demo ---
[total] started
[phase 1: file writing] started
[raii_demo.txt] closed
[phase 1: file writing] finished: 123 us
[phase 2: computation] started
[phase 2: computation] finished: 4567 us
--- end of main ---
[total] finished: 4789 us
```

The inner `ScopedTimer` and `FileWriter` are destructed first, and the outer `total` is destructed last. You can verify the file contents:

```bash
cat raii_demo.txt
# Hello from RAII!
# No manual fclose needed.
```

The content is correct, and we did not hand-write `fclose`—the destructor completed all the cleanup for us.

## Exercises

**Exercise 1: Scoped Log Timer**. Write a `ScopedLogger` class that records a timestamp upon construction (format `HH:MM:SS`) and prints "elapsed X seconds" upon destruction. Hint: use `std::time` and `std::localtime` from `<ctime>`.

**Exercise 2: Simple File Handle**. Implement a `FileHandle` class that opens a file upon construction and automatically closes it upon destruction. Provide a `read_line()` method (returning `std::string`) and a `is_valid()` method. Think about this from the perspective of the Rule of Three: does this class need to disable copying? Why?

## Summary

In this chapter, we covered the syntax, invocation timing, and core role of destructors in resource management. Destructors are automatically called when an object leaves scope or is `delete`. RAII binds resource acquisition and release to the object's lifetime, making "not leaking" the default behavior. The Rule of Three reminds us to re-examine copy semantics when hand-writing a destructor. Virtual destructors are a hard requirement in polymorphic scenarios.

In the next article, we will look at another important mechanism of classes—static members.
