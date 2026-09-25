---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand when destructors get called, and get a first look at the RAII
  principle and the design thinking behind the Rule of Three.
difficulty: beginner
order: 3
platform: host
prerequisites:
- Constructors
reading_time_minutes: 17
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Destructors and Resource Management
translation:
  source: documents/vol1-fundamentals/ch06/03-destructors.md
  source_hash: 1c3ac65ced06d62ce9c78e45ead54562cf1d66eeb703cfc60c3898d2be12f382
  translated_at: '2026-09-25T11:04:39+00:00'
  engine: anthropic
  token_count: 2500
---
# Destructors: When the Object Leaves, It Takes Its Resources With It

Constructors are responsible for bringing an object into a valid state: allocating memory, opening files, initializing hardware. But all of these resources share one common problem: at some point, they must be given back. If we `malloc` without `free`, `fopen` without `fclose`, or lock a mutex without unlocking it, the program slowly leaks resources until it exhausts the system's quotas or falls into a deadlock.

C++'s answer to this problem is the destructor, which automates giving resources back. The constructor and the destructor form a symmetric pair: one executes automatically when the object is born, the other when it dies. This pattern of "acquire at construction, release at destruction" has a proper name—RAII—and it is the foundation of C++ resource management.

Let's take destructors apart from start to finish: the syntax, when they get called, the core idea of RAII, and a classic design guideline none of us can dodge: the Rule of Three.

## Destructor Syntax

Declaring a destructor is dead simple: put a tilde `~` in front of the class name, with no parameters and no return type. A class can have only one destructor—overloading is not supported.

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

A destructor cannot take parameters, so overloading is impossible; it has no return value either. These restrictions are easy to make sense of: the runtime invokes the destructor automatically, and the caller never needs to pass anything.

If we don't define a destructor, the compiler generates a default version that destroys the non-static members in the reverse order of their declaration. A class containing only fundamental types doesn't need a hand-written destructor, but if the class manages external resources (dynamic memory, file handles, network connections), we have to write the destructor ourselves to release them. (Which is perfectly normal—the compiler has no way of knowing how we want our own resources released.)

## When the Destructor Gets Called

Understanding the call timing is the precondition for using RAII correctly. **Stack objects** are destroyed automatically when they leave their scope, whether that is a normal return, an early `return`, or stack unwinding during an exception:

```cpp
void process() {
    FileWriter writer("log.txt", "w");
    writer.write("Processing started\n");
}   // writer is destroyed here; the file closes automatically
```

**Heap objects** are destroyed only by an explicit `delete`, and this is one of the main sources of C++ resource leaks we run into:

```cpp
void leaky() {
    FileWriter* writer = new FileWriter("log.txt", "w");
    writer->write("Oops\n");
    // forgot delete — the destructor never runs, and the file never closes
}
```

Forget to `delete` an object that came from `new`, and the destructor never executes. Even if we remember to `delete` on the normal path, an exception thrown in between skips the `delete`. Modern C++ strongly recommends smart pointers or stack objects in place of raw `new`/`delete`.

**Member objects** are destroyed after the enclosing class's destructor body finishes executing, in exactly the reverse order of construction. Let's write a small program to verify:

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

Output:

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

We can see the construction order is `local -> member_a -> member_b -> Container body`, and destruction is strictly the reverse—"whatever was constructed last is destroyed first" guarantees that resources are released in the correct layer order.

## RAII — The Core Idea of C++ Resource Management

RAII stands for Resource Acquisition Is Initialization. We can compress its core idea into one sentence: **bind the resource's lifetime to the object's lifetime**. Acquire the resource at construction, release it at destruction. Because the destructor is guaranteed to be called when the object leaves its scope (even when an exception is in flight), the resource is guaranteed to be released correctly.

Let's look at a practical example: a `ScopedTimer` that measures how long a block of code takes to execute:

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
}  // timer is destroyed here, printing the elapsed time automatically
```

We never need to remember to "stop the timer" at the end of the function—the destructor does it for us automatically. Multiple `return` paths, exceptions: on every single path the timer is destroyed correctly. That is the power of RAII: **it makes "no leaks" the default behavior, instead of a "remember to do it" chore kept alive by discipline**.

The precondition for RAII is that the object lives on the stack (or is a global/static object), not a heap object from raw `new`. If we `new` a RAII object and forget to `delete` it, the destructor still never runs—RAII cannot save us. The modern C++ advice: **keep objects on the stack whenever possible**, and if you must use the heap, use smart pointers.

## Rule of Three — A Design Warning Signal

The Rule of Three is a classic design guideline: **if our class needs a custom version of any one of the following three, it almost certainly needs custom versions of the other two at the same time**—the destructor, the copy constructor, and the copy assignment operator.

These three functions together decide how objects get copied and how they get destroyed. Writing a destructor usually means the class manages resources that need manual release, while the compiler-generated copy operations do only a shallow copy: once a pointer member is copied, both objects point at the same resource, and destruction ends in a double free.

```cpp
class NaiveBuffer {
    int* data_;
    std::size_t size_;
public:
    explicit NaiveBuffer(std::size_t n) : size_(n), data_(new int[n]()) {}
    ~NaiveBuffer() { delete[] data_; }
    // No custom copy operations — the compiler-generated version does a shallow copy
};

void bug_demo() {
    NaiveBuffer a(10);
    NaiveBuffer b = a;  // Shallow copy: b.data_ == a.data_
    // Double free at the end of the scope — undefined behavior!
}
```

One way we can fix this is to forbid copying outright:

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

Consider this a preview of the concept. Once we have covered move semantics, the Rule of Three expands into the Rule of Five. For now, you only need to remember this: **the moment you hand-write a destructor, stop and ask whether your class can be copied safely—and if it cannot, delete the copy operations**.

## Virtual Destructors — The Invisible Trap of Polymorphism

If our class will be inherited from, and users manipulate derived-class objects through a base-class pointer, then the base class's destructor must be `virtual`. Otherwise, when the base pointer is `delete`d, the derived class's destructor is skipped entirely.

```cpp
class Base {
public:
    ~Base() { std::cout << "~Base" << std::endl; }  // Not virtual!
};

class Derived : public Base {
    int* resource_;
public:
    Derived() : resource_(new int[100]) {}
    ~Derived() { delete[] resource_; std::cout << "~Derived" << std::endl; }
};

void leak_demo() {
    Base* ptr = new Derived();
    delete ptr;  // Only ~Base() runs; ~Derived() is skipped → memory leak
}
```

The output shows only `~Base`—the 400 bytes of memory pointed to by `resource_` leak without a sound. The fix is a single word: add `virtual` in front of the base class's destructor:

```cpp
class Base {
public:
    virtual ~Base() { std::cout << "~Base" << std::endl; }
};
```

The condition for this rule is that the class is actually used as a polymorphic base. The rule of thumb to stay safe: **if our class has any `virtual` functions, the destructor should be `virtual` too**. Conversely, a class with no `virtual` functions does not need a virtual destructor—adding one just saddles every object with the overhead of a vtable pointer. We will dig deeper into this topic in the next chapter, when we cover inheritance and polymorphism.

## Hands-On: Destructors in Action

Now let's write a complete program that chains `ScopedTimer` and `FileWriter` together to demonstrate RAII delivering in practice:

```cpp
// destructor.cpp
// Build: g++ -std=c++17 -o destructor destructor.cpp

#include <chrono>
#include <cstdio>
#include <iostream>

/// @brief Scoped timer
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

/// @brief A file writer that manages a FILE* automatically
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

The inner `ScopedTimer` and `FileWriter` are destroyed first; the outer `total` goes last. You can verify the file's contents:

```bash
cat raii_demo.txt
# Hello from RAII!
# No manual fclose needed.
```

The contents are correct, and we never wrote `fclose` by hand—the destructor did all the cleanup for us.

## Exercises

### Exercise 1: A Scoped Logging Timer

Write a `ScopedLogger` class that records a timestamp at construction (in `HH:MM:SS` format) and prints "elapsed X seconds" at destruction. Hint: use `std::time` and `std::localtime` from `<ctime>`.

::: details Reference answer

```cpp
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>

class ScopedLogger
{
private:
    std::time_t start_time_;

public:
    // Constructor: record the start timestamp
    ScopedLogger()
    {
        start_time_ = std::time(nullptr);

        std::tm* local_time = std::localtime(&start_time_);
        std::cout << "Start time: "
                  << std::setfill('0') << std::setw(2) << local_time->tm_hour
                  << ":" << std::setw(2) << local_time->tm_min
                  << ":" << std::setw(2) << local_time->tm_sec
                  << std::endl;
    }

    // Destructor: print the seconds elapsed from construction to destruction
    ~ScopedLogger()
    {
        std::cout << "elapsed "
                  << (std::time(nullptr) - start_time_)
                  << " seconds"
                  << std::endl;
    }

    // Disallow copying
    ScopedLogger(const ScopedLogger&) = delete;
    ScopedLogger& operator=(const ScopedLogger&) = delete;
};

int main()
{
    ScopedLogger logger;

    // Sleep 2 seconds to simulate a section of timed code
    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
Start time: 11:17:43
elapsed 2 seconds
```

> Tip: `std::setfill('0')` combined with `std::setw(2)` pads hours, minutes, and seconds with a leading `0` whenever they fall below two digits (9:05:03, for example, prints as `09:05:03`). The fill character set by `setfill` stays on the stream, so writing it once is enough; `setw`, on the other hand, only affects the single output that immediately follows it, so it has to be set again for every field. Also, `Start time` depends on the local time at runtime—your run may well print something different from what you see here.

:::

### Exercise 2: A Simple File Handle

Implement a `FileHandle` class that opens the file at construction and closes it automatically at destruction. Provide a `read_line()` method (returning `std::string`) and an `is_valid()` method. Think it through with the Rule of Three: does this class need copying disabled? Why?

::: details Reference answer

```cpp
#include <cstdio>
#include <iostream>
#include <string>

class FileHandle
{
private:
    FILE *handle;

public:
    // Constructor: open the file
    FileHandle(const char *filename)
    {
        handle = fopen(filename, "r");
    }

    // Destructor: close the file
    ~FileHandle()
    {
        if (handle != nullptr)
        {
            fclose(handle);
        }
    }

    // Read one line
    std::string read_line()
    {
        char buffer[256];

        if (fgets(buffer, sizeof(buffer), handle) != nullptr)
        {
            return std::string(buffer);
        }

        return {};
    }

    // Check whether the file opened successfully
    bool is_valid() const
    {
        return handle != nullptr;
    }

    // Disallow copying
    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;
};

int main()
{
    FileHandle file("test.txt");

    // Check whether the file opened successfully
    if (!file.is_valid())
    {
        std::cout << "open file failed" << std::endl;
        return 1;
    }

    // Read the first line
    std::cout << file.read_line();

    // Read the second line
    std::cout << file.read_line();

    return 0;
}
```

Let's prepare a `test.txt` for testing:

```text
Hello from line 1
Hello from line 2
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
Hello from line 1
Hello from line 2
```

> About the follow-up question in this exercise: this class **does need copying disabled**. `FileHandle` holds a raw `FILE*` resource, and the compiler-generated copy operations do only a shallow copy: both objects' `handle` points at the same `FILE*`, and at the end of the scope the same handle gets `fclose`d twice—undefined behavior. This is exactly the Rule of Three scenario from this chapter: once we hand-write a destructor, we must examine the copy semantics at the same time—either implement a deep copy, or forbid copying outright as done here.

:::
