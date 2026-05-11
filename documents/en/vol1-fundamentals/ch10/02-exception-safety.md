# Exception Safety

Throwing an exception is easy — `throw std::runtime_error("oops")` a single line is all it takes. But the real headache is this: when an exception flies by, who cleans up the files that were opened, the memory that was allocated, the mutexes that were locked? If no one handles this, the best-case scenario is a memory leak, and the worst-case scenario is completely corrupted program state. Exception safety is about exactly this — not "how to throw exceptions," but "can the program's state still be trusted after an exception occurs?"

Let's establish a major premise first: exception safety is not a binary "safe or unsafe." Instead, it consists of **four levels** ranging from worst to best. Understanding these four levels allows us to consciously choose the safety level we want to achieve when designing functions and classes, and to know what trade-offs that requires.

## The Four Levels of Exception Safety

### No Guarantee

This is the worst-case scenario — if an exception occurs, an object might be left in an inconsistent state, resources might leak, and the program's behavior becomes completely unpredictable. It sounds like no one would intentionally write this kind of code, but in reality, as long as you are using raw `new`/`delete` without any RAII wrappers, you are already at this level:

```cpp
void no_guarantee() {
    int* data = new int[100];
    fill_data(data, 100);     // 如果这里抛异常...
    process_data(data, 100);  // ...或者这里...
    delete[] data;            // 这行永远不会执行，内存泄漏
}
```

This code works perfectly fine on the normal execution path — `data` is allocated, used, and then freed. But once `fill_data` or `process_data` throws an exception, the program flow jumps directly to the nearest `catch` block, and `delete[] data` never executes. What's worse, if `no_guarantee` itself doesn't have a `catch`, the caller won't even know a resource leaked — the exception is silently propagated, leaving behind a chunk of unmanaged heap memory.

### Basic Guarantee

The basic guarantee promises two things: first, no resources will leak; second, the object remains in a **valid** state — you can call its destructor, assign new values to it, and the program won't crash. However, the exact contents of this state are **indeterminate** — you cannot assume the data is the same as before the call; you only know it is in a "reasonable, usable" state.

All standard library containers provide at least the basic guarantee. For example, if `std::vector::push_back` throws a `std::bad_alloc` during reallocation due to insufficient memory, the vector itself remains in a valid state — you can continue operating on it — but whether the previously inserted elements are still there or what the capacity has become is uncertain.

The core mechanism for achieving the basic guarantee is RAII: if all resources (memory, file handles, locks) are managed by RAII objects, then when an exception occurs, stack unwinding automatically calls the destructors of all local objects, and resources are guaranteed to be correctly released. We will elaborate on this later.

### Strong Guarantee

The strong guarantee is stricter than the basic guarantee: an operation either **succeeds completely** or **rolls back completely** — if an exception occurs, the object's state is exactly the same as before the call, as if the operation never happened. This is known as "transactional semantics."

The typical implementation is the **copy-and-swap idiom**: first make modifications on a copy, and if no exceptions occur during the modification, swap the copy with the original object. Because the swap operation (`std::swap`) itself promises not to throw, the entire operation either succeeds or leaves the original object completely unchanged. We will use a brief example later to demonstrate this idea.

### Nothrow Guarantee

This is the highest level: the function promises it will **never** throw an exception. In C++11 and later, we use the `noexcept` keyword to mark such functions. Destructors are implicitly `noexcept` — this is a crucial design decision, because destructors are guaranteed to be called during stack unwinding, and if a destructor itself throws an exception, the program will directly call `std::terminate` and terminate.

Some simple operations are naturally non-throwing: assignment of built-in types, copying of pointers, and the `std::swap` specializations for built-in types and most standard containers. When designing a class, if the destructor, `swap` function, and move assignment operator can be made `noexcept`, it provides great convenience to the caller — many standard library operations (like `std::vector::push_back`) will select more efficient implementation paths based on whether the element type is `noexcept`.

## RAII and Exception Safety

Now let's look back at why RAII is the **core mechanism** for achieving the basic guarantee. The principle is actually quite simple: C++'s exception handling mechanism guarantees that during stack unwinding, the destructors of all local objects will be called. So as long as we put resource acquisition in the constructor and release in the destructor, resources will be correctly cleaned up when an exception occurs — without writing any extra `try-catch`.

Let's look at a before-and-after comparison. First, the "dangerous" version:

```cpp
// 危险：裸指针 + 异常 = 泄漏
void unsafe_process() {
    int* buffer = new int[1024];
    double* temp  = new double[512];

    do_work(buffer, temp);  // 如果这里抛异常呢？

    delete[] temp;
    delete[] buffer;
}
```

If `do_work` throws an exception, both `buffer` and `temp` leak entirely. You might think about wrapping it with a `try-catch`, but what if there are three or four resources? The code will rapidly bloat into spaghetti. Now let's refactor with RAII:

```cpp
// 安全：RAII 守卫，异常发生时自动清理
void safe_process() {
    auto buffer = std::make_unique<int[]>(1024);
    auto temp   = std::make_unique<double[]>(512);

    do_work(buffer.get(), temp.get());

    // 不管 do_work 是否抛异常，buffer 和 temp 都会在
    // 离开作用域时被自动释放
}
```

The destructor of `std::unique_ptr` calls `delete[]`, and stack unwinding guarantees that the destructor will definitely execute. No `try-catch` is needed, and no manual cleanup logic is required — this is the power of RAII. In fact, the core idea of RAII can be distilled into a single sentence: **the lifetime of a resource should be bound to the lifetime of an object**. As long as this is achieved, exception safety becomes a natural byproduct.

> **Pitfall Warning**: The premise of RAII is that "all resources are managed by RAII objects." If you mix RAII and raw pointers in a function — for example, using `std::unique_ptr` to manage a block of memory while also leaving a raw file handle floating around via `fopen` — that file handle will still leak when an exception occurs. **If you use RAII, go all the way — no half measures**. For file handles, the standard library doesn't provide a direct RAII wrapper (C++ doesn't have `std::file_ptr`), but we can write a simple guard class ourselves — the exercise later will have you do this hands-on.

## lock_guard: A Concrete RAII Guard

`std::lock_guard<std::mutex>` is the most classic application of RAII in concurrent programming. Its implementation is elegantly simple: the constructor calls `mutex.lock()`, and the destructor calls `mutex.unlock()`. That's it.

```cpp
#include <mutex>

std::mutex g_mutex;
int g_counter = 0;

void increment_unsafe() {
    g_mutex.lock();
    ++g_counter;
    // 如果 do_something() 抛异常...
    do_something();
    // ...这行 unlock 永远不会执行
    g_mutex.unlock();
    // 结果：互斥量永远被锁住，所有后续线程死锁
}
```

If `do_something()` throws an exception, `unlock()` will not execute, and the mutex will remain locked forever — all threads attempting to acquire this mutex will be permanently blocked. This is the classic dead lock scenario. After refactoring with `lock_guard`:

```cpp
#include <mutex>

void increment_safe() {
    std::lock_guard<std::mutex> lock(g_mutex);  // 构造时 lock()
    ++g_counter;
    do_something();  // 即使抛异常...
    // 析构时 unlock()，无论如何都会执行
}
```

Regardless of whether `do_something()` throws an exception, and regardless of which `return` statement the function exits from, the destructor of `lock_guard` will be called, and the mutex is guaranteed to be released. This is why we say RAII guards transform "the correctness of resource management" from "don't forget it, programmer" into "guaranteed by the language mechanism" — the former relies on human memory, while the latter relies on the compiler's behavioral specification. The latter is obviously far more reliable.

> **Pitfall Warning**: The lifetime of `lock_guard` is from its declaration to the end of its enclosing scope. If you lock the mutex at the very beginning of a function and only release it at the end, the lock hold time might far exceed what is actually needed — this becomes a serious performance bottleneck in multithreaded programs. If you only need to protect a small section of code, you can use a pair of curly braces to create a sub-scope for precise control over the lifetime of `lock_guard`. A more flexible option is `std::unique_lock`, which allows you to manually `lock()` and `unlock()` while still guaranteeing release upon destruction — but the cost of this flexibility is a heavier object and slightly higher runtime overhead.

## copy-and-swap: The Path to the Strong Guarantee

The basic guarantee tells us "no leaks, valid state," but sometimes we need a stronger promise — "either it succeeds, or nothing happened at all." This is the strong guarantee, and the most common technique to achieve it is copy-and-swap.

The idea is this: instead of modifying the original object directly, we first make a copy and perform modifications on the copy. If something goes wrong during modification (an exception is thrown), the original object is completely unaffected — because we only modified the copy. If the modification completes smoothly, we swap the modified copy with the original object — the swap operation itself is `noexcept` and cannot fail.

```cpp
class ConfigManager {
private:
    std::vector<std::string> entries_;

public:
    // 强异常保证：要么全部更新，要么完全不变
    void update_entries(const std::vector<std::string>& new_entries) {
        std::vector<std::string> temp = new_entries;  // 拷贝，可能抛异常

        // 在 temp 上做各种校验和修改
        validate_and_normalize(temp);  // 可能抛异常

        // 到这里说明一切正常，交换——noexcept，不会失败
        using std::swap;
        swap(entries_, temp);
    }  // temp（原来的 entries_）在作用域结束时自动销毁
};
```

If an exception is thrown in `validate_and_normalize`, the contents of `entries_` remain completely untouched; if everything goes smoothly, `swap` puts the new data in, hands the old data to `temp`, and then `temp` automatically cleans up during its destruction. The entire process doesn't require any `try-catch`.

copy-and-swap is an idiom well worth mastering, but in resource-constrained embedded scenarios, the memory overhead of making a complete copy might be unacceptable. We are just establishing the concept here; later in Volume 2, when we dive deep into RAII and resource management, we will specifically discuss its various variants and trade-offs.

## Hands-on: Exception Safety Comparison

Now let's tie together what we've learned and write a complete comparison — the same functionality implemented once with raw pointers (unsafe) and once with RAII (safe), so we can observe the behavioral differences when an exception occurs.

```cpp
// safety.cpp
// 演示异常安全与不安全代码的行为对比

#include <cstdio>
#include <memory>
#include <stdexcept>

void might_throw(bool should_fail) {
    if (should_fail) {
        throw std::runtime_error("Something went wrong!");
    }
    std::puts("  Operation succeeded.");
}

// ---- 不安全版本 ----
void unsafe_version() {
    std::puts("[Unsafe] Allocating resources...");
    int* data = new int[100];
    double* temp = new double[50];
    std::puts("[Unsafe] Resources allocated. Starting work...");

    might_throw(true);  // 故意触发异常

    delete[] temp;
    delete[] data;
    std::puts("[Unsafe] Resources released.");
}

// ---- 安全版本 ----
void safe_version() {
    std::puts("[Safe] Allocating resources...");
    auto data = std::make_unique<int[]>(100);
    auto temp = std::make_unique<double[]>(50);
    std::puts("[Safe] Resources allocated. Starting work...");

    might_throw(true);  // 同样触发异常

    std::puts("[Safe] Resources released.");
}

int main() {
    // 测试不安全版本
    std::puts("=== Testing unsafe version ===");
    try {
        unsafe_version();
    } catch (const std::exception& e) {
        std::printf("  Caught: %s\n", e.what());
    }
    std::puts("  Note: memory leaked! data and temp were never freed.\n");

    // 测试安全版本
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

The execution paths of both versions are almost identical — both trigger an exception after resource allocation but before release. The difference is that in the unsafe version, the two blocks of memory (`data` and `temp`) will never be freed, whereas in the safe version, `std::unique_ptr` automatically calls `delete[]` during stack unwinding, resulting in zero leaks. This is the tangible difference RAII makes — the code is even shorter than the raw pointer version because there's no need to manually write `delete`.

> **Pitfall Warning**: In real projects, memory leaks won't be as "quiet" as in this example — they might slowly eat away at available memory over long periods of runtime, eventually causing system crashes, and the crash location is often completely unrelated to the leak location. Valgrind and AddressSanitizer are powerful tools for detecting such issues. Adding `-fsanitize=address` at compile time enables ASan, which reports leaks the moment they occur — far more efficient than post-mortem debugging. Perhaps in the future, the author will properly introduce these handy little tools!

## Exercises

### Exercise 1: Refactor Unsafe Code

The following code has multiple exception safety issues. Try to find all the problems and refactor it into an exception-safe version:

```cpp
void process_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    char* buffer = new char[4096];

    read_and_process(f, buffer);  // 可能抛异常

    delete[] buffer;
    std::fclose(f);
}
```

Hint: Think about it — if `read_and_process` throws an exception, which resources will leak? Rewrite using RAII principles; `FILE*` can be managed using a custom guard class.

### Exercise 2: Implement ScopedFile

Write a `ScopedFile` class yourself — the constructor accepts a file path and mode, and calls `std::fopen`; the destructor calls `std::fclose`. Disable copying (because copying would cause the same `FILE*` to be `fclose` twice), but support move semantics. Reference interface:

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

## Summary

In this chapter, we focused on the topic of exception safety. The four levels of exception safety form a ladder from weak to strong: no guarantee (nothing is managed), basic guarantee (no leaks, valid state), strong guarantee (either succeed or roll back), and nothrow guarantee (never throws an exception). Among these four levels, RAII is the core mechanism for achieving the basic guarantee — as long as the lifetime of all resources is bound to objects, stack unwinding will handle all the cleanup for you. `std::lock_guard` is the classic application of RAII in concurrent scenarios, while the copy-and-swap idiom provides a path to the strong guarantee.

A practical design principle is: **aim for the basic guarantee by default, pursue the strong guarantee for critical operations, and make destructors and move operations non-throwing**. There's no need to pursue the highest level for every line of code — that's neither realistic nor necessary — but make sure your code at least doesn't leave behind a trail of wreckage when an exception flies by.

In the next chapter, we will step outside the exception framework and compare the main error handling mechanisms in C++ from a higher perspective: exceptions, return values/error codes, `std::optional`, and `std::expected`, to see which scenarios each is suited for and how to choose between them in real projects.
