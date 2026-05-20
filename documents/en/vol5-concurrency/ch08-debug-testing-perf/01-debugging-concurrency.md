---
title: Concurrent Program Debugging Techniques
description: Master the use of tools like ThreadSanitizer and Helgrind, and establish
  a systematic diagnostic workflow for concurrency bugs.
chapter: 8
order: 1
tags:
- host
- cpp-modern
- intermediate
- 进阶
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- mutex 与 RAII 锁
- 原子操作
- 线程安全队列
related:
- 并发性能测试与基准
translation:
  source: documents/vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md
  source_hash: f7097c193f2b900441cf3b2eee489e8b5c4ffc1bd4a32a4cff132a1d5604bb64
  translated_at: '2026-05-20T04:49:49.095419+00:00'
  engine: anthropic
  token_count: 5016
---
# Debugging Concurrent Programs

Honestly, you only truly understand the pain of debugging concurrent programs after you've been burned by them yourself. Bugs in single-threaded programs are at least deterministic—give them the same input, and they will crash in the exact same way at the exact same spot. Concurrent bugs are nothing like that. A data race might only appear once in ten thousand runs, a dead lock might only trigger under a very specific thread scheduling order, and it always follows the pattern of "works fine on my machine, guaranteed to fail in CI." I once spent two full days chasing a data race, only to discover that a lambda expression had captured a reference to a local variable—you can't spot this bug just by reading the code, because under a single-threaded execution path, it is completely correct.

In this chapter, we will build a systematic methodology for debugging concurrency. Not the "just add a print and see" kind, but a proper workflow starting from understanding the classification of bugs, to choosing the right tools, to interpreting tool reports, and finally forming a reproducible, verifiable fix. We will focus on ThreadSanitizer (TSan), Valgrind's Helgrind tool, Clang's compile-time thread safety analysis, and a practical structured logging solution.

## Environment Setup

All commands and code in this chapter were tested under the following environment: Ubuntu 22.04 LTS (WSL2 works too), using Clang 16+ or GCC 12+ (requires TSan support), Valgrind version 3.18 or higher (`apt install valgrind` is fine), and GDB 12+. If you use CMake to manage your project, version 3.20 or higher is required. If your distribution is older, the TSan report format might differ slightly, but the core content remains the same.

## The Four Major Categories of Concurrent Bugs

Before we start using tools, we need to clearly understand the main categories of concurrent bugs, because different types require completely different diagnostic strategies.

**Data race** is the most common and most insidious type. Its definition is strict: two or more threads access the same memory location simultaneously, at least one of them is a write, and there is no synchronization relationship between them (no mutex, no atomic, no happens-before). The C++ standard explicitly states that a data race is undefined behavior (UB)—not "it might go wrong," but "anything can happen," including but not limited to reading garbage values, program crashes, or even appearing to "work normally" and then suddenly exploding one day. Data races are hard to track because they depend on the thread scheduling order, which can be completely different when you are debugging versus when running in production. You add a `printf` to debug, and the printing itself changes the timing, causing the bug to disappear—this is the classic "Heisenbug."

**Dead lock** is another major category. Two or more threads wait for resources held by each other, neither yields, and the program freezes completely. Dead locks are actually more deterministic than data races—as long as the specific lock acquisition order is triggered, it is guaranteed to happen. The problem is that the trigger conditions can be very complex, involving specific execution path combinations across multiple threads. Furthermore, dead locks often don't appear under normal load, only exposing themselves under certain concurrency patterns.

**Livelock** is more hidden than a dead lock. The threads aren't stuck—CPU usage might be 100%—but there is no meaningful progress. A classic example is two threads politely yielding resources to each other, resulting in neither acquiring them. The symptom of a livelock is a program slowing down rather than freezing, making it easy to misdiagnose as a performance issue.

Finally, there is the **dangling reference**. A thread accesses an object that has already gone out of scope via a reference or pointer—this is especially common in asynchronous programming. For example, you start a thread, pass in a reference to a local variable, and then the function returns. The local variable is destroyed, but the thread is still using that reference. The manifestation of this bug depends on what that memory has been reallocated for—you might read a value that "looks normal but is actually wrong," or you might get a direct segfault.

| Bug Type | Core Characteristic | Reproduction Difficulty | Typical Signal |
|----------|---------------------|-------------------------|----------------|
| Data race | Unsynchronized concurrent read/write | Extremely high (timing-dependent) | Intermittent incorrect results, Heisenbug |
| Dead lock | Circular resource waiting | Medium-high (path-dependent) | Program completely freezes |
| Livelock | Repeated yielding with no progress | Medium | CPU 100% but no output |
| Dangling reference | Accessing a destroyed object | High (memory state-dependent) | Intermittent crashes, garbage values |

## ThreadSanitizer: The Nemesis of Data Races

### How It Works: Compiler Instrumentation

ThreadSanitizer (TSan for short) works by instrumenting your code at compile time. When you add the `-fsanitize=thread` compiler flag, the compiler inserts extra check code before and after every memory access (read and write). At runtime, these checks maintain a "shadow memory" that records the access history and synchronization events for each memory location.

TSan uses a hybrid algorithm based on happens-before relationships and lockset analysis. Simply put, it tracks the thread ID and a logical timestamp (vector clock) for each memory access, while also tracking which mutexes are held by the current thread. If it finds two memory accesses from different threads without a happens-before relationship (meaning there are no synchronization operations between them), and at least one is a write, it reports a data race. The theoretical foundation of this algorithm guarantees that if a data race actually occurs in your test execution (even if only once), it will be detected at the algorithm level. However, note that TSan's implementation maintains a finite-sized history buffer for every 8-byte memory location. In extreme cases (e.g., a massive number of threads frequently accessing the same address causing old records to be evicted), the actual false-negative rate is very low but not zero. For the vast majority of real-world scenarios, you can treat "TSan reported nothing" as a strong signal that "there truly is no data race on this execution path."

### Enabling TSan

Enabling TSan is very simple—just add the corresponding flag at compile time:

```bash
# 编译时加上 -fsanitize=thread 和调试信息
clang++ -fsanitize=thread -g -O1 -pthread your_program.cpp -o your_program

# 或者 GCC
g++ -fsanitize=thread -g -O1 -pthread your_program.cpp -o your_program
```

There are a few points to note here. First, `-g` must be included; otherwise, TSan reports will only have addresses without source code locations, making it very hard to pinpoint the problem. Second, the official recommendation is to use `-O1` or higher, mainly for performance—TSan itself introduces a 5-15x slowdown, and unoptimized code from `-O0` makes the overhead even worse. But don't use `-O2` or higher either, because aggressive optimization might inline too many functions, making stack traces hard to read. Third, TSan does not support being used simultaneously with AddressSanitizer (ASan). If you enable both in your build script, the compiler will throw an error directly.

If you use CMake, you can configure it like this:

```cmake
# CMakeLists.txt 中启用 TSan
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
endif()
```

Then simply `cmake -DENABLE_TSAN=ON ..`.

### Hands-on: A Complete Data Race Diagnosis

Let's look at a classic data race scenario and catch it step by step with TSan.

```cpp
#include <thread>
#include <vector>
#include <iostream>

class ThreadSafeCounter {
public:
    void increment()
    {
        // 看起来人畜无害，实际上这里有 data race
        count_++;
    }

    int get() const { return count_; }

private:
    int count_{0};
};

int main()
{
    ThreadSafeCounter counter;
    constexpr int kIterations = 100000;

    auto worker = [&counter]() {
        for (int i = 0; i < kIterations; ++i) {
            counter.increment();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    // 期望 400000，实际上几乎不可能得到这个值
    std::cout << "Final count: " << counter.get() << "\n";
    return 0;
}
```

The problem with this code is obvious—`count_++` is not an atomic operation, so four threads incrementing it simultaneously will lose data. But the issue is that without TSan, you only see "incorrect results" (e.g., outputting 287541 instead of 400000), and you can't be sure if this is a data race or a logic error. With TSan added:

```bash
clang++ -fsanitize=thread -g -O1 -pthread counter.cpp -o counter
./counter
```

TSan's output looks roughly like this (exact line numbers will vary based on your code):

```text
==================
WARNING: ThreadSanitizer: data race (pid=12345)
  Write of size 4 at 0x7b0c00000000 by thread T2:
    #0 ThreadSafeCounter::increment() counter.cpp:10:9 (counter+0x4a2b)
    #1 main::$_0::operator()() const counter.cpp:24:13 (counter+0x4a03)

  Previous write of size 4 at 0x7b0c00000000 by thread T1:
    #0 ThreadSafeCounter::increment() counter.cpp:10:9 (counter+0x4a2b)
    #1 main::$_0::operator()() const counter.cpp:24:13 (counter+0x4a03)

  Location is stack of main thread.

  Thread T2 (tid=12347, running) created by main thread at:
    #0 pthread_create <null> (counter+0x42278)
    #1 main counter.cpp:28:23 (counter+0x4b0e)

  Thread T1 (tid=12346, finished) created by main thread at:
    #0 pthread_create <null> (counter+0x42278)
    #1 main counter.cpp:28:23 (counter+0x4b0e)
SUMMARY: ThreadSanitizer: data race counter.cpp:10:9 in ThreadSafeCounter::increment()
==================
Final count: 287541
```

Let's break down this report. The very first line, `WARNING: ThreadSanitizer: data race`, tells you this is a data race. Then it gives the two conflicting accesses: one is a write by thread T2 (`Write of size 4`), occurring at `counter.cpp:10:9`, which is the line `count_++`. The other is a previous write by thread T1 (`Previous write`), occurring at the same location. This perfectly matches the standard definition of a data race—two threads writing to the same memory location simultaneously without synchronization. Finally, it tells you where the thread was created (`main counter.cpp:28:23`), helping you trace the entire call chain.

The fix is simple—use `std::atomic<int>` or add a mutex:

```cpp
#include <atomic>

class ThreadSafeCounter {
public:
    void increment()
    {
        // 使用 atomic，data race 消失
        count_.fetch_add(1, std::memory_order_relaxed);
    }

    int get() const
    {
        return count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> count_{0};
};
```

Recompile and run, TSan no longer reports any issues, and the output stably hits 400000.

### Limitations of TSan

TSan is powerful, but it's not a silver bullet. We must be clear about its limitations.

First, the performance overhead is significant. TSan's typical overhead is a 5-15x runtime slowdown and a 5-10x memory overhead. This means you can't run TSan in production—it's strictly for testing and CI. The good news is you don't need to run it in production, because TSan detects code logic issues, not runtime environment issues.

Second, TSan can only detect data races on code paths that are **actually executed** during your tests. If your test coverage is insufficient, some races might never be triggered. So when using TSan, your concurrent tests need to cover various thread interleaving scenarios as much as possible—for example, running multiple rounds with different thread counts and different task granularities.

There is also an easily overlooked issue: TSan has limited recognition of custom synchronization mechanisms. If you implement a spinlock or barrier based on `std::atomic` yourself but don't use TSan's annotation interfaces (`__tsan_acquire` / `__tsan_release`), TSan might produce false positives (treating your custom synchronization as no synchronization) or false negatives. For standard `std::mutex`, `std::atomic`, `std::condition_variable`, etc., TSan can recognize them correctly; but if you have custom synchronization primitives, extra handling is required.

> ⚠️ **Warning**: TSan and ASan cannot be enabled simultaneously. If your project already uses ASan for memory error detection, you need to build a separate TSan version. The common practice is to run two sets of tests in CI—one with ASan, one with TSan.

## Helgrind: Valgrind's Thread Error Detector

### How It Works and Usage

Helgrind is a thread error detector in the Valgrind toolset. Unlike TSan's compile-time instrumentation, Valgrind uses dynamic binary instrumentation (DBI)—it doesn't require recompiling your program, but instead dynamically analyzes every instruction at runtime.

Helgrind uses happens-before-based lockset analysis. It tracks all pthread synchronization operations in the program (mutex lock/unlock, thread create/join, condition variable signal/wait) to build a happens-before relationship graph between threads. At the same time, it maintains a "lockset" (the set of currently held locks) for each thread, and checks on every memory access: if two threads access the same memory location and the intersection of their locksets is empty (meaning no shared lock protection), it reports a potential data race.

Additionally, Helgrind builds a "lock order graph." If it observes lock A being acquired before lock B (forming an edge A -> B), and later observes the order B -> A in another thread, a cycle appears in the graph—this is a potential dead lock.

Using Helgrind requires no recompilation—just run it directly:

```bash
valgrind --tool=helgrind ./your_program
```

If your program takes command-line arguments, just append them at the end:

```bash
valgrind --tool=helgrind ./your_program --arg1 --arg2
```

### Hands-on: Lock Order Errors

Let's look at a classic lock order problem—two threads acquiring two locks in different orders, which is a breeding ground for dead locks.

```cpp
#include <mutex>
#include <thread>
#include <iostream>

class BankAccount {
public:
    explicit BankAccount(int balance) : balance_(balance) {}

    void transfer_from(BankAccount& other, int amount)
    {
        // 先锁自己，再锁对方
        std::lock_guard<std::mutex> lk1(mutex_);
        std::lock_guard<std::mutex> lk2(other.mutex_);

        if (other.balance_ >= amount) {
            other.balance_ -= amount;
            balance_ += amount;
        }
    }

    int get_balance() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return balance_;
    }

private:
    mutable std::mutex mutex_;
    int balance_;
};

int main()
{
    BankAccount alice(1000);
    BankAccount bob(1000);

    // alice 向 bob 转 100
    std::thread t1([&]() {
        for (int i = 0; i < 100; ++i) {
            alice.transfer_from(bob, 1);
        }
    });

    // bob 向 alice 转 100
    std::thread t2([&]() {
        for (int i = 0; i < 100; ++i) {
            bob.transfer_from(alice, 1);
        }
    });

    t1.join();
    t2.join();

    std::cout << "Alice: " << alice.get_balance()
              << ", Bob: " << bob.get_balance() << "\n";
    return 0;
}
```

This program has a probability of dead locking: t1 locks alice then bob, while t2 locks bob then alice. If t1 locks alice while t2 locks bob simultaneously, both are waiting for the other to release—a classic dead lock. Let's run it with Helgrind:

```bash
g++ -g -O1 -pthread transfer.cpp -o transfer
valgrind --tool=helgrind ./transfer
```

Helgrind will output a report similar to this:

```text
---Thread-Announcement ---
Thread #1 is the program's root thread

---Thread-Announcement ---
Thread #2 was created
   at 0x4C3A0E3: pthread_create (helgrind_intercepts.c:xxx)
   by 0x401234: main (transfer.cpp:38)

---Thread-Announcement ---
Thread #3 was created
   ...

--- Lock order violation ---
Possible data race during lock order check
  Lock #1 (0x....) locked at
     ...
     by 0x4011A0: BankAccount::transfer_from (transfer.cpp:13)
  Lock #2 (0x....) locked at
     ...
     by 0x4011C8: BankAccount::transfer_from (transfer.cpp:14)
  Lock #2 (0x....) previously locked at
     ...
     by 0x401208: main::$_1::operator() (transfer.cpp:44)
  Lock #1 (0x....) previously locked at
     ...
     by 0x401208: main_$_1::operator() (transfer.cpp:44)

  This indicates that the locking order is inconsistent.
```

Helgrind explicitly tells you: the lock acquisition order is inconsistent. One path is #1 then #2 (`transfer.cpp:13-14`), and the other path is #2 then #1. The fix is to use `std::lock` to acquire both locks simultaneously—it internally uses a try-and-back-off algorithm to avoid dead locks:

```cpp
void transfer_from(BankAccount& other, int amount)
{
    // std::lock 同时获取两把锁，避免死锁
    std::lock(mutex_, other.mutex_);
    std::lock_guard<std::mutex> lk1(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other.mutex_, std::adopt_lock);

    if (other.balance_ >= amount) {
        other.balance_ -= amount;
        balance_ += amount;
    }
}
```

### TSan vs Helgrind: How to Choose?

These two tools have quite a bit of functional overlap, but each has its own focus.

TSan uses compile-time instrumentation, requiring recompilation but with relatively lower runtime overhead (though still a 5-15x slowdown). It has the best support for C++ standard library synchronization primitives, and its report format is clear and readable. If you can recompile the project, TSan is usually the first choice—its data race detection is more precise, with a lower false-positive rate.

Helgrind uses runtime dynamic analysis, requiring no recompilation (as long as debug symbols are present), but its runtime overhead is larger than TSan (typically 20-50x slowdown) because every instruction must be translated through Valgrind's IR. Helgrind's advantage is that you can directly analyze an already-compiled binary without setting up a build environment. Furthermore, Helgrind is particularly strong at lock order analysis—if you suspect a dead lock risk but it hasn't been triggered yet, Helgrind's lock order graph can help you discover the hidden danger in advance.

My recommendation is: use TSan for daily development to quickly detect data races; bring in Helgrind when you need to analyze lock order issues or cannot recompile. The two complement each other, so there's no need to choose just one.

## Compile-Time Defense: Clang Thread Safety Analysis

TSan and Helgrind are both runtime tools—you need to let the bug happen before they can detect it. But there is a class of problems that can be prevented at compile time. Clang's Thread Safety Analysis (TSA) is a compile-time static analysis extension that declares thread safety constraints through code annotations, and the compiler checks whether you violate these constraints at compile time. Zero runtime overhead, zero performance impact—it works entirely at compile time.

### Basic Annotations

The core concept of TSA is "capability." A mutex is a capability—you must hold it to access the data it protects. You need to use macros (which are `__attribute__` under the hood) to declare these constraints.

First, you need to add the `CAPABILITY` annotation to your mutex type:

```cpp
// 为标准库 mutex 包装一个带注解的类型
class CAPABILITY("mutex") Mutex {
public:
    void lock() ACQUIRE() { mu_.lock(); }
    void unlock() RELEASE() { mu_.unlock(); }
    bool try_lock() TRY_ACQUIRE(true) { return mu_.try_lock(); }

private:
    std::mutex mu_;
};

// RAII 守卫也需要注解
class SCOPED_CAPABILITY MutexGuard {
public:
    explicit MutexGuard(Mutex& m) ACQUIRE(m) : mu_(m) { mu_.lock(); }
    ~MutexGuard() RELEASE() { mu_.unlock(); }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    Mutex& mu_;
};
```

Then, you can use `GUARDED_BY` to declare which mutex protects a data member, and `REQUIRES` to declare that a function requires a specific lock to be acquired before calling:

```cpp
class ThreadSafeQueue {
public:
    void push(int value)
    {
        MutexGuard lk(mutex_);   // 获取锁
        data_.push_back(value);  // OK，锁已持有
    }

    int pop()
    {
        MutexGuard lk(mutex_);
        int val = data_.front();  // OK
        data_.pop_front();
        return val;
    }

    // 危险！跳过锁直接读
    int unsafe_front()
    {
        return data_.front();  // 编译警告！
    }

    // 声明需要调用者持有锁
    int front_locked() REQUIRES(mutex_)
    {
        return data_.front();  // OK，调用者保证持锁
    }

private:
    mutable Mutex mutex_;
    std::deque<int> data_ GUARDED_BY(mutex_);
};
```

When compiling with `-Wthread-safety`, `unsafe_front()` will trigger a compiler warning because it accesses `data_`, which is protected by `GUARDED_BY(mutex_)`, without holding `mutex_`. Meanwhile, `front_locked()` has the `REQUIRES(mutex_)` annotation, so the compiler knows it requires the caller to hold the lock and that internally accessing `data_` is safe—if someone calls `front_locked()` without the lock, the warning will appear on the caller's side.

### Lock Order Annotations

TSA also supports declaring lock acquisition orders to prevent dead locks:

```cpp
class NetworkManager {
private:
    Mutex stats_mutex_ ACQUIRED_AFTER(data_mutex_);
    Mutex data_mutex_;

    std::vector<int> data_ GUARDED_BY(data_mutex_);
    int total_bytes_ GUARDED_BY(stats_mutex_);
};
```

If you lock `data_mutex_` then `stats_mutex_` somewhere, no problem—this matches the declared order. But if you do it in reverse, locking `stats_mutex_` then `data_mutex_`, the compiler will warn.

Enabling it is very simple:

```bash
clang++ -Wthread-safety -c your_file.cpp
```

> ⚠️ **Warning**: TSA is purely static analysis and cannot replace runtime tools. It can only check constraints you have annotated; it completely ignores code without annotations. Moreover, TSA is currently a Clang-exclusive extension—GCC and MSVC do not support it. But if you build with Clang, adding annotations to key data structures and letting the compiler enforce them can save you a tremendous amount of debugging time.

## Runtime Diagnosis of Dead Locks

TSA can prevent some dead locks at compile time, but if your program is already frozen, you need runtime diagnostic methods.

### GDB: The Most Direct Approach

When a program dead locks, the most direct approach is to attach GDB to the process and inspect the call stacks of all threads:

```bash
# 找到你的进程 PID
ps aux | grep your_program

# GDB 附加
gdb -p <PID>

# 在 GDB 中：查看所有线程的调用栈
(gdb) thread apply all bt
```

You will see output similar to this:

```text
Thread 3 (Thread 0x7f... "your_program"):
#0  __lll_lock_wait (futex=..., private=0) at lowlevellock.c:52
#1  __pthread_mutex_lock (mutex=...) at pthread_mutex_lock.c:67
#2  BankAccount::transfer_from (this=..., other=..., amount=1) at transfer.cpp:13
#3  ...

Thread 2 (Thread 0x7f... "your_program"):
#0  __lll_lock_wait (futex=..., private=0) at lowlevellock.c:52
#1  __pthread_mutex_lock (mutex=...) at pthread_mutex_lock.c:67
#2  BankAccount::transfer_from (this=..., other=..., amount=1) at transfer.cpp:13
#3  ...
```

Both threads are stuck in `__lll_lock_wait` (the kernel wait for a mutex), and both are at line 13 of `transfer_from`—this is ironclad proof of a dead lock. Based on the call stacks, you can deduce the lock acquisition order and fix it.

### Assisting with GDB Python Scripts

For complex projects, manually reading `thread apply all bt` output is exhausting. You can write a simple GDB Python script to extract all threads waiting for locks and the mutex addresses they are waiting on:

```python
# save as deadlock_detector.py
import gdb

class DeadlockDetector(gdb.Command):
    """Detect potential deadlocks by showing all threads waiting on mutexes."""

    def __init__(self):
        super().__init__("detect-deadlock", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        threads = gdb.selected_inferior().threads()
        for thread in threads:
            thread.switch()
            frame = gdb.selected_frame()
            sal = frame.find_sal()
            try:
                # 尝试找到 __lll_lock_wait 或 pthread_mutex_lock
                func_name = frame.function().name or ""
                if "lock" in func_name.lower():
                    print(f"Thread {thread.num} waiting on lock at "
                          f"{sal.symtab.filename}:{sal.line}")
            except Exception:
                pass

DeadlockDetector()
```

After `source deadlock_detector.py` in GDB, simply type `detect-deadlock` to see all threads waiting for locks.

## Structured Logging: Making printf Reliable

When debugging concurrent programs, many people's first reaction is to add `printf` or `std::cout`. This has two serious problems.

First, `printf` and `std::cout` are not inherently thread-safe (to be precise, the C++ standard guarantees they won't cause a data race, but when multiple threads write to `std::cout` simultaneously, the output will be interleaved and garbled). You add a bunch of prints, and the output you see might be gibberish where one line is truncated by another thread's output—worse than having no logs at all.

Second, logs without timestamps and thread identifiers are almost useless. When you see two lines of output like `value = 42` and `value = 0`, you have no idea which thread wrote them, when they were written, or their chronological order.

### A Minimal Thread-Safe Logger

What we need is a thread-safe logger where every log entry includes a timestamp and thread ID. The following implementation is simple but practical:

```cpp
#include <mutex>
#include <chrono>
#include <sstream>
#include <iostream>
#include <thread>
#include <iomanip>
#include <atomic>

class ThreadSafeLogger {
public:
    static ThreadSafeLogger& instance()
    {
        static ThreadSafeLogger logger;
        return logger;
    }

    void log(const std::string& level, const std::string& message)
    {
        auto now = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch());

        // 先在局部构建完整的日志行，再一次性加锁输出
        // 这样锁的持有时间最短
        std::ostringstream oss;
        oss << "[" << std::setw(16) << ns.count() << " ns] "
            << "[" << std::this_thread::get_id() << "] "
            << "[" << level << "] "
            << message << "\n";

        std::lock_guard<std::mutex> lk(mutex_);
        std::cout << oss.str();
    }

private:
    ThreadSafeLogger() = default;
    std::mutex mutex_;
};

// 便捷宏，减少打字量
#define LOG_INFO(msg)  ThreadSafeLogger::instance().log("INFO", msg)
#define LOG_WARN(msg)  ThreadSafeLogger::instance().log("WARN", msg)
#define LOG_ERROR(msg) ThreadSafeLogger::instance().log("ERROR", msg)
```

The key implementation detail is this: we first build the complete log line locally using `std::ostringstream`, and then acquire the lock to output it. The benefit of this approach is that the lock hold time is extremely short (just one `std::cout << string`), reducing lock contention. If you do the formatting inside the lock, multiple threads will queue up waiting for formatting to complete, and the impact on concurrent performance is non-negligible.

Each log entry contains three key pieces of information: a nanosecond-resolution timestamp (for determining event order), a thread ID (for distinguishing the behavior of different threads), and a log level. With this information, you can precisely trace each thread's timeline when analyzing concurrent bugs.

Using it is very simple:

```cpp
ThreadSafeLogger::instance().log("INFO",
    "Acquired mutex for account " + std::to_string(account_id));
```

The output looks like this:

```text
[  123456789012345 ns] [140234567890] [INFO] Acquired mutex for account 42
[  123456789045678 ns] [140234567891] [INFO] Acquired mutex for account 17
```

From the timestamps and thread IDs, you can clearly see that two threads acquired different mutexes almost simultaneously—if they subsequently acquire the second mutex in reverse order, you've found the root cause of the dead lock.

> ⚠️ **Warning**: This logger uses `std::cout` as the underlying output. If your program requires high-performance logging (e.g., millions of entries per second), this implementation won't suffice—you'll need to switch to a lock-free ring buffer solution or use an existing logging library (like spdlog). But for the debugging phase, it is completely adequate.

## Systematic Diagnostic Workflow

Alright, we've now introduced four main tools—TSan, Helgrind, Clang TSA, and structured logging. The question is, when you encounter a concurrent bug in a real project, what order should you use these tools in? Based on my own hard-earned experience, I've summarized a workflow.

When you discover a suspected concurrent bug, the first step is always to **reproduce it as stably as possible**. This is the hardest and most critical step. You need to record all conditions that trigger the bug: input data, thread count, system load, and even hardware model. If the bug only appears under high concurrency, write a stress test and run it repeatedly; if it only appears under specific data, preserve that data. A bug that cannot be stably reproduced is almost impossible to fix—because you cannot verify whether your fix is effective. If stable reproduction is truly impossible, consider adding a loop test in CI—run the same test 1000 times, and if it fails even once, count it as a failure.

After reproduction, the second step is to **determine the bug's category**. Is it a data race, dead lock, livelock, or dangling reference? If the program outputs incorrect results but doesn't crash, it's most likely a data race. If the program freezes completely, it might be a dead lock. If CPU is at 100% with no output, it might be a livelock. If it's a segfault and the stack trace shows weird addresses, it might be a dangling reference. This classification determines which tool you use next.

The third step is to **select and run the tool**. If it's a data race, compile a TSan version and run it once. If it's a dead lock risk, use Helgrind's lock order analysis. If it's an already-dead-locked process, attach GDB to inspect all thread stacks. If it's a dangling reference, ASan is more appropriate (although this chapter mainly covers concurrency tools, ASan's detection of use-after-free is extremely precise).

The fourth step is to **analyze the tool's report**. TSan's report will tell you exactly which line of code has the problem and which threads are conflicting. Helgrind will tell you where the lock acquisition order is inconsistent. GDB will tell you where each thread is stuck. Read the report carefully—don't rush to modify the code; first make sure you understand the root cause of the problem.

The fifth step is to **fix and verify**. After the fix, rerun TSan/Helgrind to confirm the report is gone, and rerun your reproduction test to confirm the bug no longer appears. If possible, add a TSan build as a permanent check in CI to prevent similar issues from being introduced again.

This workflow looks simple, but there are pitfalls in every step. The most common mistake is skipping "reproduction" and going straight to reading code to guess the bug's location—in concurrent programs, the location you guess is probably wrong, because the root cause of a concurrent bug is often in a seemingly unrelated code path. Another common mistake is not running TSan to verify after a fix—you think you've fixed it, but you might have just changed the timing to make the bug appear less frequently, rather than fundamentally eliminating it.

## Where We Are

In this chapter, we built a toolbox and methodology for concurrent debugging. TSan captures data races at runtime through compile-time instrumentation, Helgrind detects lock order issues and races through dynamic analysis, Clang TSA prevents thread safety violations at compile time through annotations, GDB provides a crash scene snapshot when the program dead locks, and structured logging helps us track event timelines during debugging. These tools each have their own focus, and using them in combination covers the vast majority of concurrent bug scenarios.

But "correctness" is only half of concurrent programming. A bug-free concurrent program is not necessarily an efficient one—you might spend a week optimizing a mutex, only to find the bottleneck isn't there at all; or you might introduce code so complex it's unmaintainable, all in the pursuit of lock-free performance. In the next chapter, we will discuss how to scientifically measure the performance of concurrent programs: Google Benchmark's multi-threading usage, common pitfalls in concurrent benchmark design, and performance counter analysis with the perf tool. Debugging tells us "what's wrong," and benchmarking tells us "what's slow"—combining the two forms a complete concurrent engineering capability.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch08-debug-testing-perf/`.

## References

- [ThreadSanitizer — LLVM Documentation](https://clang.llvm.org/docs/ThreadSanitizer.html) — Official TSan documentation, covering usage, limitations, and configuration options
- [Dynamic Race Detection with LLVM Compiler — Google Research](https://research.google.com/pubs/archive/37278.pdf) — The original TSan-LLVM paper, detailing the hybrid detection algorithm
- [Helgrind: an experimental thread error detector — Valgrind Manual](https://valgrind.org/docs/manual/hg-manual.html) — Official Helgrind manual, including lock order analysis and annotation API
- [Thread Safety Analysis — Clang Documentation](https://clang.llvm.org/docs/ThreadSafetyAnalysis.html) — Complete reference for Clang TSA, including usage of all annotations
- [Thread Safety Analysis in C and C++ — CERT/SEI (CMU)](https://www.sei.cmu.edu/blog/thread-safety-analysis-in-c-and-c/) — The design philosophy and industrial application behind TSA
- [C/C++ Thread Safety Analysis — Google Research (PDF)](https://research.google.com/pubs/archive/42958.pdf) — The original TSA paper by Hutchins et al.
