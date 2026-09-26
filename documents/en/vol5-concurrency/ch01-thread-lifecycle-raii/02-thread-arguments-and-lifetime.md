---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: "Dig into how thread arguments are passed, and learn to recognize concurrency bugs caused by dangling references and object destruction order"
difficulty: intermediate
order: 2
platform: host
prerequisites:
- "std::thread Basics"
reading_time_minutes: 18
related:
- "Thread Ownership and RAII"
- "CPU Cache and OS Threads"
tags:
- host
- cpp-modern
- intermediate
- 内存管理
title: "Thread Arguments and Lifetime"
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md
  source_hash: c392a9998fd17fd404c3a002db3d83b8c7900e083cbe2d09b5d05ce8d0af094a
  translated_at: '2026-09-26T06:31:27+00:00'
  engine: anthropic
  token_count: 13000
---
# Thread Arguments and Lifetime

In the previous article we learned the basic operations of `std::thread`—creating, joining, detaching, getting the ID. At the time, we more or less deliberately skirted a very important topic: how do the arguments you pass to a thread actually reach the thread function? Why is it that sometimes you clearly pass in a reference, the thread modifies it, and the outer variable doesn't change? And why does the program sometimes crash inexplicably once you start using `std::ref`?

This article takes those questions apart completely. The argument-passing mechanism of `std::thread` hinges on one core design decision—**decay-copy**—which means all arguments are conceptually passed by value. Once you understand this mechanism, you can see the root of a whole family of concurrency bugs. Then we go deeper: dangling references, `this`-pointer capture, object destruction order, the traps of reference capture in lambdas—the essence of all these problems is the same thing: **the thread's lifetime exceeds the lifetime of the objects it references**.

## decay-copy: All Arguments Are Passed by Value

Let's start with a fact that may surprise you: no matter how you write your thread function's signature, the `std::thread` constructor **always** copies (or moves) every argument you pass, by value. This behavior is called decay-copy—each argument's type goes through the same decay process as function template argument deduction: references are stripped, `const`/`volatile` is dropped, arrays decay to pointers, and functions decay to function pointers.

Let's look at the behavior in code:

```cpp
#include <thread>
#include <iostream>

void update_value(int& x)
{
    x = 42;
    std::cout << "Thread: set x to " << x << "\n";
}

int main()
{
    int value = 0;
    // Compile error! After decay-copy, int& becomes int
    // std::thread t(update_value, value);
    // The error is roughly: std::thread's arguments must be convertible to the decay-copied type

    // Correct approach: wrap the reference explicitly with std::ref
    std::thread t(update_value, std::ref(value));
    t.join();
    std::cout << "Main: value = " << value << "\n";
    return 0;
}
```

If you change `std::ref(value)` to passing `value` directly, the compiler rejects it—because `update_value`'s parameter is `int&`, but what `std::thread` stores internally is an `int` (after decay-copy), and an rvalue `int` cannot bind to a non-const reference. This compile error is actually the standard library protecting you: if you pass a reference to a local variable into a thread, and the thread might touch that variable only after it has been destroyed, the result is a dangling reference—ten thousand times worse than a compile error.

The design motivation behind decay-copy is crystal clear: **by default, every thread owns its own copy of the arguments, avoiding implicit shared state**. Shared state is a breeding ground for concurrency bugs, so the C++ standard library went with a "safe by default" strategy—if you want to share, you have to say so explicitly (with `std::ref`). That way, at code review time at least, the word `std::ref` stands out like a bright marker reminding you: there is sharing here, the lifetime needs checking.

### std::ref and std::cref: Explicit Reference Wrappers

`std::ref` and `std::cref` are reference wrappers defined in `<functional>`. They "wrap" a reference into a copyable object that internally holds the address of the original object. When `std::thread` passes this wrapper along to the thread function, the thread function receives a reference to the original object—not a copy.

```cpp
#include <thread>
#include <iostream>
#include <functional>
#include <string>

void append_suffix(std::string& str, const std::string& suffix)
{
    str += suffix;
}

int main()
{
    std::string message = "Hello";
    std::string suffix = " World";

    std::thread t(append_suffix, std::ref(message), std::cref(suffix));
    t.join();

    std::cout << message << "\n";  // prints "Hello World"
    return 0;
}
```

`std::ref(message)` makes the `str` parameter inside the thread function bind to the `message` variable in `main`; `std::cref(suffix)` binds the `suffix` parameter to a reference-to-const. Here `join()` guarantees the thread finishes while `message` and `suffix` are still in scope, so this is safe.

But what if you change that `join()` to `detach()`? The main thread may destroy `message` while the background thread is still modifying it—a classic use-after-free. `std::ref` opens the door to shared state, but it also means you are on your own to guarantee that the referenced object's lifetime covers the thread's entire execution. The standard library cannot help you there.

## Move Semantics: Passing Move-Only Types into Threads

Not every type can be copied. `std::unique_ptr`, `std::thread` itself, and many custom resource-managing classes are move-only—they support moving but not copying. The `std::thread` constructor accepts rvalue-reference parameters, so you can move these objects straight into the thread:

```cpp
#include <thread>
#include <iostream>
#include <memory>

void process_data(std::unique_ptr<int[]> data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        data[i] *= 2;
    }
    std::cout << "First element after processing: "
              << data[0] << "\n";
}

int main()
{
    constexpr std::size_t kSize = 10;
    auto data = std::make_unique<int[]>(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        data[i] = static_cast<int>(i);
    }

    // Move the unique_ptr into the thread
    std::thread t(process_data, std::move(data), kSize);
    t.join();

    // data is nullptr after the move
    std::cout << "data after move: "
              << (data ? "not null" : "null") << "\n";
    return 0;
}
```

`std::move(data)` transfers ownership of the `unique_ptr` into the thread's internal storage. Once the thread starts, the `data` parameter received by `process_data` holds exclusive ownership of that memory—nobody else can reach it at the same time, so there is no data race. When the thread finishes, the `unique_ptr` releases the memory automatically as the thread function returns. This is a beautifully clean ownership-transfer pattern: whoever owns the data releases it, and it is never shared.

The same pattern applies to moving `std::thread` objects themselves. You cannot copy a thread object (`std::thread`'s copy constructor is deleted), but you can move it, transferring ownership of the thread from one managing object to another—a topic we will expand on in the next article, "Thread Ownership and RAII".

## Dangling References: The Number-One detach Killer

Now we come to the heart of this article—dangling references. They are the most common, and the most insidious, source of bugs in `std::thread` usage. Their signature: the program sometimes works fine, sometimes crashes, sometimes produces wrong results—depending entirely on how fast the thread runs and how the operating system schedules it.

### Scenario 1: Accessing a Destroyed Local Variable After detach

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void faulty_function()
{
    int local_value = 42;

    std::thread t([&local_value]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // local_value may already be destroyed!
        std::cout << "Value: " << local_value << "\n";
    });
    t.detach();
    // After faulty_function returns, local_value is destroyed
    // but the thread still accesses it 100ms later -> undefined behavior
}

int main()
{
    faulty_function();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}
```

After `faulty_function` returns, `local_value`, being a stack variable, is destroyed. But the detached thread is still running in the background; 100ms later it tries to read the memory where `local_value` lived—and that memory has already been reclaimed, possibly overwritten by other function calls. This is the classic dangling reference: the reference still exists, but the memory it points to no longer holds the original object.

What makes this bug so maddening is that **it does not reproduce reliably**. If the caller of `faulty_function` happens to wait long enough (in the `main` above we sleep 200ms while the thread needs only 100ms), the program runs fine. But if scheduling slips even a little—say the system is under heavy load—the function returns before the thread has finished reading the data, and the bug fires. In the test environment you might run it ten thousand times without a hiccup; in production it crashes once at three in the morning in a customer's environment, and you have no way to reproduce it.

### Scenario 2: Capturing the this Pointer

In object-oriented code, member functions often launch threads from lambdas that capture `this`. But what if the object doesn't live as long as the thread?

```cpp
#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>

class BackgroundWorker {
public:
    BackgroundWorker() : running_(false) {}

    void start()
    {
        running_ = true;
        std::thread t([this]() {
            while (running_) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
        t.detach();
    }

    void stop()
    {
        running_ = false;
    }

    ~BackgroundWorker()
    {
        stop();
        // Problem: the detached thread may still be running!
        // The object its this pointer points to is being destroyed
    }

private:
    std::atomic<bool> running_;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
        // worker is destroyed here, but the detached thread still uses this
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // The thread still accesses members of the destroyed worker -> undefined behavior
    return 0;
}
```

`worker.start()` launches a detached thread, and the thread accesses the member variable `running_` through the captured `this` pointer. When `worker` is destroyed at the end of the scope, `this` becomes a dangling pointer—the memory it points to has been reclaimed. Every subsequent access to `running_` from the thread is undefined behavior.

You might think: "But I call `stop()` in the destructor—`running_` gets set to `false` and the thread exits on its own." The problem is that after `detach` you have **no mechanism to wait for the thread to actually exit**. `stop()` sets `running_` to `false` and returns immediately; the thread may not check the flag until its next loop iteration—by which time `worker` has already been destroyed. And if the thread spends a `sleep` between `running_` being set to `false` and its next check, the time window gets even wider.

The correct fix is not to detach at all: hold on to the thread object and join it in the destructor—we will see the repaired version shortly.

### Scenario 3: The Reference-Capture Lambda Trap

Reference capture `[&]` in lambdas is wonderfully convenient in single-threaded code—you never have to think about lifetimes, because the lambda's execution and the captured variables' lifetimes live in the same flow of execution. In multithreaded code, that convenience turns into a trap:

```cpp
#include <thread>
#include <iostream>
#include <vector>
#include <chrono>

void parallel_square_incorrect(const std::vector<int>& input,
                                std::vector<int>& output)
{
    std::vector<std::thread> threads;

    // Danger: [&] captures references to input and output
    // and to i as well!
    for (std::size_t i = 0; i < input.size(); ++i) {
        threads.emplace_back([&, i]() {
            // i is captured by value, OK
            // but input and output are captured by reference
            // if the threads are still running after parallel_square_incorrect returns...
            output[i] = input[i] * input[i];
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    // We join here, so inside this function it is safe
    // but change the join to detach and it is a disaster
}

int main()
{
    std::vector<int> data(100);
    std::vector<int> result(100);
    for (int i = 0; i < 100; ++i) {
        data[i] = i;
    }

    parallel_square_incorrect(data, result);

    std::cout << "result[5] = " << result[5] << "\n";  // 25
    return 0;
}
```

This code is actually safe—because the function joins all its threads before returning. But its "safety" is very fragile: the moment someone changes `join` to `detach` (perhaps thinking "I don't need to wait for the results"), it instantly becomes a dangling-reference bug. What's more, `[&]` is a one-size-fits-all capture mode—it captures references to every local variable, including ones you never meant to capture. If a temporary variable is added to the function later, it gets implicitly swept in too.

By contrast, writing the capture list out explicitly (`[&input, &output, i]`, or simply passing things as arguments) makes the intent clearer and easier to review. C++17 introduced `[=, *this]` to capture a copy of the entire object by value (instead of capturing only the `this` pointer), and C++20 went further and deprecated `[=]`'s implicit capture of `this`—you must now write `[=, this]` explicitly. All of these changes make capture semantics more explicit. But no matter how the syntax shifts, the core principle stays the same: **a referenced object must remain valid for the entire lifetime of the thing referencing it (the thread)**.

## Fix Patterns: Copy Data into the Thread, or Extend Lifetime with shared_ptr

Once you know where the problem is, the fix is straightforward. There are two main strategies.

### Strategy 1: Copy the Data into the Thread

The simplest and safest approach is to give each thread its own copy of the data—which happens to be exactly what `std::thread`'s decay-copy does by default.

```cpp
#include <thread>
#include <iostream>
#include <string>

void safe_version()
{
    std::string message = "Hello from parent";

    // Capture by value: copies message into the thread
    std::thread t([message]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // This accesses a copy of message, unrelated to the outer message
        std::cout << "Thread sees: " << message << "\n";
    });
    t.detach();

    // Now it does not matter even if message is destroyed
    // the thread holds its own copy
}
```

Change `[&message]` to `[message]` (capture by value), and the lambda copies `message` into its own closure object. `std::thread` then decay-copies that closure object into the thread's internal storage. The thread now holds data that is entirely its own, with no connection whatsoever to the outer `message`. After detach, there is no dangling-reference problem.

The cost of this strategy is the extra memory copy. For small objects (`int`s, pointers) it is nothing; for large ones (a big vector, a huge string) it can hurt performance. But in concurrent programming, correctness always outranks performance—get it correct first, optimize later. If the copying cost is genuinely unacceptable, use the next strategy.

### Strategy 2: Extending Lifetime with shared_ptr

When the data cannot be copied (or copying it is too expensive) and it still has to be shared across threads, `std::shared_ptr` is an excellent middle ground: it manages the shared data's lifetime automatically through reference counting—as long as there is still a `shared_ptr` pointing at it, the data will not be destroyed.

```cpp
#include <thread>
#include <iostream>
#include <memory>
#include <chrono>

class BackgroundWorker {
public:
    BackgroundWorker() : running_(std::make_shared<std::atomic<bool>>(true)) {}

    void start()
    {
        // Capture the shared_ptr (by value), reference count +1
        auto running = running_;
        std::thread t([running]() {
            while (running->load()) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500));
            }
            std::cout << "Worker exiting cleanly\n";
        });
        t.detach();
        // The thread holds a copy of running, shared_ptr reference count is 2
        // Even if BackgroundWorker is destroyed, the object running points to stays alive
    }

    void stop()
    {
        running_->store(false);
    }

    ~BackgroundWorker()
    {
        stop();
        // When running_ is destroyed, the reference count drops by 1
        // but the thread still holds a copy, so the object running points to is not destroyed
        // When the thread finally exits, the shared_ptr it holds is destroyed too and the count reaches zero
        // Only then is the object truly destroyed
    }

private:
    std::shared_ptr<std::atomic<bool>> running_;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
    }
    // worker is destroyed, but the thread is still running safely
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
```

The key change in this version is that `running_` goes from `std::atomic<bool>` to `std::shared_ptr<std::atomic<bool>>`, and the lambda captures the `shared_ptr` by value, taking a copy. Now two `shared_ptr`s point at the same `atomic<bool>` object: one inside `BackgroundWorker`, one inside the detached thread.

When `BackgroundWorker` is destroyed, it calls `stop()` to set `running` to `false`, and then the `running_` `shared_ptr` is destructed, dropping the reference count from 2 to 1. But the `atomic<bool>` object is not destroyed—because the thread still holds a `shared_ptr` copy. Eventually the thread sees that `running` is `false`, exits the loop, the lambda returns, the `shared_ptr` it holds is destructed, the reference count hits zero, and only then is the `atomic<bool>` object safely destroyed.

This pattern is extremely useful, but there is one thing to watch: the reference-count operations on `shared_ptr` itself are atomic (thread-safe), but whether access to the object it points to is safe is still up to you. In the example above, `atomic<bool>` is itself thread-safe, so we are fine. But if you share a vector across multiple threads via `shared_ptr<std::vector<int>>`, your concurrent access to that vector still needs synchronization—`shared_ptr` guarantees only that the object will not be destroyed too early; it says nothing about the thread safety of the object's internals.

### A Better Choice: Don't Use detach

Having covered all these fixes, our personal recommendation is: **in the vast majority of scenarios, do not use detach**. Combining join with RAII (joining automatically in the thread object's destructor) prevents nearly every dangling-reference problem—because join guarantees the thread completes before the scope exits, and the referenced objects live at least until the end of the scope.

The `BackgroundWorker` above, rewritten in join mode, looks like this:

```cpp
#include <thread>
#include <iostream>
#include <memory>
#include <atomic>
#include <chrono>

class BackgroundWorker {
public:
    BackgroundWorker() : running_(false) {}

    void start()
    {
        running_ = true;
        thread_ = std::thread([this]() {
            while (running_) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500));
            }
            std::cout << "Worker exiting cleanly\n";
        });
    }

    void stop()
    {
        running_ = false;
    }

    ~BackgroundWorker()
    {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        // join guarantees the thread exits before destruction completes
        // no dangling this pointer problem
    }

private:
    std::atomic<bool> running_;
    std::thread thread_;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    // worker stops first, then joins, during destruction
    // the thread exits cleanly, with no dangling references
    return 0;
}
```

This version is far simpler—no `shared_ptr`, no reference-count bookkeeping to worry about; `stop()` + `join()` in the destructor is the whole logic. `join()` is a synchronization point: it guarantees the thread has fully finished executing by the time `join` returns, and only afterward are `worker`'s member variables destroyed. The ordering in time is deterministic; there is no race.

So the ultimate strategy for fixing lifetime bugs is really a return to the original intent behind `std::thread`'s design: **synchronize the thread's exit with join, and use RAII to guarantee that join always happens**. detach is a tool with a clear meaning ("I genuinely don't care when it finishes"), but in practice, "don't care" is all too often a synonym for "didn't think it through".

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: Identifying Lifetime Bugs

Each of the three snippets below contains one lifetime bug. Point out the problem in each and fix it.

**Code Snippet A:**

```cpp
void spawn_printer()
{
    std::string msg = "Hello from detach!";
    std::thread t([&msg]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << msg << "\n";
    });
    t.detach();
}
```

**Code Snippet B:**

```cpp
class TaskRunner {
public:
    void run(int iterations)
    {
        for (int i = 0; i < iterations; ++i) {
            threads_.emplace_back([this, i]() {
                results_[i] = compute(i);
            });
        }
    }

    ~TaskRunner()
    {
        for (auto& t : threads_) {
            t.join();
        }
    }

    const std::vector<int>& results() const { return results_; }

private:
    int compute(int n) { return n * n; }
    std::vector<std::thread> threads_;
    std::vector<int> results_;
};
```

**Code Snippet C:**

```cpp
void process(std::vector<int>& output)
{
    int counter = 0;
    std::thread t([&output, &counter]() {
        for (int i = 0; i < 100; ++i) {
            output.push_back(counter++);
        }
    });
    // The programmer forgot to join or detach
}
```

Hints: Snippet A's problem is detach plus reference capture; Snippet B's problem lies not in the thread management itself but in the size of `results_` and concurrent access to it; Snippet C's problem is the most direct—forgetting join/detach leads to `std::terminate`.

### Exercise 2: Fixing the this-Pointer Capture with shared_ptr

Rework "Code Snippet B" above using the `std::shared_ptr` pattern so that `TaskRunner` can safely detach its threads. Make sure `results_` is not destroyed before all threads have finished.

### Exercise 3: Writing a Thread-Safe RAII Wrapper

Write a simple class `ScopedThread` that accepts a `std::thread` object in its constructor and automatically calls `join()` in its destructor. Make sure it correctly handles the following cases:

1. The thread passed in has already been joined (`joinable() == false`)
2. A default-constructed thread object is passed in
3. The `ScopedThread` object is moved (after the move, the original object should not join on destruction)

Test code:

```cpp
int main()
{
    {
        ScopedThread st(std::thread([]() {
            std::cout << "Hello from scoped thread\n";
        }));
        // st joins automatically on destruction
    }
    std::cout << "ScopedThread destroyed, thread joined\n";
    return 0;
}
```

This exercise is a preview of the next article, "Thread Ownership and RAII"—you will build the most basic thread RAII wrapper with your own hands.

## References

- [std::thread constructor — cppreference](https://en.cppreference.com/w/cpp/thread/thread/thread)
- [std::ref, std::cref — cppreference](https://en.cppreference.com/w/cpp/utility/functional/ref)
- [C++ Core Guidelines: CP.24 — Think of a thread as a global container](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container)
- [C++ Core Guidelines: CP.25 — Prefer gsl::joining_thread over std::thread](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp25-prefer-gsljoining_thread-over-stdthread)
- [Top 20 C++ Multithreading Mistakes and How to Avoid Them — A Coder's Journey](https://acodersjourney.com/top-20-cplusplus-multithreading-mistakes/)
- [Abseil Tip of the Week #180: Avoiding Dangling References](https://abseil.io/tips/180)
