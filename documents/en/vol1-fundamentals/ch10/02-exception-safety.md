---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the four levels of exception safety, and master the RAII guard
  pattern to ensure resources are released correctly when exceptions occur.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 异常基础
reading_time_minutes: 14
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Exception Safety
translation:
  source: documents/vol1-fundamentals/ch10/02-exception-safety.md
  source_hash: 69db2f4d7fc26bcaa2ec29de95cdef8c75137673ece2b0888cd3f7c00996672d
  translated_at: '2026-06-16T03:46:56.732623+00:00'
  engine: anthropic
  token_count: 2330
---
# Exception Safety

Throwing an exception is easy—one line is all it takes. The real headache is this: when an exception flies by, who cleans up the files that were opened, the memory that was allocated, the mutexes that were locked...? If no one handles it, you might get a memory leak at best, or completely corrupted program state at worst. Exception safety is all about this—not "how to throw exceptions," but "whether the program state remains sane after an exception occurs."

Let's establish a major premise first: exception safety isn't a binary choice of "safe or unsafe." Instead, it consists of **four levels**, ranging from poor to excellent. Understanding these four levels allows us to consciously choose the safety level we want to achieve when designing functions and classes, and to understand the costs involved.

## The Four Levels of Exception Safety

### No Guarantee

This is the worst-case scenario—if an exception occurs, the object might be in an inconsistent state, resources might leak, and the program's behavior is completely unpredictable. It sounds like no one would intentionally write such code, but in reality, as long as you use raw `new`/`delete` without any RAII wrapper, you are already at this level:

```cpp
void riskyFunction() {
    int* data = new int[100]; // Resource acquired
    processData(data);        // Might throw
    delete[] data;            // Never reached if exception thrown
}
```

This code works well in the normal path—`data` is allocated, used, and then freed. But once `processData` throws an exception, the program flow jumps directly to the nearest `catch` block, and `delete[] data` is never executed. Even worse, if `riskyFunction` itself doesn't have a `catch`, the caller might not even know a resource leaked—the exception propagates silently, leaving behind a block of unmanaged heap memory.

### Basic Guarantee

The basic guarantee promises two things: first, no resources will leak; second, the object remains in a **valid** state—you can call its destructor, assign new values to it, and the program won't crash. However, the specific content of this state is **indeterminate**—you cannot assume the data is the same as before the call, only that it is in a "reasonable, usable" state.

All standard library containers provide at least the basic guarantee. For example, if `std::vector` throws `std::bad_alloc` during reallocation due to insufficient memory, the vector itself remains in a valid state—you can continue to operate on it—but whether previously inserted elements still exist or what the capacity has become are uncertain.

The core means of implementing the basic guarantee is RAII: if all resources (memory, file handles, locks) are managed by RAII objects, then when an exception occurs, stack unwinding will automatically call the destructors of all local objects, ensuring resources are correctly released. We will expand on this in detail shortly.

### Strong Guarantee

The strong guarantee is stricter than the basic guarantee: the operation either **succeeds completely** or **rolls back completely**—if an exception occurs, the state of the object is exactly the same as before the call, as if the operation had never been executed. This is known as "transactional semantics."

A typical implementation is the **copy-and-swap idiom**: modify a copy first; if no exception occurs during the modification, swap the copy with the original object. Since the swap operation (using `std::swap`) itself promises not to throw, the entire operation either succeeds or leaves the original object completely unchanged. Later, we will use a brief example to demonstrate this idea.

### Nothrow Guarantee

This is the highest level: the function promises **never** to throw an exception. In C++11 and later, the `noexcept` keyword is used to mark such functions. Destructors are `noexcept` by default—this is a crucial design decision because destructors are guaranteed to be called during stack unwinding. If a destructor itself throws an exception, the program will immediately call `std::terminate` to shut down.

Some simple operations are naturally non-throwing: assignment of built-in types, copying of pointers, and `std::swap` specializations for built-in types and most standard containers. When designing classes, if destructors, move constructors, and move assignment operators can be `noexcept`, it brings great convenience to the caller—many standard library operations (like `std::vector::resize`) choose more efficient implementation paths based on whether the element type is `noexcept`.

## RAII and Exception Safety

Now let's look back at why RAII is the **core mechanism** for implementing the basic guarantee. The principle is actually simple: C++'s exception handling mechanism guarantees that during stack unwinding, the destructors of all local objects will be called. As long as we put resource acquisition in the constructor and release in the destructor, resources will be correctly cleaned up when an exception occurs—without writing any extra `catch` blocks.

Let's look at a comparison before and after modification. First, the "dangerous" version:

```cpp
void dangerous() {
    int* p1 = new int;
    int* p2 = new int;
    // ... code that might throw ...
    delete p1;
    delete p2;
}
```

If the code in the middle throws an exception, both `p1` and `p2` leak. You might try wrapping it in `try-catch`, but what if there are three or four resources? The code will rapidly swell into spaghetti. Now let's refactor with RAII:

```cpp
void safe() {
    std::unique_ptr<int> p1(new int);
    std::unique_ptr<int> p2(new int);
    // ... code that might throw ...
    // No manual delete needed
}
```

The destructor of `unique_ptr` calls `delete`, and stack unwinding guarantees the destructor is executed. No `try-catch` is needed, nor any manual cleanup logic—this is the power of RAII. In fact, the core concept of RAII can be condensed into one sentence: **the lifecycle of a resource should be bound to the lifecycle of an object**. As long as this is achieved, exception safety is a natural byproduct.

> **Warning**: RAII's premise is "all resources are managed by RAII objects." If you mix RAII and raw pointers in a function—for example, using `std::unique_ptr` to manage a block of memory, but also `open`ing a file handle and leaving it raw—that file handle will still leak when an exception occurs. **Go all-in with RAII, don't do it halfway.** For file handles, the standard library lacks a direct RAII wrapper (C++ has no `std::file`), but we can write a simple guard class ourselves—the exercises later will have you do this.

## lock_guard: A Concrete RAII Guard

`std::lock_guard` is the most classic application of RAII in concurrent programming. Its implementation is elegantly simple: call `lock()` in the constructor, `unlock()` in the destructor. That's it.

```cpp
std::mutex m;
void bad_lock() {
    m.lock();
    // If this throws, unlock() is never reached
    dangerousOperation();
    m.unlock();
}
```

If `dangerousOperation` throws an exception, `m.unlock()` is not executed, and the mutex remains locked forever—any thread attempting to acquire this mutex will be permanently blocked. This is the classic deadlock scenario. Refactoring with `std::lock_guard`:

```cpp
void good_lock() {
    std::lock_guard<std::mutex> lock(m);
    dangerousOperation();
} // m.unlock() guaranteed here
```

Regardless of whether `dangerousOperation` throws an exception, or which `return` statement the function exits from, `lock_guard`'s destructor is called, and the mutex is definitely released. This is why we say RAII guards turn "resource management correctness" from "don't forget it, programmer" into "guaranteed by language mechanism"—the former relies on human memory, the latter relies on compiler behavior specifications; the latter is obviously much more reliable.

> **Warning**: The lifecycle of `std::lock_guard` is from its declaration to the end of the scope. If you lock the mutex at the very beginning of the function and only release it at the very end, the lock hold time might far exceed the actual need—this becomes a serious performance bottleneck in multi-threaded programs. If you only need to protect a small section of operation, you can use a pair of braces to create a sub-scope to precisely control the lifecycle of `std::lock_guard`. A more flexible choice is `std::unique_lock`, which allows you to manually `lock` and `unlock`, while still guaranteeing release upon destruction—but the cost of flexibility is a heavier object and slightly higher runtime overhead.

## copy-and-swap: The Path to the Strong Guarantee

The basic guarantee tells us "no leaks, valid state," but sometimes we need a stronger commitment—"either success, or nothing happened." This is the strong guarantee, and the most common technique to achieve it is copy-and-swap.

The idea is this: instead of modifying the original object directly, we first make a copy and modify the copy. If something goes wrong during the modification (an exception is thrown), the original object is completely unaffected—because only the copy was changed. If the modification completes smoothly, we swap the modified copy with the original object—the swap operation itself is `noexcept` and cannot fail.

```cpp
class Widget {
    std::vector<int> data;
public:
    void update(const std::vector<int>& newData) {
        std::vector<int> temp = newData; // Copy
        // Modify temp... might throw
        temp.push_back(42);              // Might throw

        data.swap(temp);                 // No-throw swap
    } // temp destructor cleans up old data
};
```

If an exception is thrown during the modification of `temp`, the contents of `this->data` remain completely untouched. If everything goes smoothly, `data.swap(temp)` puts the new data in, hands the old data to `temp`, and `temp`'s destructor automatically cleans it up. The whole process requires no `try-catch`.

copy-and-swap is a very worthwhile idiom to master, but in resource-constrained embedded scenarios, the memory overhead of making a full copy might be unacceptable. We are just establishing the concept here; later in Volume 2, when we dive deep into RAII and resource management, we will specifically discuss its various variants and trade-offs.

## Practice: Exception Safety Comparison

Now let's string together the previous knowledge and write a complete comparison code—the same functionality, one using raw pointers (unsafe), one using RAII (safe), to see the behavioral difference when an exception occurs.

```cpp
#include <iostream>
#include <memory>
#include <stdexcept>

// Unsafe version: raw pointers
void unsafeCode() {
    int* p1 = new int(10);
    int* p2 = new int(20);

    // Simulate an exception
    throw std::runtime_error("Something went wrong!");

    delete p1;
    delete p2;
}

// Safe version: RAII
void safeCode() {
    std::unique_ptr<int> p1(new int(10));
    std::unique_ptr<int> p2(new int(20));

    // Simulate an exception
    throw std::runtime_error("Something went wrong!");

    // No manual delete needed
}

int main() {
    std::cout << "Running unsafe version..." << std::endl;
    try {
        unsafeCode();
    } catch (const std::exception& e) {
        std::cout << "Caught: " << e.what() << std::endl;
    }

    std::cout << "\nRunning safe version..." << std::endl;
    try {
        safeCode();
    } catch (const std::exception& e) {
        std::cout << "Caught: " << e.what() << std::endl;
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++11 -o exception_safety exception_safety.cpp
./exception_safety
```

Expected output:

```text
Running unsafe version...
Caught: Something went wrong!

Running safe version...
Caught: Something went wrong!
```

The execution paths of both versions are almost identical—both trigger an exception after resource allocation and before release. The difference is that in the unsafe version, the two blocks of memory (`p1` and `p2`) are never released, while in the safe version, `unique_ptr` automatically calls `delete` during stack unwinding, resulting in zero leaks. This is the tangible difference brought by RAII—the code is even shorter than the raw pointer version because there's no manual `delete` to write.

> **Warning**: In actual projects, memory leaks won't be as "quiet" as in this example—they might slowly eat away at available memory after long runs, eventually causing system crashes, and the crash location is often unrelated to the leak location. Valgrind and AddressSanitizer are powerful tools for detecting such issues. Adding `-fsanitize=address` at compile time enables ASan, which will report immediately upon a leak, far more efficient than post-mortem debugging. Perhaps the author will introduce these handy tools properly in the future!

## Exercises

### Exercise 1: Refactor Unsafe Code

The following code has multiple exception safety issues. Try to find all problems and refactor it into an exception-safe version:

```cpp
void riskyOperation() {
    int* data = new int[100];
    FILE* f = fopen("log.txt", "w");

    // Some operations that might throw
    process(data);

    fclose(f);
    delete[] data;
}
```

Hint: Think about it—if `process` throws an exception, which resources will leak? Rewrite using RAII principles; `FILE*` can be managed by a custom guard class.

### Exercise 2: Implement ScopedFile

Write a `ScopedFile` class yourself—the constructor accepts a file path and mode, calls `fopen`; the destructor calls `fclose`. Requirement: disable copying (because copying would cause the same `FILE*` to be `fclose`'d twice), but support move semantics. Reference interface:

```cpp
class ScopedFile {
    FILE* file;
public:
    ScopedFile(const char* filename, const char* mode);
    ~ScopedFile();

    // Disable copy
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    // Enable move
    ScopedFile(ScopedFile&& other) noexcept;
    ScopedFile& operator=(ScopedFile&& other) noexcept;

    operator FILE*() { return file; } // Transparent use
};
```

## Summary

In this post, we focused on the topic of exception safety. The four levels of exception safety form a ladder from weak to strong: no guarantee (nothing is managed), basic guarantee (no leaks, valid state), strong guarantee (either success or rollback), and nothrow guarantee (never throws). Among these levels, RAII is the core mechanism for implementing the basic guarantee—as long as the lifecycle of all resources is bound to objects, stack unwinding will complete all cleanup work for you. `std::lock_guard` is the classic application of RAII in concurrent scenarios, while the copy-and-swap idiom provides the path to the strong guarantee.

A practical design principle is: **aim for the basic guarantee by default, pursue the strong guarantee for critical operations, and make destructors and move operations noexcept**. You don't need to pursue the highest level for every line of code—that's neither realistic nor necessary—but ensure your code doesn't leave a mess of fragments when an exception flies by.

In the next post, we will step out of the exception framework and compare several major error handling methods in C++ from a higher perspective: exceptions, return values/error codes, `std::optional`, and `std::expected`, to see which scenarios they fit best and how to choose them in actual projects.
