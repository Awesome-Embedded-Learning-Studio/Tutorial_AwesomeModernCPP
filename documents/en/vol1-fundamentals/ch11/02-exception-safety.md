---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the four levels of exception safety, and master the RAII guard pattern for making sure resources are properly released when an exception occurs
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
  source: documents/vol1-fundamentals/ch11/02-exception-safety.md
  source_hash: 430cc3404666248e61d069658562ee423da06ec84f7c3604125e9c31f60fff74
  translated_at: '2026-09-27T04:03:59+00:00'
  engine: anthropic
  token_count: 3100
---
# Exception Safety: When an Exception Flies Through, the Program Must Not Fall Apart

Throwing an exception is easy—one line of `throw std::runtime_error("oops")` is all it takes. The real headache is another question entirely: while the exception flies through, who cleans up the files already opened, the memory already allocated, the mutexes already locked? If nobody does, the mild outcome is a memory leak; the severe one is program state corrupted beyond repair. That is what exception safety is about: not "how do we throw exceptions", but "once an exception has happened, is the program's state still presentable".

Let's pin down one big premise first: exception safety is not a binary "safe or unsafe" — it is a **spectrum of four levels**, from worst to best. Only by understanding these four levels can we consciously choose the level of safety we want when designing functions and classes, and know what it costs to get there.

## The Four Levels of Exception Safety

### No Guarantee

This is the worst case: if an exception occurs, objects may be left in an inconsistent state, resources may leak, and the program's behavior becomes completely unpredictable. It sounds like nobody would ever write such code on purpose, but the moment we use raw `new`/`delete` without any RAII wrapper, we are already at this level:

```cpp
void no_guarantee() {
    int* data = new int[100];
    fill_data(data, 100);     // if this throws...
    process_data(data, 100);  // ...or this one...
    delete[] data;            // this line never runs: memory leak
}
```

Watch this code on the normal path and it works fine: `data` is allocated, used, and then freed. But the moment `fill_data` or `process_data` throws, execution jumps straight to the nearest `catch` block and `delete[] data` never executes. Worse, if `no_guarantee` itself has no `catch`, the caller has no way of even knowing that a resource leaked—the exception propagates away silently, leaving behind nothing but an orphaned block of heap memory.

### Basic Guarantee

The basic guarantee makes two promises at once: no resources leak, and the object is still left in a **valid** state—we can destroy it, assign it a new value, and the program will not crash. What that state actually contains, though, is **unspecified**: we cannot assume the data is what it was before the call; all we know is that it is in some "reasonable, usable" state.

Every standard library container provides at least the basic guarantee. For example, if `std::vector::push_back` throws `std::bad_alloc` during reallocation because memory ran out, the vector itself is still in a valid state and we can keep operating on it; but whether the previously inserted elements are still there, and what the capacity has become, are both left uncertain.

The core tool for implementing the basic guarantee is RAII: if every resource (memory, file handles, locks) is managed by an RAII object, then when an exception occurs, stack unwinding automatically calls the destructors of all local objects, and the resources are guaranteed to be released properly. We will come back to this point in detail shortly.

### Strong Guarantee

The strong guarantee is stricter than the basic one: an operation either **succeeds completely** or **rolls back completely**—if an exception is thrown, the object's state is exactly what it was before the call, as if the operation had never run. This is the so-called "transaction semantics".

The canonical implementation is the **copy-and-swap idiom**: make the modifications on a copy first, and only if they complete without an exception, swap the copy with the original object. Because the swap operation (`std::swap`) itself promises not to throw, the whole operation either succeeds or leaves the original object completely untouched. We will illustrate this idea with a short example later on.

### Nothrow Guarantee

This is the highest level: the function promises to **never** throw an exception. Since C++11, the `noexcept` keyword marks such functions. Destructors are `noexcept` by default—a very important design decision, because destructors are guaranteed to be called during stack unwinding, and if a destructor itself throws, the program goes straight to `std::terminate`.

Some simple operations are naturally nothrow: assignments of built-in types, copies of pointers, and `std::swap`'s specializations for built-in types and most standard containers. When designing a class, making the destructor, the `swap` function, and the move assignment operator `noexcept` does callers a great favor—many standard library operations (such as `std::vector::push_back`) pick a more efficient implementation path depending on whether the element type is `noexcept`.

## RAII and Exception Safety

Now let's circle back to why RAII is the **core mechanism** behind the basic guarantee. The principle is actually simple: C++'s exception handling mechanism guarantees that during stack unwinding, the destructors of all local objects get called. So as long as we put resource acquisition in the constructor and release in the destructor, resources are guaranteed to be cleaned up properly when an exception strikes—without writing any extra `try-catch`.

Let's look at a before-and-after comparison. First, the "dangerous" version:

```cpp
// Dangerous: raw pointers + exceptions = leaks
void unsafe_process() {
    int* buffer = new int[1024];
    double* temp  = new double[512];

    do_work(buffer, temp);  // what if this throws?

    delete[] temp;
    delete[] buffer;
}
```

If `do_work` throws an exception, both `buffer` and `temp` leak. You might think of wrapping things in a `try-catch`, but what if there are three or four resources? The code quickly bloats into spaghetti. Now the RAII rewrite:

```cpp
// Safe: RAII guards clean up automatically when an exception flies by
void safe_process() {
    auto buffer = std::make_unique<int[]>(1024);
    auto temp   = std::make_unique<double[]>(512);

    do_work(buffer.get(), temp.get());

    // Whether or not do_work throws, buffer and temp get
    // automatically released when the scope ends
}
```

`std::unique_ptr`'s destructor calls `delete[]`, and stack unwinding guarantees that the destructor executes. No `try-catch` anywhere, no manual cleanup logic—that is RAII's power. In fact, RAII's core idea condenses into a single sentence: **a resource's lifetime should be bound to the lifetime of some object**. Get that right, and exception safety falls out as a natural by-product.

RAII's premise is that "all resources are managed by RAII objects". If we mix RAII and raw handles inside a function (say, `std::unique_ptr` manages one block of memory while a file handle from `fopen` sits around raw), that file handle still leaks on an exception. **Go RAII all the way, or don't bother—no half measures**. For file handles, the standard library offers no ready-made RAII wrapper (C++ has no `std::file_ptr`), but we can write a simple guard class ourselves—and a later exercise will have you do exactly that.

## lock_guard: A Concrete RAII Guard

`std::lock_guard<std::mutex>` is RAII's most iconic real-world landing in concurrent programming. Its implementation principle is admirably concise: the constructor calls `mutex.lock()`, the destructor calls `mutex.unlock()`. That is all there is to it.

```cpp
#include <mutex>

std::mutex g_mutex;
int g_counter = 0;

void increment_unsafe() {
    g_mutex.lock();
    ++g_counter;
    // if do_something() throws...
    do_something();
    // ...this unlock never executes
    g_mutex.unlock();
    // Result: the mutex stays locked forever, and every subsequent thread deadlocks
}
```

If `do_something()` throws an exception, `unlock()` never executes and the mutex stays locked forever—every thread that subsequently tries to acquire it gets blocked permanently. This is the classic deadlock scenario. Rewritten with `lock_guard`:

```cpp
#include <mutex>

void increment_safe() {
    std::lock_guard<std::mutex> lock(g_mutex);  // lock() at construction
    ++g_counter;
    do_something();  // even if this throws...
    // unlock() in the destructor — runs no matter what
}
```

Whether or not `do_something()` throws, and whichever `return` statement the function exits from, `lock_guard`'s destructor gets called and the mutex is guaranteed to be released. That is why we say RAII guards turn "correct resource management" from "the programmer must not forget" into "the language mechanism guarantees it"—the former relies on human memory, the latter on the compiler's behavioral contract, and clearly the latter is the more dependable of the two.

A `lock_guard`'s lifetime runs from its declaration to the end of its enclosing scope. If we lock the mutex at the top of the function and release it only at the bottom, the lock may be held far longer than actually needed, which becomes a serious performance bottleneck in multithreaded programs. When only a small stretch of operations needs protection, a pair of braces can carve out a sub-scope to control the `lock_guard`'s lifetime precisely. A more flexible option is `std::unique_lock`, which lets us call `lock()` and `unlock()` manually while still guaranteeing release at destruction—but the price of that flexibility is a heavier object and slightly higher runtime overhead.

## copy-and-swap: The Path to the Strong Guarantee

The basic guarantee tells us "no leaks, valid state", but sometimes we need a stronger promise—"either it succeeds, or nothing ever happened". That is the strong guarantee, and the most common technique for achieving it is copy-and-swap.

The idea goes like this: instead of modifying the original object directly, we first make a copy and carry out the modifications on the copy. If something goes wrong during the modification (an exception is thrown), the original is completely unaffected, because only the copy was ever touched. If the modification finishes cleanly, we swap the modified copy with the original—the swap operation itself is `noexcept` and cannot fail.

```cpp
class ConfigManager {
private:
    std::vector<std::string> entries_;

public:
    // Strong exception guarantee: either everything is updated, or nothing changes
    void update_entries(const std::vector<std::string>& new_entries) {
        std::vector<std::string> temp = new_entries;  // the copy; may throw

        // Run the validation and modification steps on temp
        validate_and_normalize(temp);  // may throw

        // Getting this far means all went well: swap — noexcept, cannot fail
        using std::swap;
        swap(entries_, temp);
    }  // temp (the old entries_) is destroyed automatically at end of scope
};
```

If `validate_and_normalize` throws, `entries_` was never touched at all; if everything goes smoothly, `swap` moves the new data in, hands the old data to `temp`, and `temp` cleans it up automatically at destruction. The whole process needs no `try-catch` whatsoever.

copy-and-swap is an idiom well worth mastering, though in resource-constrained embedded settings the memory cost of a full copy may be unacceptable. Here we are only building the concept; Volume 2, which covers RAII and resource management in depth, will devote dedicated discussion to its variants and trade-offs.

## In Practice: Comparing Exception Safety

Now let's string the previous pieces together and write a complete side-by-side comparison—the same functionality implemented once with raw pointers (unsafe) and once with RAII (safe), to see how differently they behave when an exception hits.

```cpp
// safety.cpp
// Demonstrates how exception-safe and unsafe code behave differently

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

    might_throw(true);  // deliberately trigger the exception

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

    might_throw(true);  // trigger the exception here as well

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

The complete code is right below—hit "Try It" to run it directly, no terminal needed:

<OnlineCompilerDemo
  title="In Practice: Comparing Exception Safety safety.cpp"
  source-path="code/examples/vol1/21_exception_safety.cpp"
  description="Run the comparison online: the same exception makes the unsafe version leak two blocks of memory, while the RAII version comes out spotless."
  run-options="-O2 -std=c++17"
  allow-run
/>

Watch the two versions: their execution paths are nearly identical—both trigger the exception after the resources are allocated and before they are released. The difference is that the unsafe version's two blocks of memory (`data` and `temp`) are never freed, while the safe version's `std::unique_ptr` calls `delete[]` automatically during stack unwinding—zero leaks. That is the tangible difference RAII makes—and the code is even shorter than the raw-pointer version, because there is no hand-written `delete`.

In real projects, memory leaks are not this "quiet": they can slowly eat away at the available memory over a long run until the system finally crashes—and the crash site often has nothing to do with the leak site. Valgrind and AddressSanitizer are the go-to tools for catching this class of problems; compiling with `-fsanitize=address` enables ASan, which reports a leak the moment it happens, far more efficient than post-mortem sleuthing. Perhaps the author will give these handy little tools a proper introduction later on!

## Exercises

### Exercise 1: Rewriting Unsafe Code

The code below has multiple exception safety problems. Try to find them all and rewrite it into an exception-safe version:

```cpp
void process_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    char* buffer = new char[4096];

    read_and_process(f, buffer);  // may throw

    delete[] buffer;
    std::fclose(f);
}
```

Hint: think it through—if `read_and_process` throws, which resources leak? Rewrite it the RAII way; the `FILE*` can be managed by a custom guard class.

### Exercise 2: Implementing ScopedFile

Write a `ScopedFile` class yourself—the constructor takes a file path and a mode and calls `std::fopen`; the destructor calls `std::fclose`. Requirements: copying must be disabled (because a copy would let the same `FILE*` be `fclose`d twice), but move semantics should be supported. Reference interface:

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
