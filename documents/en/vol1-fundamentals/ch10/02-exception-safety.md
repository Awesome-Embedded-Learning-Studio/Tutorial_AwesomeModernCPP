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
- Exception Basics
reading_time_minutes: 14
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Exception Safety
translation:
  source: documents/vol1-fundamentals/ch10/02-exception-safety.md
  source_hash: 6bc34bb8908484592a4ff271db51b2a45c1b833f8bccc6e91e294e1ef0a1cb08
  translated_at: '2026-09-25T11:50:25+00:00'
  engine: anthropic
  token_count: 3800
---
# Exception Safety: An Exception May Fly By, but the Program Must Not Fall Apart

Throwing an exception is easy—one line of `throw std::runtime_error("oops")` does it. The real headache is a different question: when an exception flies past, who cleans up the files already opened, the memory already allocated, the mutexes already locked...? If nobody does, you get a memory leak in the mild case and completely corrupted program state in the bad one. That is what exception safety is about: not "how to throw exceptions," but "after an exception happens, is the program's state still presentable?"

Let's pin down one big premise first: exception safety is not a binary "safe or unsafe" choice, but a spectrum of **four levels**, from worse to better. Once we understand these four levels, we can deliberately pick the safety level we want to achieve when designing functions and classes—and know what it costs to get there.

## The Four Levels of Exception Safety

### No Guarantee

This is the worst case: if an exception occurs, the object may be left in an inconsistent state, resources may leak, and the program's behavior becomes completely unpredictable. It sounds like nobody would write this kind of code on purpose, but the moment we use raw `new`/`delete` without any RAII wrapper, we are already at this level:

```cpp
void no_guarantee() {
    int* data = new int[100];
    fill_data(data, 100);     // If this throws...
    process_data(data, 100);  // ...or this...
    delete[] data;            // Never executed — memory leak
}
```

Look at this code: on the normal path it works just fine—`data` is allocated, used, and then freed. But the moment `fill_data` or `process_data` throws, program flow jumps straight to the nearest `catch` block, and `delete[] data` never executes. Worse, if `no_guarantee` itself has no `catch`, the caller doesn't even know a resource leaked—the exception propagates silently, and all that's left behind is a chunk of heap memory nobody manages.

### Basic Guarantee

The basic guarantee makes two promises at once: no resources leak; and the object remains in a **valid** state—we can call its destructor, assign it a new value, and the program won't crash. But the actual content of that state is **unspecified**: we can't assume the data still holds the pre-call values; all we know is that it is in some "reasonable, usable" state.

All standard library containers provide at least the basic guarantee. For example, if `std::vector::push_back` throws `std::bad_alloc` during reallocation because memory ran out, the vector itself is still in a valid state and we can keep operating on it; but whether the previously inserted elements are still there, or what the capacity has become—none of that is guaranteed.

The core tool for implementing the basic guarantee is RAII: if all resources (memory, file handles, locks) are managed by RAII objects, then when an exception occurs, stack unwinding automatically calls the destructors of all local objects, and the resources are guaranteed to be released correctly. We'll expand on this in detail shortly.

### Strong Guarantee

The strong guarantee is stricter than the basic one: an operation either **succeeds completely** or **rolls back completely**—if an exception occurs, the object's state is exactly what it was before the call, as if the operation had never run. This is the so-called "transaction semantics."

The classic implementation is the **copy-and-swap idiom**: first apply the modifications to a copy, and if nothing throws along the way, swap the copy with the original object. Since the swap operation (`std::swap`) itself promises not to throw, the whole operation either succeeds or leaves the original object completely unchanged. We'll show this idea with a short example later.

### Nothrow Guarantee

This is the highest level: the function promises to **never** throw an exception. Since C++11, the `noexcept` keyword marks such functions. Destructors are `noexcept` by default—a very important design decision, because destructors are guaranteed to be called during stack unwinding, and if a destructor itself throws, the program goes straight to `std::terminate` and dies.

Some simple operations are naturally nothrow: assignment of built-in types, copying pointers, and `std::swap`'s specializations for built-in types and most standard containers. When designing classes, making the destructor, the `swap` function, and the move assignment operator `noexcept` is a great favor to callers—many standard library operations (such as `std::vector::push_back`) pick a more efficient implementation path depending on whether the element type is `noexcept`.

## RAII and Exception Safety

Now let's come back to why RAII is the **core mechanism** for implementing the basic guarantee. The principle is actually simple: C++'s exception handling mechanism guarantees that during stack unwinding, the destructors of all local objects get called. So as long as we put resource acquisition in the constructor and release in the destructor, resources are guaranteed to be cleaned up correctly when an exception happens—without writing any extra `try-catch`.

Let's look at a before-and-after comparison. First, the "dangerous" version:

```cpp
// Dangerous: raw pointers + exceptions = leaks
void unsafe_process() {
    int* buffer = new int[1024];
    double* temp  = new double[512];

    do_work(buffer, temp);  // What if this throws?

    delete[] temp;
    delete[] buffer;
}
```

If `do_work` throws, both `buffer` and `temp` leak. You might be tempted to wrap things in a `try-catch`, but what if there are three or four resources? The code quickly bloats into spaghetti. Now let's redo it with RAII:

```cpp
// Safe: RAII guards clean up automatically when an exception occurs
void safe_process() {
    auto buffer = std::make_unique<int[]>(1024);
    auto temp   = std::make_unique<double[]>(512);

    do_work(buffer.get(), temp.get());

    // Whether or not do_work throws, buffer and temp are
    // released automatically when the scope ends
}
```

Notice that `std::unique_ptr`'s destructor calls `delete[]`, and stack unwinding guarantees the destructor runs. No `try-catch`, no manual cleanup logic—that's the power of RAII. In fact, the core idea of RAII boils down to one sentence: **a resource's lifetime should be bound to the lifetime of some object**. Get that right, and exception safety falls out as a natural by-product.

The premise of RAII is "every resource is managed by an RAII object." If we mix RAII and raw pointers inside a function (say, `std::unique_ptr` manages one block of memory, but we also `fopen` a file handle and leave it lying around raw), that file handle still leaks on an exception. **If you're going to use RAII, go all the way—no half measures.** For file handles, the standard library has no ready-made RAII wrapper (C++ has no `std::file_ptr`), but we can write a simple guard class ourselves—and the exercise at the end will have you do exactly that.

## lock_guard: A Concrete RAII Guard

`std::lock_guard<std::mutex>` is RAII's most classic real-world case in concurrent programming. Its implementation principle is admirably simple: the constructor calls `mutex.lock()`, and the destructor calls `mutex.unlock()`. That's all there is to it.

```cpp
#include <mutex>

std::mutex g_mutex;
int g_counter = 0;

void increment_unsafe() {
    g_mutex.lock();
    ++g_counter;
    // If do_something() throws...
    do_something();
    // ...this unlock never runs
    g_mutex.unlock();
    // Result: the mutex stays locked forever; every later thread deadlocks
}
```

If `do_something()` throws, `unlock()` never runs and the mutex stays locked forever—every thread that tries to acquire it blocks permanently. This is the classic deadlock scenario. After the `lock_guard` makeover:

```cpp
#include <mutex>

void increment_safe() {
    std::lock_guard<std::mutex> lock(g_mutex);  // lock() on construction
    ++g_counter;
    do_something();  // Even if this throws...
    // unlock() in the destructor — it runs no matter what
}
```

Whether or not `do_something()` throws, and whichever `return` statement the function exits from, the `lock_guard` destructor gets called and the mutex is released. This is why we say RAII guards turn "correct resource management" from "the programmer must not forget" into "the language mechanism guarantees it"—the former relies on human memory, the latter on the compiler's behavior rules, and the latter is clearly far more dependable.

A `lock_guard` lives from its declaration to the end of its enclosing scope. If we lock the mutex at the top of a function and only release it at the end, we may be holding the lock far longer than actually needed—which becomes a serious performance bottleneck in multithreaded programs. If only a small stretch of operations needs protection, a pair of braces can create a sub-scope to control the `lock_guard`'s lifetime precisely. A more flexible option is `std::unique_lock`, which lets us call `lock()` and `unlock()` manually while still guaranteeing release at destruction—the price of that flexibility being a heavier object and slightly more runtime overhead.

## copy-and-swap: The Path to the Strong Guarantee

The basic guarantee tells us "no leaks, valid state," but sometimes we need a stronger promise—"either it succeeds, or nothing ever happened." That's the strong guarantee, and the most common technique for achieving it is copy-and-swap.

The idea goes like this: instead of modifying the original object directly, we first make a copy and apply the modifications to the copy. If something goes wrong during modification (an exception is thrown), the original is completely untouched, because only the copy was being changed. If the modifications complete, we swap the modified copy with the original—the swap itself is `noexcept` and cannot fail.

```cpp
class ConfigManager {
private:
    std::vector<std::string> entries_;

public:
    // Strong exception guarantee: either everything updates, or nothing changes
    void update_entries(const std::vector<std::string>& new_entries) {
        std::vector<std::string> temp = new_entries;  // Copy — may throw

        // Run all the validation and modification on temp
        validate_and_normalize(temp);  // May throw

        // Getting here means all went well; swap — noexcept, cannot fail
        using std::swap;
        swap(entries_, temp);
    }  // temp (the old entries_) is destroyed automatically at end of scope
};
```

Notice that if `validate_and_normalize` throws, the contents of `entries_` were never touched; if all goes well, `swap` moves the new data in and hands the old data to `temp`, which then cleans up automatically when destroyed. The entire process needs no `try-catch` at all.

copy-and-swap is an idiom well worth mastering, though in resource-constrained embedded settings, the memory cost of a full copy may be unacceptable. Here we are just building the concept; in Volume 2, when we dig into RAII and resource management, we'll devote dedicated discussion to its variants and trade-offs.

## In Practice: Comparing Exception Safety

Now let's tie the earlier pieces together and write a complete side-by-side—the same functionality twice, once with raw pointers (unsafe) and once with RAII (safe), to see how they behave when an exception occurs.

```cpp
// safety.cpp
// Demonstrates the behavioral contrast between unsafe and exception-safe code

#include <cstdio>
#include <memory>
#include <stdexcept>

void might_throw(bool should_fail) {
    if (should_fail) {
        throw std::runtime_error("Something went wrong!");
    }
    std::puts("  Operation succeeded.");
}

// ---- Unsafe version ----
void unsafe_version() {
    std::puts("[Unsafe] Allocating resources...");
    int* data = new int[100];
    double* temp = new double[50];
    std::puts("[Unsafe] Resources allocated. Starting work...");

    might_throw(true);  // Deliberately trigger the exception

    delete[] temp;
    delete[] data;
    std::puts("[Unsafe] Resources released.");
}

// ---- Safe version ----
void safe_version() {
    std::puts("[Safe] Allocating resources...");
    auto data = std::make_unique<int[]>(100);
    auto temp = std::make_unique<double[]>(50);
    std::puts("[Safe] Resources allocated. Starting work...");

    might_throw(true);  // Trigger the exception here too

    std::puts("[Safe] Resources released.");
}

int main() {
    // Test the unsafe version
    std::puts("=== Testing unsafe version ===");
    try {
        unsafe_version();
    } catch (const std::exception& e) {
        std::printf("  Caught: %s\n", e.what());
    }
    std::puts("  Note: memory leaked! data and temp were never freed.\n");

    // Test the safe version
    std::puts("=== Testing safe version ===");
    try {
        safe_version();
    } catch (const std::exception& e) {
        std::printf("  Caught: %s\n", e.what());
    }
    std::puts("  Note: no leak! unique_ptr destructors cleaned up.\n");

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra safety.cpp -o safety && ./safety
```

Expected output:

```text
=== Testing unsafe version ===
[Unsafe] Allocating resources...
[Unsafe] Resources allocated. Starting work...
  Caught: Something went wrong!
  Note: memory leaked! data and temp were never freed.

=== Testing safe version ===
[Safe] Allocating resources...
[Safe] Resources allocated. Starting work...
  Caught: Something went wrong!
  Note: no leak! unique_ptr destructors cleaned up.
```

Notice that the two versions execute nearly identical paths: both trigger the exception after the resources are allocated and before they are released. The difference: in the unsafe version, the two blocks of memory (`data` and `temp`) are never freed, while in the safe version, `std::unique_ptr` automatically calls `delete[]` during stack unwinding—no leaks at all. That's the tangible difference RAII makes—the code is even shorter than the raw-pointer version, because there is no hand-written `delete`.

In real projects, memory leaks are not this "quiet": they can slowly gnaw away at available memory during long runs until the system finally crashes—and the crash site often has nothing to do with where the leak was. Valgrind and AddressSanitizer are the go-to tools for catching this class of problems: compiling with `-fsanitize=address` enables ASan, which reports leaks the moment they happen—far more efficient than investigating after the fact. Perhaps I'll give these handy little tools a proper introduction later!

## Exercises

### Exercise 1: Fixing Unsafe Code

The code below has several exception-safety problems. Try to find all of them and rework it into an exception-safe version:

```cpp
void process_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    char* buffer = new char[4096];

    read_and_process(f, buffer);  // May throw

    delete[] buffer;
    std::fclose(f);
}
```

Hint: think about which resources leak if `read_and_process` throws. Rewrite it with RAII thinking; the `FILE*` can be managed by a custom guard class.

### Exercise 2: Implementing ScopedFile

Write a `ScopedFile` class yourself—the constructor takes a file path and mode and calls `std::fopen`; the destructor calls `std::fclose`. Requirements: copying must be disabled (a copy would lead to the same `FILE*` being `fclose`d twice), but move semantics should be supported. Reference interface:

```cpp
class ScopedFile {
public:
    explicit ScopedFile(const char* path, const char* mode);
    ~ScopedFile();

    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    ScopedFile(ScopedFile&& other) noexcept;
    ScopedFile& operator=(ScopedFile&& other) noexcept;

    FILE* get() const noexcept;
    explicit operator bool() const noexcept;
};
```
