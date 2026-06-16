---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Delve into thread parameter passing mechanisms, identifying concurrency
  bugs caused by dangling references and object destruction order.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- std::thread 基础
reading_time_minutes: 18
related:
- 线程所有权与 RAII
- CPU cache 与 OS 线程
tags:
- host
- cpp-modern
- intermediate
- 内存管理
title: Thread Parameters and Lifecycle
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md
  source_hash: c392a9998fd17fd404c3a002db3d83b8c7900e083cbe2d09b5d05ce8d0af094a
  translated_at: '2026-06-16T04:03:14.650625+00:00'
  engine: anthropic
  token_count: 3845
---
# Thread Parameters and Lifetime

In the previous post, we learned the basic operations of `std::thread`: creation, `join`, `detach`, and getting IDs. At that time, we intentionally or unintentionally skirted around a very important topic: how do the arguments passed to the thread actually reach the thread function? Why is it that sometimes I clearly pass in a reference, but the variable outside doesn't change when modified inside the thread? Why does the program sometimes crash inexplicably after using `std::ref`?

In this post, we will thoroughly dissect these issues. A core design decision in `std::thread`'s parameter passing mechanism is **decay-copy**, which dictates that all arguments are conceptually passed by value. Understanding this mechanism allows you to see the root cause of a large class of concurrency bugs. Then we will go deeper: dangling references, `this` pointer capture, object destruction order, lambda reference capture pitfalls—the essence of these issues is all the same thing: **the thread's lifetime exceeds the lifetime of the objects it references**.

## decay-copy: All Arguments are Passed by Value

First, a fact that might surprise you: regardless of how you write your thread function signature, the `std::thread` constructor **always** copies (or moves) all passed arguments by value. This behavior is called decay-copy—the argument types undergo the same decay process as function template argument deduction: references are stripped, `const`/`volatile` are discarded, arrays decay to pointers, and functions decay to function pointers.

Let's look at this behavior in code:

```cpp
void func(int& n) {
    n *= 2;
}

int main() {
    int x = 10;
    // std::thread t(func, x); // Error! x is copied, int cannot bind to int&
    std::thread t(func, std::ref(x)); // OK
    t.join();
    // x is now 20
}
```

If you change `std::ref(x)` to pass `x` directly, the compiler will report an error—because `func`'s parameter is `int&`, but `std::thread` internally stores an `int` (after decay-copy), and an rvalue `int` cannot bind to a non-const reference. This compilation error is actually the standard library protecting you: if you pass a reference to a local variable to a thread, and the thread might access that variable after it is destroyed, the result is a dangling reference—far worse than a compilation error.

The design motivation for decay-copy is very clear: **give each thread its own copy of arguments by default, avoiding implicit shared state**. Shared state is a breeding ground for concurrency bugs. The C++ standard library chose a "safe by default" strategy—if you want to share, you must say so explicitly (using `std::ref`). At least during code review, the word `ref` acts as a conspicuous marker reminding you: there is sharing here, check lifetimes.

### std::ref 与 std::cref: Explicit Reference Wrappers

`std::ref` and `std::cref` are reference wrappers defined in `<functional>`. They "wrap" a reference into an object that can be copied, internally holding the address of the original object. When `std::thread` passes this wrapper to the thread function, the thread function receives a reference to the original object—rather than a copy.

```cpp
#include <functional>
#include <thread>
#include <string>
#include <iostream>

void modify(std::string& str, const int& factor) {
    // str is a reference to the original object
    // factor is a const reference
    for (int i = 0; i < factor; ++i) {
        str += "!";
    }
}

int main() {
    std::string message = "Hello";
    int count = 3;

    // Wrap references to pass them to the thread
    std::thread t(modify, std::ref(message), std::cref(count));
    t.join();

    std::cout << message << std::endl; // Output: Hello!!!
}
```

`std::ref` causes the `std::string&` parameter in the thread function to bind to the `message` variable in `main`; `std::cref` causes the `int&` parameter to bind to a const reference. Here `t.join()` guarantees the thread finishes within the scope of `message` and `count`, so it is safe.

But what if you change `t.join()` to `t.detach()`? The main thread might destroy `message` while the background thread is still modifying it—this is a classic use-after-free. `std::ref` opens the door to shared state, but it also means you must ensure the lifetime of the referenced object covers the entire execution period of the thread. The standard library cannot help you there.

## Move Semantics: Passing Move-Only Types to Threads

Not all types can be copied. `std::unique_ptr`, `std::thread` itself, and many custom resource management classes are move-only—they support move but not copy. `std::thread`'s constructor accepts rvalue reference parameters, so you can move these objects directly into the thread:

```cpp
#include <thread>
#include <memory>
#include <iostream>

void task(std::unique_ptr<int> ptr) {
    // The thread now owns the unique_ptr
    std::cout << "Thread received value: " << *ptr << std::endl;
    // Memory is automatically freed when ptr goes out of scope here
}

int main() {
    auto ptr = std::make_unique<int>(42);

    // std::thread t(task, ptr);       // Error! unique_ptr is not copyable
    std::thread t(task, std::move(ptr)); // OK, move ownership

    t.join();
    // ptr is now nullptr, ownership transferred
}
```

`std::move` transfers ownership of the `unique_ptr` to the thread's internal storage. Once the thread starts, the `ptr` parameter received by `task` holds the sole ownership of that memory—no one else can access it simultaneously, so there is no data race. When the thread finishes execution, `ptr` automatically releases the memory when the thread function returns. This is a very clean ownership transfer model: whoever owns the data is responsible for releasing it; no sharing.

The same pattern applies to moving `std::thread` objects themselves. You cannot copy a thread object (`std::thread`'s copy constructor is deleted), but you can move it, transferring thread ownership from one managing object to another—we will expand on this in the next post, "Thread Ownership and RAII".

## Dangling References: The Number One Killer of detach

Next, we enter the core part of this post—dangling references. It is the most common and insidious source of bugs in `std::thread` usage. Its characteristics are: the program sometimes works, sometimes crashes, sometimes gives wrong results—entirely dependent on thread execution speed and OS scheduling policy.

### Scenario 1: Accessing Destroyed Local Variables After detach

```cpp
void dangerous_task() {
    int local_data = 100;
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Data: " << local_data << std::endl; // Dangling reference!
    });
    t.detach(); // Detach the thread
} // local_data is destroyed here, but the thread might still be running
```

When `dangerous_task` returns, `local_data` is destroyed as a stack variable. But the detached thread is still running in the background; after 100ms it will try to read the memory where `local_data` resided—and that memory has been reclaimed, possibly overwritten by other function calls. This is the classic dangling reference: the reference still exists, but the memory it points to is no longer the original object.

The most frustrating part of this bug is that it **is not deterministic**. If the caller of `dangerous_task` happens to wait long enough (e.g., `sleep` for 200ms in `main` while the thread only needs 100ms), the program might run fine. But if the scheduling is delayed just a little—like under high system load—the function returns before the thread has time to read the data, and the bug triggers. It might run ten thousand times in the test environment without issue, then crash at 3 AM in a customer's environment, and you have no way to reproduce it.

### Scenario 2: this Pointer Capture

In object-oriented programming, member functions often start threads by capturing `this` via lambda. But what if the object's lifetime is shorter than the thread's?

```cpp
class Worker {
public:
    void start() {
        running_ = true;
        // Capture 'this' to access member variables
        thread_ = std::thread([this]() {
            while (running_) {
                // Do work using member variables
                process();
            }
        });
        thread_.detach();
    }

    ~Worker() {
        running_ = false;
        // No join here!
    }

private:
    bool running_;
    std::string data_;
    std::thread thread_;
    void process() { /* ... */ }
};

void usage() {
    Worker worker;
    worker.start();
} // worker is destroyed here, but the detached thread might still be running
```

`Worker::start` launches a detached thread. The thread accesses the `running_` member variable through the captured `this` pointer. When `Worker` is destructed at the end of the scope, the `this` pointer becomes a dangling pointer—the memory it points to has been reclaimed. The thread's subsequent access to `running_` is undefined behavior.

You might think: "But I set `running_` to `false` in the destructor, the thread will exit itself." The problem is that after `running_ = false` you **have no mechanism to ensure the thread actually exits**. The destructor sets `running_` to `false` and returns; the thread might not check this flag until its next loop iteration—by which time `Worker` is already destroyed. If the thread has a `sleep` inside the loop after `running_` is set to `false` and before the next check, the time window is even larger.

The correct approach is not to detach, but to hold the thread object and join at destruction—we will see a fixed version shortly.

### Scenario 3: Lambda Reference Capture Trap

Lambda reference capture `[&]` is very convenient in single-threaded code—you don't need to worry about lifetimes because the lambda's execution and the captured variables' lifetimes are in the same execution flow. But in multithreading, this becomes a trap:

```cpp
void parallel_task() {
    std::string s = "Data";
    std::thread t1([&]() { std::cout << "T1: " << s << std::endl; });
    std::thread t2([&]() { std::cout << "T2: " << s << std::endl; });

    t1.join();
    t2.join();
}
```

This code is actually safe—because the function joins all threads before returning. But its "safety" is very fragile: as soon as someone changes `join` to `detach` (perhaps thinking "I don't need to wait for results"), it immediately becomes a dangling reference bug. Also, `[&]` is a "catch-all" capture method—it captures references to all local variables, including those you didn't intend to capture. If a temporary variable is added to the function later, it will be implicitly captured too.

In contrast, explicitly writing the capture list (`[s]` or simply using parameter passing) makes the intent clearer and easier to review. C++17 introduced `*this` to capture a copy of the entire object by value (rather than just the `this` pointer), and C++20 went further by deprecating `[=]`'s implicit capture of `this`—now you must explicitly write `[=, this]` or `[this]`. These changes make capture semantics more explicit. But regardless of syntax changes, the core principle remains: **the referenced object must remain valid for the entire lifetime of the referencer (the thread)**.

## Fix Patterns: Copy into Thread, or Extend Lifetime with shared_ptr

Now that we know where the problem lies, the fix is straightforward. There are mainly two strategies.

### Strategy 1: Copy Data into the Thread

The simplest and safest approach is to give each thread its own copy of the data—this happens to be the default behavior of `std::thread`'s decay-copy.

```cpp
void safe_task() {
    std::string data = "Sensitive Data";
    // [=] captures by value (copy)
    std::thread t([=]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Thread sees: " << data << std::endl;
    });
    t.detach();
} // 'data' is destroyed, but the thread has its own copy
```

Changing `[&]` to `[=]` (value capture) causes the lambda to copy a `data` string into its own closure object. `std::thread` then decay-copies this closure object into the thread's internal storage. Thus, the thread holds completely its own data, unrelated to the external `data`. There is no dangling reference issue after detach.

The cost of this strategy is the extra memory copy. For small objects (`int`, pointers), it doesn't matter, but for large objects (a large `vector`, a huge string), there might be a performance impact. However, in concurrent programming, correctness always comes before performance—ensure correctness first, then optimize performance. If the copy overhead is truly unacceptable, use the strategy below.

### Strategy 2: Extend Lifetime with shared_ptr

When data cannot be copied (or is too expensive to copy) and needs to be shared between threads, `std::shared_ptr` is an excellent compromise: it automatically manages the shared data's lifetime via reference counting; as long as a `shared_ptr` points to it, the data will not be destroyed.

```cpp
#include <memory>
#include <thread>
#include <atomic>
#include <iostream>

class SafeWorker {
public:
    void start() {
        // Use shared_ptr to manage 'this'
        // We capture 'shared_from_this()' by value
        // Note: Requires SafeWorker to be managed by shared_ptr to begin with
        // For this example, we'll simulate it with a manual shared_ptr
    }
};

// Correct implementation pattern:
void run_safe_worker() {
    auto worker_ptr = std::make_shared<SafeWorker>();

    std::thread t([worker_ptr]() {
        // worker_ptr is a copy of the shared_ptr, ref count is 2
        while (worker_ptr->running_) {
            worker_ptr->process();
        }
    });
    t.detach();
} // worker_ptr goes out of scope, ref count drops to 1.
  // The object survives because the thread still holds a shared_ptr.
```

The key change in this version is changing `this` to a `std::shared_ptr` managing the object (conceptually `shared_from_this`), and the lambda captures a copy of this `shared_ptr` by value. Now there are two `shared_ptr`s pointing to the same `SafeWorker` object: one in `run_safe_worker`, one in the detached thread.

When `worker_ptr` destructs, it calls `running_ = false`, then this `shared_ptr` destructs, and the reference count drops from 2 to 1. But the `SafeWorker` object is not destroyed—because the thread still holds a `shared_ptr` copy. Eventually, the thread detects `running_` is `false`, exits the loop, the lambda returns, its held `shared_ptr` destructs, the reference count hits zero, and only then is the `SafeWorker` object safely destroyed.

This pattern is very practical, but it has a caveat: the reference counting operations of `std::shared_ptr` itself are atomic (thread-safe), but you still need to ensure the thread safety of accessing the object it points to. In the example above, `std::atomic<bool>` is thread-safe, so it's fine. But if you use `std::shared_ptr` to share a `vector` between multiple threads, you still need synchronization for concurrent access to the vector—`shared_ptr` only guarantees the object won't be destroyed prematurely, not internal thread safety.

### Better Choice: Don't detach

Having discussed so many fix strategies, my personal advice is: **in the vast majority of scenarios, do not use detach**. Using `join` with RAII (automatically joining in the thread object's destructor) can avoid almost all dangling reference issues—because `join` guarantees the thread finishes before the scope exits, and referenced objects live at least until the end of the scope.

The `Worker` example above written with the `join` pattern looks like this:

```cpp
class RAIIThreadWorker {
public:
    RAIIThreadWorker() : running_(false) {}

    ~RAIIThreadWorker() {
        stop(); // Ensure thread is joined before destruction
    }

    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            while (running_) {
                process();
            }
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    std::atomic<bool> running_;
    std::thread thread_;
    void process() { /* ... */ }
};
```

This version is much more concise—no `shared_ptr`, no need to worry about reference counts. The logic in the destructor is simply `running_ = false` + `join()`. `join()` is the synchronization point; it guarantees the thread is fully executed before `~RAIIThreadWorker` returns, after which member variables are destroyed. The timing is deterministic; there is no race condition.

So, the ultimate strategy for fixing lifetime bugs is actually returning to the original intent of `std::thread`'s design: **use `join` to synchronize thread exit, and use RAII to ensure `join` is definitely executed**. `detach` is a tool with specific semantics ("I really don't care when it ends"), but in practice, "not caring" is often a synonym for "didn't think it through".

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `08_thread_lifetime`.

## Exercises

### Exercise 1: Identify Lifetime Bugs

Each of the three code snippets below has a lifetime bug. Identify the problem and fix it.

**Code Snippet A:**

```cpp
void snippet_a() {
    int data = 0;
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        data += 10;
    });
    t.detach();
}
```

**Code Snippet B:**

```cpp
class B {
public:
    void run() {
        // Capture 'this' by reference implicitly
        std::thread t([this]() {
            while (running_) {
                // Do work
            }
        });
        t.detach();
    }
    ~B() { running_ = false; }
private:
    bool running_;
    std::vector<int> large_data_;
};
```

**Code Snippet C:**

```cpp
void snippet_c() {
    std::thread t([]() {
        std::cout << "Hello" << std::endl;
    });
    // Missing join or detach
}
```

Hint: Snippet A's problem is detach + reference capture; Snippet B's problem is not thread management itself, but the `this` pointer lifetime; Snippet C's problem is most direct—forgot `join`/`detach` will call `std::terminate`.

### Exercise 2: Fix this Pointer Capture with shared_ptr

Modify "Code Snippet B" above to use the `std::shared_ptr` pattern so that `B` can safely detach threads. Ensure `B` is not destroyed before all threads finish.

### Exercise 3: Write a Thread-Safe RAII Wrapper

Write a simple class `ThreadGuard` that accepts a `std::thread` object in its constructor and automatically calls `join` in its destructor. Ensure it handles the following correctly:

1. The passed thread has already been joined (`joinable()` returns false).
2. A default-constructed thread object is passed.
3. The `ThreadGuard` object is moved (the original object should not join in its destructor after being moved).

Test code:

```cpp
void test_guard() {
    std::thread t([]() { std::cout << "Work done" << std::endl; });
    ThreadGuard guard(std::move(t));
    // No need to call join manually here
}
```

This exercise is a preview of the next post, "Thread Ownership and RAII"—you will implement the most basic thread RAII wrapper with your own hands.

## References

- [std::thread constructor — cppreference](https://en.cppreference.com/w/cpp/thread/thread/thread)
- [std::ref, std::cref — cppreference](https://en.cppreference.com/w/cpp/utility/functional/ref)
- [C++ Core Guidelines: CP.24 — Think of a thread as a global container](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container)
- [C++ Core Guidelines: CP.25 — Prefer gsl::joining_thread over std::thread](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp25-prefer-gsljoining_thread-over-stdthread)
- [Top 20 C++ Multithreading Mistakes and How to Avoid Them — A Coder's Journey](https://acodersjourney.com/top-20-cplusplus-multithreading-mistakes/)
- [Abseil Tip of the Week #180: Avoiding Dangling References](https://abseil.io/tips/180)
