---
title: 'unique_ptr Explained: A Zero-Overhead Smart Pointer with Exclusive Ownership'
description: 'Deep dive into unique_ptr: implementation principles, usage, and best
  practices'
chapter: 1
order: 2
tags:
- host
- cpp-modern
- intermediate
- unique_ptr
- 智能指针
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 18
prerequisites:
- 'Chapter 1: RAII 深入理解'
related:
- shared_ptr 详解
- 自定义删除器
---
# unique_ptr In-Depth: The Zero-Overhead Smart Pointer for Exclusive Ownership

In the previous article, we discussed RAII (Resource Acquisition Is Initialization)—the cornerstone of C++ resource management. Now let's look at the most direct manifestation of the RAII philosophy in the realm of smart pointers: `std::unique_ptr`. The design philosophy of this class can be summed up in one sentence: **one object, one owner, zero overhead**. It doesn't bother with reference counting, atomic operations, or extra control block allocations—you give it an object, it manages it for you; you leave the scope, it deletes it for you. It's that simple. (By the way, why do interviewers love asking about this so much?)

But simple doesn't mean shallow. The topics behind `unique_ptr`—ownership semantics, move semantics, custom deleters, and EBO (Empty Base Optimization)—are each worth a deep dive. Today, we'll break them all down.

## Exclusive Ownership: Why It Can't Be Copied

The core semantic of `unique_ptr` is "exclusive"—at any given time, only one `unique_ptr` owns the object. This means it does not allow copy construction or copy assignment, only move operations. This isn't a limitation, but rather a precise design expression: if copying were allowed, both `unique_ptr` instances would believe they own the object, and when they both leave scope, they would both try to delete it—a double free leading directly to undefined behavior (UB).

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
    // auto p2 = p1;              // 编译错误！unique_ptr 不可拷贝
    auto p2 = std::move(p1);      // OK：所有权从 p1 转移到 p2

    // 此时 p1 == nullptr，p2 拥有对象
    std::cout << "p1: " << p1.get() << "\n";  // 输出: 0 或 nullptr
    std::cout << "p2: " << p2.get() << "\n";  // 输出: 有效地址
    std::cout << "p2->value: " << p2->value << "\n";  // 输出: 42
}   // p2 析构，Widget 自动被 delete
```

Output:

```text
Widget(42) 构造
p1: 0
p2: 0x55a3c8f42eb0
p2->value: 42
~Widget(42) 析构
```

This "non-copyable, movable" design perfectly maps to real-world ownership transfer—just like handing a physical key to someone else, meaning you no longer possess it. At the code level, `std::move` transfers the raw pointer inside `p1` to `p2`, and then sets `p1` to null. The entire process involves no extra memory allocation and no reference counting overhead.

## make_unique vs new: Why C++14 Added This Function

C++11 introduced `std::unique_ptr` but forgot to provide `std::make_unique` (widely considered an oversight), which wasn't added until C++14. So what advantages does `make_unique` have over directly using `new`?

First is **exception safety**. Consider the following function call:

```cpp
// 假设有这样一个函数签名
void process(std::unique_ptr<Widget> ptr, int computed_value);

// 危险写法（C++11 风格）
process(std::unique_ptr<Widget>(new Widget(42)), compute_something());

// 安全写法（C++14 风格）
process(std::make_unique<Widget>(42), compute_something());
```

In the dangerous approach, the C++ compiler needs to complete the following in sequence before calling `process`: `new Widget(42)`, construct `unique_ptr`, and call `compute_something()`. **Prior to C++17**, the C++ standard did not specify the evaluation order of function arguments—the compiler might `new` first, then call `compute_something()`, and finally construct `unique_ptr`. If `compute_something()` throws an exception, the `Widget` created by `new` would leak—because `unique_ptr` hasn't had a chance to take ownership of it yet.

⚠️ **Important update**: Starting from **C++17**, the standard mandates that function arguments must be evaluated left-to-right. Therefore, in C++17 and later, the dangerous approach is actually safe. However, `make_unique` still has other advantages (concise code, avoiding repeated type names) and is compatible with older standards, so it remains the recommended practice.

`make_unique` wraps allocation and construction in a single function call, eliminating this "intermediate state" and thus ensuring exception safety.

Second is **code conciseness**. `make_unique` avoids exposing raw `new` in your code, reducing the chance of errors:

```cpp
// 对比
auto p1 = std::unique_ptr<Widget>(new Widget(42));  // 啰嗦，且容易忘写 unique_ptr
auto p2 = std::make_unique<Widget>(42);              // 简洁，不可能忘记管理
```

⚠️ `make_unique` has one limitation: it does not support custom deleters. If you need a custom deleter (for example, to manage memory allocated by `FILE*` or `malloc`), you must construct the `unique_ptr` directly. We will discuss this issue in detail in the "Custom Deleters" section later.

## The Deep Relationship Between Move Semantics and unique_ptr

`unique_ptr` has a very tight relationship with move semantics. Before C++11, C++ only had copy semantics—"copying" an object. But for `unique_ptr`, copying means "two pointers pointing to the same object," which violates the exclusive ownership semantic. The introduction of move semantics perfectly solved this problem: moving is not "copying," but "transferring"—the source object gives up ownership, and the target object takes over.

This allows `unique_ptr` to be stored in standard containers:

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

    // push_back 需要移动，因为 unique_ptr 不可拷贝
    sensors.push_back(std::make_unique<Sensor>(1));
    sensors.push_back(std::make_unique<Sensor>(2));
    sensors.push_back(std::make_unique<Sensor>(3));

    // vector 扩容时，内部的 unique_ptr 会通过移动构造转移
    // 这也是为什么 unique_ptr 的移动操作标记为 noexcept
    for (const auto& s : sensors) {
        std::cout << "Sensor id: " << s->id << "\n";
    }

    // 从函数返回 unique_ptr 也是通过移动（或 RVO）
    auto make_sensor = [](int id) -> std::unique_ptr<Sensor> {
        return std::make_unique<Sensor>(id);
    };

    auto s = make_sensor(99);
    std::cout << "Created sensor " << s->id << "\n";
}
```

Here is an important detail: both the move constructor and move assignment operator of `unique_ptr` are marked as `noexcept`. This directly impacts the behavior of `std::vector`—when a vector reallocates, if the move constructor of the element is `noexcept`, the vector will prefer to use move operations; otherwise, it falls back to copying (but `unique_ptr` is not copyable, so it must be moved). Therefore, `noexcept` having `unique_ptr` move operations is the key guarantee that allows it to be safely stored in containers.

You can run `code/volumn_codes/vol2/ch01-smart-pointers/test_vector_noexcept.cpp` to verify this. This example demonstrates how a vector safely moves objects managed by `unique_ptr` during reallocation, and verifies that all elements remain valid after the reallocation.

## unique_ptr<T[]>: The Array Version

`unique_ptr` has a partial specialization for arrays, `unique_ptr<T[]>`, which calls `delete[]` instead of `delete` upon destruction.

```cpp
auto arr = std::make_unique<int[]>(64);  // 分配 64 个 int
arr[0] = 42;
arr[1] = 17;
// 析构时自动 delete[]
```

That said, scenarios requiring manual management of dynamic arrays in C++ are already very rare. If you need a fixed-size array, using `std::array` or `std::vector` is almost always a better choice. `unique_ptr<T[]>` is primarily used for interfacing with C APIs that return dynamically allocated arrays, such as:

```cpp
// 假设某个 C API 返回 malloc 分配的数组
extern "C" int* create_buffer(size_t size);
extern "C" void free_buffer(int* buf);

auto buffer = std::unique_ptr<int[], void(*)(int*)>(
    create_buffer(1024),
    [](int* p) { free_buffer(p); }
);
buffer[0] = 42;
```

⚠️ I strongly recommend against using `unique_ptr<T[]>` as a replacement for `std::vector`. `vector` provides `size()`, iterators, and bounds checking (via `at()`), whereas `unique_ptr<T[]>` offers nothing beyond automatic deallocation.

## Custom Deleter Basics

The second template parameter of `unique_ptr` is the deleter type. The default is `std::default_delete<T>`, which internally simply performs `delete ptr`. But you can replace it with any callable object—a function pointer, a lambda, or a function object—as long as it matches the `void operator()(T*)` signature.

The most common scenario is managing resources returned by C APIs:

```cpp
#include <cstdio>
#include <memory>

// 函数指针作为删除器
using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;

FilePtr open_file(const char* path, const char* mode) {
    FILE* f = std::fopen(path, mode);
    return FilePtr(f, &std::fclose);
}

// lambda 作为删除器（无捕获 → 无状态 → 零开销）
auto make_closer = []() {
    auto deleter = [](FILE* f) noexcept { if (f) std::fclose(f); };
    return std::unique_ptr<FILE, decltype(deleter)>(std::fopen("/tmp/log", "w"), deleter);
};
```

Using a function object (functor) as a deleter is also a common choice, especially when you want the deleter type to have a name:

```cpp
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

// 管理 malloc 分配的内存
auto buf = std::unique_ptr<char, FreeDeleter>(
    static_cast<char*>(std::malloc(256))
);
```

We will dive deeper into custom deleters (stateful deleters, EBO optimization, deleters in `shared_ptr`, etc.) in a dedicated article on "Custom Deleters and Intrusive Reference Counting."

## Zero-Overhead Proof: sizeof and Assembly Analysis

`unique_ptr` is often touted as a "zero-overhead abstraction," but this isn't just marketing—we can verify it with actual code. First, let's compare `sizeof`:

```cpp
#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                  " << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):        " << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>): "
              << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";

    // 函数指针作为删除器——有额外开销
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>): "
              << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
}
```

Typical output on a 64-bit platform:

```text
sizeof(int*):                  8
sizeof(unique_ptr<int>):        8
sizeof(unique_ptr<int, EmptyDeleter>): 8
sizeof(unique_ptr<int, void(*)(int*)>): 16
```

A `unique_ptr` with the default deleter or a stateless function object is exactly the same size as a raw pointer—8 bytes. This is thanks to EBO (Empty Base Optimization): `unique_ptr` typically inherits from the deleter type internally, and when the deleter is an empty class (has no data members), the compiler optimizes its size to zero, so `unique_ptr` only needs to store that single raw pointer.

You can run `code/volumn_codes/vol2/ch01-smart-pointers/test_ebo_sizeof.cpp` to verify this. Typical output on the x86_64-linux platform (g++ 15.2.1):

```text
sizeof(int*):                                  8 bytes
sizeof(unique_ptr<int>):                        8 bytes
sizeof(unique_ptr<int, EmptyDeleter>):         8 bytes
sizeof(unique_ptr<int, void(*)(int*)>):        16 bytes
sizeof(unique_ptr<int, StatefulDeleter>):      16 bytes
```

As we can see, when using a stateless deleter, the size of `unique_ptr` is exactly the same as a raw pointer, while using a function pointer or a stateful deleter incurs additional overhead.

When using a function pointer as the deleter, `unique_ptr` needs to store an additional function pointer, so the size doubles to 16 bytes. This reveals the prerequisite for "zero overhead": **the deleter must be stateless**.

Let's verify this from an assembly perspective as well. Here is a simple example:

```cpp
// 用 unique_ptr 管理 int
int use_unique_ptr() {
    auto p = std::make_unique<int>(42);
    return *p;
}

// 等价的裸指针版本
int use_raw_ptr() {
    int* p = new int(42);
    int v = *p;
    delete p;
    return v;
}
```

With optimizations enabled (`-O2`), the assembly code generated for these two functions is almost identical. If you check `code/volumn_codes/vol2/ch01-smart-pointers/test_assembly_optimization.cpp` and compile with `g++ -std=c++17 -O2 -S`, you'll see both functions generate:

```asm
movl    $42, %eax
ret
```

The compiler inlines and optimizes away the construction and destruction of `unique_ptr`, and even eliminates `new` and `delete` (because the object's lifetime is very short and it has no side effects). This is the power of C++ abstraction: you gain safety and readability at the source code level, without paying any cost at the machine code level.

## The PIMPL Idiom: Hiding Implementation Details

PIMPL (Pointer to Implementation) is a classic technique in C++ for reducing compilation dependencies. `unique_ptr`'s support for incomplete types makes it the best tool for implementing PIMPL.

Header file `widget.h`:

```cpp
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();  // 必须声明，在实现文件中定义

    Widget(Widget&&) noexcept;
    Widget& operator=(Widget&&) noexcept;

    // 禁止拷贝（或自行实现深拷贝）
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    void do_something();

private:
    struct Impl;                  // 前向声明，不完整类型
    std::unique_ptr<Impl> impl_;  // unique_ptr 支持不完整类型
};
```

Implementation file `widget.cpp`:

```cpp
#include "widget.h"
#include <iostream>
#include <string>

// 真正的实现在这里定义——头文件的包含者完全看不到这些细节
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

Widget::~Widget() = default;  // 在这里 Impl 是完整类型，delete 能正确执行

Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;

void Widget::do_something() {
    impl_->do_work();
}
```

The benefits of PIMPL are obvious: modifying the definition of `Impl` (such as adding members or changing methods) only requires recompiling `widget.cpp`, and all files that include `widget.h` do not need to be recompiled. For large projects, this can significantly reduce compilation time.

The complete PIMPL example code can be found in `code/volumn_codes/vol2/ch01-smart-pointers/`:

- `pimpl_widget.h` - Public interface header file
- `pimpl_widget.cpp` - Implementation (containing the full definition of `Widget::Impl`)
- `pimpl_user.cpp` - User code example

You can compile and run it like this:

```bash
cd code/volumn_codes/vol2/ch01-smart-pointers
g++ -std=c++17 -c pimpl_widget.cpp -o pimpl_widget.o
g++ -std=c++17 -c pimpl_user.cpp -o pimpl_user.o
g++ -std=c++17 pimpl_widget.o pimpl_user.o -o test_pimpl
./test_pimpl
```

This example demonstrates the key characteristics of the PIMPL pattern: the public interface exposes absolutely no implementation details, and modifying the `Impl` struct does not require recompiling user code.

⚠️ There are a few things to note when using `unique_ptr` with PIMPL. First, `~Widget()` must be defined in the implementation file—because destruction requires `Impl` to be a complete type, whereas the header file only has a forward declaration. Second, the move constructor and move assignment operator should also be `= default` in the implementation file for the same reason. If you `= default` them in the header file, the compiler will try to instantiate the destructor of `unique_ptr<Impl>` in the header, at which point `Impl` is incomplete, resulting in a compilation error.

## Factory Functions Returning unique_ptr

Having factory functions return `unique_ptr` is a very common pattern. It is not only safe (callers can't possibly forget to release the object), but it also expresses clear ownership semantics: the factory creates the object, and the caller exclusively owns it.

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
        // 写入文件（省略具体实现）
    }
private:
    std::string path_;
};

// 工厂函数：返回 unique_ptr<Logger>
std::unique_ptr<Logger> create_logger(bool use_file, const std::string& path = "") {
    if (use_file) {
        return std::make_unique<FileLogger>(path);
    }
    return std::make_unique<ConsoleLogger>();
}

// 使用
void application() {
    auto logger = create_logger(true, "/tmp/app.log");
    logger->log("Application started");

    // 也可以通过移动把所有权传递给其他组件
    // set_global_logger(std::move(logger));
}
```

There's another nice aspect to this pattern: the factory function returns a `unique_ptr<Logger>` (base class pointer), but actually creates a `ConsoleLogger` or `FileLogger` (derived class object). As long as `Logger` has a virtual destructor (and we did declare `virtual ~Logger() = default`), polymorphic destruction is safe.

It's worth noting that returning a `unique_ptr` does not incur any performance penalty. In modern compilers, return value optimization (RVO) and move semantics ensure the entire process is zero-copy—the `unique_ptr` created in the factory function is directly "moved" into the caller's variable.

Specifically:

- C++11/14: Relies primarily on move semantics (move constructor)
- C++17: Guaranteed copy elision further optimizes this scenario

In either case, no extra memory allocation or reference counting operations occur, and the performance is equivalent to returning a raw pointer directly.

## release(), reset(), and get(): Three Key Operations

`unique_ptr` provides several methods for manually managing ownership, and understanding their differences is crucial.

`get()` returns the internal raw pointer without transferring ownership. This is useful when you need to pass the pointer to a function that uses but does not own it:

```cpp
void print_widget(const Widget* w);

auto p = std::make_unique<Widget>(42);
print_widget(p.get());  // 传给只读函数，p 仍然拥有对象
```

`release()` relinquishes ownership and returns the raw pointer—the `unique_ptr` becomes empty, but the object is not deleted. This is equivalent to saying, "I'm handing this object over to you; you are responsible for releasing it":

```cpp
auto p = std::make_unique<Widget>(42);
Widget* raw = p.release();  // p 变为 nullptr，raw 指向对象
// ... 使用 raw ...
delete raw;  // 你必须手动释放
```

⚠️ `release()` is an operation that requires caution. Once you call it, you are back in the world of raw pointers—if you forget to `delete`, you will have a memory leak. In most cases, using `std::move()` to transfer ownership to another `unique_ptr` is the better choice.

`reset()` replaces the currently managed object. If no argument is passed, it simply releases the current object and sets the pointer to null:

```cpp
auto p = std::make_unique<Widget>(1);
p.reset(new Widget(2));  // 释放 Widget(1)，接管 Widget(2)
p.reset();               // 释放 Widget(2)，p 变为 nullptr
```

## Embedded in Practice: Hardware Handle Management

In embedded development, `unique_ptr` paired with a custom deleter can elegantly manage hardware resources. For example, managing a DMA buffer allocated via the HAL:

```cpp
struct DmaBuffer {
    void* data;
    size_t size;
};

struct DmaDeleter {
    void operator()(DmaBuffer* buf) noexcept {
        if (buf) {
            hal_dma_free(buf->data);  // 释放 DMA 缓冲区
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

The benefit of this approach is that any return path—whether it's a normal return, an error return, or an exception—will correctly release the DMA buffer. In complex driver code, this kind of automatic management can significantly reduce the bug rate.

## Summary

`unique_ptr` is the tool of choice for expressing exclusive ownership in modern C++. Its core design—non-copyable, movable, and RAII-managed lifetime—precisely maps to the semantic of "one object, one owner." Through EBO (Empty Base Optimization), a `unique_ptr` with the default deleter is exactly identical to a raw pointer in both memory and runtime overhead, making it a true zero-overhead abstraction.

Today we covered the core usages of `unique_ptr`: the exception safety of `make_unique`, move semantics and container compatibility, the array version, custom deleter basics, the PIMPL idiom, and the factory function pattern. These are the most frequently encountered scenarios in daily engineering.

In the next article, we will turn to `shared_ptr`—a completely different ownership model: shared ownership. Are you ready? The real complexity is just beginning.

## Reference Resources

- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
- [Empty Base Optimization and unique_ptr](https://www.cppstories.com/2021/no-unique-address/)
- Herb Sutter, *GotW #89: Smart Pointers*
