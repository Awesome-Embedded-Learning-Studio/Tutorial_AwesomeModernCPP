---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Dive into thread parameter passing mechanisms, identifying concurrency
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
title: Thread Parameters and Lifetime
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md
  source_hash: 2acf83ae14bab23867a3ff351e9c1bff052fb15dcbc0ec7428c261e79f3a90e5
  token_count: 3850
  translated_at: '2026-05-20T04:34:03.498432+00:00'
---
# Thread Parameters and Lifetimes

In the previous article, we learned the basic operations of `std::thread` — creating, joining, detaching, and getting the ID. Along the way, we intentionally or unintentionally sidestepped a crucial topic: how exactly do the arguments passed to a thread reach the thread function? Why is it that sometimes I clearly pass in a reference, but modifications inside the thread don't affect the outside variable? And why does the program sometimes crash inexplicably after using `std::ref`?

In this article, we will thoroughly dismantle these problems. The parameter passing mechanism of `std::thread` has a very core design decision — **decay-copy**, which dictates that all arguments are conceptually passed by value. Once you understand this mechanism, you will be able to recognize the root cause of a large class of concurrency bugs. Then we will dig deeper: dangling references, `this` pointer capture, object destruction order, and lambda reference capture traps — the essence of all these problems boils down to one thing: **the thread's lifetime exceeds the lifetime of the objects it references**.

## decay-copy: All Arguments Are Passed by Value

Let's start with a fact that might surprise you: no matter how your thread function signature is written, the constructor of `std::thread` **always** copies (or moves) all passed arguments by value. This behavior is called decay-copy — the argument types go through the same decay process as function template argument deduction: references are stripped, `const`/`volatile` are discarded, arrays decay to pointers, and functions decay to function pointers.

Let's look at this behavior in code:

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
    // 编译错误！decay-copy 后 int& 变成了 int
    // std::thread t(update_value, value);
    // 错误信息大致是：std::thread 的参数需要能转换为 decay-copy 后的类型

    // 正确的做法：用 std::ref 显式包装引用
    std::thread t(update_value, std::ref(value));
    t.join();
    std::cout << "Main: value = " << value << "\n";
    return 0;
}
```

If you change `std::ref(value)` to directly pass `value`, the compiler will throw an error — because the parameter of `update_value` is `int&`, but internally `std::thread` stores a `int` (after decay-copy), and an rvalue `int` cannot bind to a non-const reference. This compilation error is actually the standard library protecting you: if you pass a reference to a local variable to a thread, and the thread might access it after that variable is destroyed, the result is a dangling reference — ten thousand times worse than a compilation error.

The design motivation behind decay-copy is very clear: **make each thread own its own copy of the arguments by default, avoiding implicit shared state**. Shared state is a breeding ground for concurrency bugs, and the C++ standard library chose a "safe by default" strategy — if you want to share, you must say so explicitly (using `std::ref`). This way, at least during code review, the word `std::ref` acts as a prominent marker reminding you: there is sharing here, check the lifetimes.

### std::ref and std::cref: Explicit Reference Wrappers

`std::ref` and `std::cref` are reference wrappers defined in `<functional>`. They "wrap" a reference into an object that can be copied, internally holding the address of the original object. When `std::thread` passes this wrapper to the thread function, the thread function receives a reference to the original object — not a copy.

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

    std::cout << message << "\n";  // 输出 "Hello World"
    return 0;
}
```

`std::ref(message)` makes the `str` parameter in the thread function bind to the `message` variable in `main`; `std::cref(suffix)` makes the `suffix` parameter bind to a const reference. Here, `join()` guarantees that the thread completes within the scope of `message` and `suffix`, so it is safe.

But what if you change `join()` to `detach()`? The main thread might destroy `message` while the background thread is still modifying it — this is a classic use-after-free. `std::ref` opens the door to shared state, but it also means you must guarantee yourself that the lifetime of the referenced object covers the entire execution period of the thread. The standard library cannot help you here.

## Move Semantics: Passing Move-Only Types to Threads

Not all types can be copied. `std::unique_ptr`, `std::thread` itself, and many custom resource management classes are move-only — they support moving but not copying. The constructor of `std::thread` accepts rvalue reference parameters, so you can directly move these objects into the thread:

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

    // 移动 unique_ptr 到线程中
    std::thread t(process_data, std::move(data), kSize);
    t.join();

    // data 在移动后为 nullptr
    std::cout << "data after move: "
              << (data ? "not null" : "null") << "\n";
    return 0;
}
```

`std::move(data)` transfers the ownership of `unique_ptr` to the thread's internal storage. After the thread starts, the `data` parameter received by `process_data` has sole ownership of that memory — no one else can access it simultaneously, so there is no data race. When the thread finishes executing, `unique_ptr` automatically releases the memory when the thread function returns. This is a very clean ownership transfer pattern: whoever owns the data is responsible for releasing it, with absolutely no sharing.

The same pattern also applies to moving the `std::thread` object itself. You cannot copy a thread object (the copy constructor of `std::thread` is deleted), but you can move it, transferring thread ownership from one managing object to another — we will expand on this topic in the next article, "Thread Ownership and RAII".

## Dangling References: The Number One Killer of detach

Next, we enter the most core part of this article — dangling references. They are the most common and most insidious source of bugs when using `std::thread`. Their hallmark is: the program sometimes works fine, sometimes crashes, and sometimes gives wrong results — entirely dependent on the thread's execution speed and the operating system's scheduling strategy.

### Scenario 1: Accessing Destroyed Local Variables After detach

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void faulty_function()
{
    int local_value = 42;

    std::thread t([&local_value]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // local_value 可能已经被销毁了！
        std::cout << "Value: " << local_value << "\n";
    });
    t.detach();
    // faulty_function 返回后，local_value 被销毁
    // 但线程还在 100ms 后访问它 -> 未定义行为
}

int main()
{
    faulty_function();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}
```

After `faulty_function` returns, `local_value` is destroyed as a stack variable. But the detached thread is still running in the background, and after 100ms it will try to read the memory where `local_value` resides — and that memory has already been reclaimed, possibly overwritten by other function calls. This is the classic dangling reference: the reference still exists, but the memory it points to is no longer the original object.

The most frustrating thing about this kind of bug is that it **is not reliably reproducible**. If the caller of `faulty_function` happens to wait long enough (for example, if `sleep` for 200ms in the above `main`, while the thread only needs 100ms), the program will run fine. But if the scheduling is delayed by just a little bit — for instance, when system load is high — the thread doesn't have time to finish reading the data before the function returns, and the bug triggers. It might run ten thousand times without issues in the test environment, then crash at three in the morning in a customer's environment, and you have no way to reproduce it.

### Scenario 2: this Pointer Capture

In object-oriented programming, member functions often start threads by capturing `this` in a lambda. But what if the object's lifetime is shorter than the thread's?

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
        // 问题：detach 的线程可能还在跑！
        // 它持有的 this 指针指向的对象正在被销毁
    }

private:
    std::atomic<bool> running_;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
        // worker 在这里析构，但 detach 的线程还在用 this
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // 线程还在访问已销毁的 worker 的成员 -> 未定义行为
    return 0;
}
```

`worker.start()` starts a detached thread, and the thread accesses the `running_` member variable through the captured `this` pointer. When `worker` is destructed at the end of its scope, the `this` pointer becomes a dangling pointer — the memory it points to has already been reclaimed. The thread's subsequent access to `running_` is undefined behavior.

You might think: "But I set `stop()` in the destructor, `running_` gets set to `false`, and the thread will exit on its own." The problem is that after `detach`, you **have no mechanism to wait for the thread to actually exit**. `stop()` sets `running_` to `false` and returns, but the thread might not check this flag until its next loop iteration — and by then `worker` has already been fully destructed. If the thread has a `sleep` between when `running_` is set to `false` and the next check, that time window becomes even larger.

The correct approach is to not detach, but to hold the thread object and join in the destructor — we will see the fixed version shortly.

### Scenario 3: The Lambda Reference Capture Trap

Lambda reference capture `[&]` is very convenient in single-threaded code — you don't need to worry about lifetimes, because the lambda's execution and the lifetime of the captured variables are in the same execution flow. But in multithreading, this becomes a trap:

```cpp
#include <thread>
#include <iostream>
#include <vector>
#include <chrono>

void parallel_square_incorrect(const std::vector<int>& input,
                                std::vector<int>& output)
{
    std::vector<std::thread> threads;

    // 危险：[&] 捕获了 input 和 output 的引用
    // 以及 i 的引用！
    for (std::size_t i = 0; i < input.size(); ++i) {
        threads.emplace_back([&, i]() {
            // i 是值捕获，OK
            // 但 input 和 output 是引用捕获
            // 如果 parallel_square_incorrect 返回后线程还在跑...
            output[i] = input[i] * input[i];
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    // 这里 join 了，所以在这个函数内部是安全的
    // 但如果把 join 改成 detach，就是灾难
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

This code is actually safe — because the function joins all threads before returning. But its "safety" is very fragile: as soon as someone changes `join` to `detach` (perhaps thinking "I don't need to wait for the result"), it instantly becomes a dangling reference bug. Furthermore, `[&]` is a "blanket" capture method — it captures references to all local variables, including ones you didn't intend to capture. If a temporary variable is added to the function later, it will be implicitly captured as well.

In contrast, explicitly writing out the capture list (`[&input, &output, i]` or simply using parameter passing) makes the intent clearer and easier to review. C++17 introduced `[=, *this]` to capture a copy of the entire object by value (rather than just capturing the `this` pointer), and C++20 went a step further by deprecating the implicit capture of `this` by `[=]` — now you must explicitly write `[=, this]`. These changes make the capture semantics more explicit. But no matter how the syntax changes, the core principle remains the same: **the referenced object must remain valid for the entire lifetime of the referent (the thread)**.

## Fix Patterns: Copy Into the Thread, or Use shared_ptr to Extend the Lifetime

Once we know where the problem lies, the fix is straightforward. There are two main strategies.

### Strategy 1: Copy Data Into the Thread

The simplest and safest approach is to let each thread have its own copy of the data — which happens to be the default behavior of `std::thread`'s decay-copy.

```cpp
#include <thread>
#include <iostream>
#include <string>

void safe_version()
{
    std::string message = "Hello from parent";

    // 值捕获：拷贝 message 到线程中
    std::thread t([message]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // 这里访问的是 message 的副本，跟外部的 message 无关
        std::cout << "Thread sees: " << message << "\n";
    });
    t.detach();

    // 现在即使 message 被销毁了也无所谓
    // 线程持有自己的副本
}
```

Changing `[&message]` to `[message]` (value capture) means the lambda will copy a `message` into its own closure object. `std::thread` will then decay-copy this closure object into the thread's internal storage. This way, the thread holds entirely its own data, with no connection to the external `message`. There is no dangling reference issue after detaching.

The cost of this strategy is the extra memory copy. For small objects (`int`, pointers) it doesn't matter, but for large objects (a large vector, a huge string) there might be a performance impact. However, in concurrent programming, correctness always comes before performance — ensure correctness first, then optimize performance. If the copy overhead is truly unacceptable, use the strategy below.

### Strategy 2: Use shared_ptr to Extend the Lifetime

When data cannot be copied (or the copy cost is too high), yet needs to be shared between threads, `std::shared_ptr` is a great compromise: it automatically manages the shared data's lifetime through reference counting, and as long as a `shared_ptr` points to it, the data will not be destroyed.

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
        // 捕获 shared_ptr（值捕获），引用计数 +1
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
        // 线程持有 running 的副本，shared_ptr 引用计数为 2
        // 即使 BackgroundWorker 析构，running 指向的对象仍然存活
    }

    void stop()
    {
        running_->store(false);
    }

    ~BackgroundWorker()
    {
        stop();
        // running_ 析构时引用计数 -1
        // 但线程还持有一个副本，所以 running 指向的对象不会销毁
        // 线程最终退出时，它持有的 shared_ptr 也析构，引用计数归零
        // 此时对象才真正被销毁
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
    // worker 已析构，但线程还在安全地运行
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
```

The key change in this version is converting `running_` from `std::atomic<bool>` to `std::shared_ptr<std::atomic<bool>>`, and then the lambda gets a copy of the `shared_ptr` through value capture. This way, there are two `shared_ptr`s pointing to the same `atomic<bool>` object: one in `BackgroundWorker`, and one in the detached thread.

When `BackgroundWorker` is destructed, it calls `stop()` to set `running` to `false`, and then the `shared_ptr` `running_` is destructed, dropping the reference count from two to one. But the `atomic<bool>` object is not destroyed — because the thread still holds a `shared_ptr` copy. The thread eventually detects that `running` is `false`, exits the loop, the lambda returns, its held `shared_ptr` is destructed, the reference count reaches zero, and only then is the `atomic<bool>` object safely destroyed.

This pattern is very practical, but it also has something to note: the reference counting operations of `shared_ptr` itself are atomic (thread-safe), but whether accessing the object it points to is safe still needs to be guaranteed by you. In the example above, `atomic<bool>` itself is thread-safe, so there is no problem. But if you use `shared_ptr<std::vector<int>>` to share a vector among multiple threads, your concurrent access to the vector still needs synchronization — `shared_ptr` only guarantees that the object will not be destroyed prematurely, not that the object's internals are thread-safe.

### A Better Choice: Don't detach

After discussing all these fix strategies, my personal recommendation is: **in the vast majority of scenarios, do not use detach**. Using join together with RAII (automatically joining in the thread object's destructor) can avoid almost all dangling reference problems — because join guarantees that the thread completes before the scope exits, and the referenced objects live at least until the end of the scope.

The above `BackgroundWorker` written with the join pattern looks like this:

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
        // join 保证了线程在析构完成之前退出
        // 不存在 this 指针悬垂的问题
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
    // worker 析构时先 stop 再 join
    // 线程干净地退出，没有悬垂引用
    return 0;
}
```

This version is much cleaner — no need for `shared_ptr`, no need to worry about reference counting, and `stop()` + `join()` in the destructor is the entire logic. `join()` is a synchronization point; it guarantees that the thread has fully completed execution when `join` returns, and only then are `worker`'s member variables destroyed. The temporal order is deterministic, with no race conditions.

So, the ultimate strategy for fixing lifetime bugs is actually to return to the original intent of `std::thread`'s design: **use join to synchronize thread exit, and use RAII to guarantee that join is always executed**. detach is a tool with clear semantics ("I really don't care when it finishes"), but in practice, "not caring" is often a synonym for "not thinking it through."

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: Identify Lifetime Bugs

Each of the three code snippets below has a lifetime bug. Please point out the problem in each one and fix it.

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
    // 程序员忘了 join 或 detach
}
```

Hint: The problem in Code Snippet A is detach + reference capture; the problem in Code Snippet B is not in thread management itself, but in the size of `results_` and concurrent access; the problem in Code Snippet C is the most straightforward — forgetting to join/detach will trigger `std::terminate`.

### Exercise 2: Fix this Pointer Capture with shared_ptr

Rewrite "Code Snippet B" above using the `std::shared_ptr` pattern, so that `TaskRunner` can safely detach threads. Ensure that `results_` is not destroyed before all threads have finished.

### Exercise 3: Write a Thread-Safe RAII Wrapper

Write a simple class `ScopedThread` that accepts a `std::thread` object in its constructor and automatically calls `join()` in its destructor. Ensure it correctly handles the following cases:

1. The passed-in thread has already been joined (`joinable() == false`)
2. A default-constructed thread object is passed in
3. The `ScopedThread` object is moved (the original object should not join in its destructor after being moved from)

Test code:

```cpp
int main()
{
    {
        ScopedThread st(std::thread([]() {
            std::cout << "Hello from scoped thread\n";
        }));
        // st 析构时自动 join
    }
    std::cout << "ScopedThread destroyed, thread joined\n";
    return 0;
}
```

This exercise is a preview of the next article, "Thread Ownership and RAII" — you will implement the most basic thread RAII wrapper with your own hands.

## References

- [std::thread constructor — cppreference](https://en.cppreference.com/w/cpp/thread/thread/thread)
- [std::ref, std::cref — cppreference](https://en.cppreference.com/w/cpp/utility/functional/ref)
- [C++ Core Guidelines: CP.24 — Think of a thread as a global container](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container)
- [C++ Core Guidelines: CP.25 — Prefer gsl::joining_thread over std::thread](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp25-prefer-gsljoining_thread-over-stdthread)
- [Top 20 C++ Multithreading Mistakes and How to Avoid Them — A Coder's Journey](https://acodersjourney.com/top-20-cplusplus-multithreading-mistakes/)
- [Abseil Tip of the Week #180: Avoiding Dangling References](https://abseil.io/tips/180)
