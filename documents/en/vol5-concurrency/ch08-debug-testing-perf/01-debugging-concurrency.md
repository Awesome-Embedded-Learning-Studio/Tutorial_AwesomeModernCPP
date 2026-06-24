---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the usage of tools such as ThreadSanitizer and Helgrind, and establish
  a systematic diagnostic workflow for concurrency bugs.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- mutex 与 RAII 锁
- 原子操作
- 线程安全队列
reading_time_minutes: 26
related:
- 并发性能测试与基准
tags:
- host
- cpp-modern
- intermediate
- 进阶
title: Debugging Techniques for Concurrent Programs
translation:
  source: documents/vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md
  source_hash: 7e0f01ada965026c692c5f535a3350c843dbc82f5438bdcf8eae91dc198b5c0f
  translated_at: '2026-06-24T01:08:43.189438+00:00'
  engine: anthropic
  token_count: 5009
---
# Debugging Concurrent Programs

Honestly, only those who have stepped into the traps themselves can truly understand the pain of debugging concurrent programs. Bugs in single-threaded programs are at least deterministic—given the same input, they will consistently crash in the same place in the same way. Concurrent bugs are not like that. A data race might appear only once in ten thousand runs, and a deadlock might only trigger under a very specific thread scheduling sequence. It is always the case that "it works fine on your machine, but fails on CI." I once spent two full days on a data race, only to find it was caused by a lambda capturing a reference to a local variable. You cannot spot this bug just by reading the code, because it is completely correct under a single-threaded execution path.

In this article, we will establish a systematic concurrent debugging methodology. Not the "add a print and see" kind, but a process starting from understanding the classification characteristics of bugs, to selecting the right tools, to interpreting tool reports, and finally forming a reproducible and verifiable fix workflow. We will focus on ThreadSanitizer (TSan), Valgrind's Helgrind tool, Clang's compile-time thread safety analysis, and a practical structured logging solution.

## Environment Setup

All commands and code in this article have been tested in the following environment: Ubuntu 22.04 LTS (WSL2 is also acceptable), using Clang 16+ or GCC 12+ (TSan support required), Valgrind 3.18 or higher (installable via `apt install valgrind`), and GDB 12+ as the debugger. If you use CMake to manage your project, version 3.20 or higher is required. If your distribution is older, the TSan report format might differ slightly, but the core content remains consistent.

## The Four Major Factions of Concurrent Bugs

Before using tools, we need to understand the broad categories of concurrent bugs, because different types require completely different diagnostic strategies.

**Data race** is the most common and insidious type. Its definition is strict: two or more threads access the same memory location simultaneously, at least one is a write, and there is no synchronization between them (no mutex, no atomic, no happens-before). The C++ standard explicitly defines data races as undefined behavior (UB)—not "might go wrong," but "anything can happen," including but not limited to reading garbage values, program crashes, or appearing to "work normally" and then suddenly exploding one day. Data races are hard to track because they depend on the thread scheduling order, which might be completely different when you are debugging versus in production. You add a `printf` for debugging, and the printing itself changes the timing, causing the bug to disappear—this is the classic "Heisenbug."

**Deadlock** is another major category. Two or more threads wait for resources held by each other, neither yielding, and the program freezes completely. Deadlocks are actually more deterministic than data races—as long as the specific lock acquisition order is triggered, it will inevitably happen. The problem is that the trigger conditions can be very complex, involving combinations of specific execution paths across multiple threads. Furthermore, deadlocks often do not appear under normal load, only exposing themselves under specific concurrency patterns.

**Livelock** is more subtle than a deadlock. The threads are not stuck—CPU usage might be 100%—but no meaningful progress is made. A classic example is two threads politely yielding resources to each other, resulting in neither acquiring them. The symptom of a livelock is a slow program rather than a frozen one, easily misdiagnosed as a performance issue.

Finally, there is the **dangling reference**. A thread accesses an object that has gone out of scope via a reference or pointer—this is particularly common in asynchronous programming. For example, you start a thread, pass in a reference to a local variable, then the function returns, the local variable is destroyed, but the thread is still using that reference. The manifestation of this bug depends on what that memory is reallocated for—you might read a "seemingly normal but actually wrong" value, or you might get a direct segfault.

| Bug Type | Core Characteristic | Reproduction Difficulty | Typical Signal |
|----------|---------------------|--------------------------|----------------|
| Data race | Unsynchronized concurrent read/write | Extremely high (timing dependent) | Intermittent incorrect results, Heisenbug |
| Deadlock | Circular wait for resources | Medium-high (path dependent) | Program completely frozen |
| Livelock | Repeated yielding without progress | Medium | CPU 100% but no output |
| Dangling reference | Accessing destroyed object | High (memory state dependent) | Intermittent crashes, garbage values |

## ThreadSanitizer: The Data Race Slayer

### Principle: Compiler Instrumentation

ThreadSanitizer (TSan) works by instrumenting your code at compile time. When you add the `-fsanitize=thread` compiler flag, the compiler inserts extra check code before and after every memory access (read and write). At runtime, this check code maintains a "shadow memory" that records the access history and synchronization events for each memory location.

TSan uses a hybrid algorithm based on happens-before relationships and lockset analysis. Simply put, it tracks the thread ID and a logical timestamp (vector clock) for every memory access, while also tracking which mutexes are held by the current thread. If it finds two memory accesses from different threads that lack a happens-before relationship (meaning there is no synchronization operation between them), and at least one is a write, it reports a data race. The theoretical basis of this algorithm guarantees that if a data race actually occurs during your test execution (even if only once), the algorithm can detect it. However, note that TSan's implementation maintains a finite-sized history buffer for every 8-byte memory location. In extreme cases (e.g., many threads frequently accessing the same address causing old records to be evicted), the actual false negative rate is very low but not zero. For the vast majority of real-world scenarios, you can treat "TSan reports nothing" as a strong signal that "there is indeed no data race on this execution path."

### Enabling TSan

Enabling TSan is very simple; just add the corresponding flag at compile time:

```bash
# 编译时加上 -fsanitize=thread 和调试信息
clang++ -fsanitize=thread -g -O1 -pthread your_program.cpp -o your_program

# 或者 GCC
g++ -fsanitize=thread -g -O1 -pthread your_program.cpp -o your_program
```

There are a few points to keep in mind here. First, `-g` must be included; otherwise, the TSan report will contain only addresses without source code locations, making it difficult to pinpoint the issue. Second, we recommend using `-O1` or higher, primarily for performance—TSan already introduces a five to 15 times slowdown, and unoptimized code with `-O0` makes the overhead even worse. However, avoid `-O2` or higher, as aggressive optimizations may inline too many functions, making the stack trace difficult to read. Third, TSan does not support running simultaneously with AddressSanitizer (ASan). If both are enabled in your build script, the compiler will throw an error.

If you use CMake, you can configure it like this:

```cmake
# CMakeLists.txt 中启用 TSan
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
endif()
```

Then, simply run `cmake -DENABLE_TSAN=ON ..`.

### Hands-on: Diagnosing a Data Race

Let's examine a classic data race scenario and use TSan to catch it step by step.

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

The problem with this code is obvious—`count_++` is not an atomic operation, so incrementing it from four threads simultaneously will result in data loss. However, without TSan, you would simply see "incorrect results" (for example, an output of 287541 instead of 400000), making it impossible to determine if this is a data race or a logic error. With TSan enabled:

```bash
clang++ -fsanitize=thread -g -O1 -pthread counter.cpp -o counter
./counter
```

The output from TSan looks roughly like this (the specific line numbers will vary depending on your code):

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

Let's break down this report. The top line `WARNING: ThreadSanitizer: data race` indicates a data race. It then details two conflicting accesses: one is a write (`Write of size 4`) in thread T2 at `counter.cpp:10:9`, which is the line `count_++`. The other is a previous write (`Previous write`) by thread T1 at the same location. This fits the standard definition of a data race—two threads writing to the same memory location simultaneously without synchronization. Finally, it shows where the threads were created (`main counter.cpp:28:23`) to help you trace the call chain.

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

Recompile and run the application. TSan no longer reports any issues, and the output is consistently 400,000.

### Limitations of TSan

TSan is effective, but it is not a silver bullet. We must be aware of its limitations.

First, the performance overhead is significant. TSan typically incurs a slowdown of 5 to 15 times the execution speed and increases memory usage by 5 to 10 times. This means you cannot run TSan in a production environment—it is intended for testing and CI pipelines only. The good news is that you don't need to run it in production, because TSan detects code logic issues, not runtime environment issues.

Second, TSan can only detect data races on code paths that are **actually executed** during your tests. If your test coverage is insufficient, some races may never be triggered. Therefore, when using TSan, your concurrency tests should cover various thread interleaving scenarios as thoroughly as possible—for example, by running multiple rounds with different thread counts and task granularities.

There is also an easily overlooked issue: TSan has limited recognition of custom synchronization mechanisms. If you implement a custom spinlock or barrier based on `std::atomic` but do not use the annotation interfaces provided by TSan (`__tsan_acquire` / `__tsan_release`), TSan may produce false positives (treating your custom synchronization as no synchronization at all) or false negatives. TSan correctly identifies standard primitives like `std::mutex`, `std::atomic`, and `std::condition_variable`, but if you have custom synchronization primitives, they require extra handling.

> ⚠️ **Note**: TSan and ASan cannot be enabled simultaneously. If your project already uses ASan for memory error detection, you need to build a separate version for TSan. The common practice is to run two separate test suites in CI—one with ASan and one with TSan.

## Helgrind: Thread Error Detection with Valgrind

### Principles and Usage

Helgrind is a thread error detector within the Valgrind toolset. Unlike TSan's compile-time instrumentation, Valgrind uses dynamic binary instrumentation (DBI)—it does not require recompiling your program, but instead dynamically analyzes every instruction at runtime.

Helgrind utilizes happens-before lockset analysis. It tracks all pthread synchronization operations in the program (mutex lock/unlock, thread create/join, condition variable signal/wait) to build a happens-before relationship graph between threads. Simultaneously, it maintains a "lock set" (the set of locks currently held) for each thread and checks every memory access: if two threads access the same memory location and the intersection of their lock sets is empty (meaning no common lock protects the access), it reports a potential data race.

Furthermore, Helgrind builds a "lock order graph." If it observes lock A being acquired before lock B (forming an edge A -> B), and later observes the order B -> A in another thread, a cycle appears in the graph—indicating a potential deadlock.

Using Helgrind does not require recompilation; you can run it directly:

```bash
valgrind --tool=helgrind ./your_program
```

If your program accepts command line arguments, just append them to the end:

```bash
valgrind --tool=helgrind ./your_program --arg1 --arg2
```

### Hands-on: Lock Ordering Errors

Let's look at a classic lock ordering problem—two threads acquiring two locks in different orders, which is a breeding ground for deadlocks.

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

This program has a probability of deadlocking: `t1` locks `alice` then `bob`, while `t2` locks `bob` then `alice`. If `t1` locks `alice` while `t2` locks `bob`, both are waiting for the other to release—a classic deadlock. Let's run it with Helgrind:

```bash
g++ -g -O1 -pthread transfer.cpp -o transfer
valgrind --tool=helgrind ./transfer
```

Helgrind outputs a report similar to this:

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

Helgrind explicitly indicates an inconsistent lock acquisition order. One path acquires lock #1 followed by #2 (`transfer.cpp:13-14`), while the other path acquires #2 followed by #1. We can fix this by using `std::lock` to acquire both locks simultaneously, as it employs a try-and-backoff algorithm internally to prevent deadlocks:

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

### TSan vs. Helgrind: Which One Should We Choose?

These two tools have overlapping functionalities, but they focus on different areas.

TSan performs compile-time instrumentation. It requires recompilation, but the runtime overhead is relatively small (though it still causes a 5-15x slowdown). It offers the best support for C++ standard library synchronization primitives and provides clear, readable reports. If you can recompile your project, TSan is usually the preferred choice—it detects data races more accurately and has a lower false positive rate.

Helgrind is a runtime dynamic analysis tool. It does not require recompilation (as long as debug symbols are present), but the runtime overhead is significantly higher than TSan (typically a 20-50x slowdown) because every instruction must be translated by Valgrind's IR. Helgrind's advantage is that you can directly analyze an already compiled binary without needing to set up a build environment. Additionally, Helgrind is particularly strong at analyzing lock order—if you suspect a deadlock risk but haven't triggered it yet, Helgrind's lock order graph can help you discover potential hazards in advance.

Our recommendation is to use TSan for daily development to quickly detect data races. When you need to analyze lock order issues or cannot recompile, bring in Helgrind. The two can complement each other; you don't have to choose just one.

## Compile-time Defense: Clang Thread Safety Analysis

TSan and Helgrind are both runtime tools—you need to let the bug occur before they can detect it. However, there is a class of problems that can be prevented at compile time. Clang's Thread Safety Analysis (TSA) is a compile-time static analysis extension. It uses code annotations to declare thread safety constraints, and the compiler checks if you violate these constraints during compilation. Zero runtime overhead, zero performance impact—it works entirely at compile time.

### Basic Annotations

The core concept of TSA is "capabilities." A mutex is a type of capability—you must hold it to access the data it protects. You need to use macros (underlying `__attribute__`) to declare these constraints.

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

Then, we can use `GUARDED_BY` to declare which mutex protects a data member, and `REQUIRES` to declare which lock must be acquired before calling a function:

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

When compiling with `-Wthread-safety`, `unsafe_front()` triggers a compiler warning because it accesses `data_`, which is protected by `GUARDED_BY(mutex_)`, without holding `mutex_`. On the other hand, `front_locked()` is annotated with `REQUIRES(mutex_)`, so the compiler knows that the caller must hold the lock and that the internal access to `data_` is safe—if someone calls `front_locked()` without the lock, the warning will appear at the call site.

### Lock Order Annotations

TSA also supports declaring lock acquisition order to prevent deadlocks:

```cpp
class NetworkManager {
private:
    Mutex stats_mutex_ ACQUIRED_AFTER(data_mutex_);
    Mutex data_mutex_;

    std::vector<int> data_ GUARDED_BY(data_mutex_);
    int total_bytes_ GUARDED_BY(stats_mutex_);
};
```

If we lock `data_mutex_` first and then `stats_mutex_` somewhere, that is fine—it follows the declared order. However, if we reverse the order, locking `stats_mutex_` first and then `data_mutex_`, the compiler will issue a warning.

Enabling this is straightforward:

```bash
clang++ -Wthread-safety -c your_file.cpp
```

> ⚠️ **Note**: TSA is purely a static analysis tool; it cannot replace runtime tools. It only checks constraints that you have annotated, and it completely ignores code without annotations. Furthermore, TSA is currently a Clang-specific extension and is not supported by GCC or MSVC. However, if you build with Clang, adding annotations to critical data structures and letting the compiler enforce the rules can save you significant debugging time.

## Runtime Diagnosis of Deadlocks

TSA can prevent some deadlocks at compile time, but if your program has already frozen, you need runtime diagnostic methods.

### GDB: The Most Direct Approach

When a program deadlocks, the most direct method is to attach GDB to the process and inspect the call stacks of all threads:

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

Both threads are stuck in `__lll_lock_wait` (the kernel wait for a mutex), and both are at line 13 of `transfer_from` — this is conclusive evidence of a deadlock. Based on the backtrace, we can deduce the lock acquisition order and fix it.

### GDB Python Scripting Assistance

For complex projects, manually parsing the output of `thread apply all bt` is tedious. We can write a simple GDB Python script to extract all threads waiting on locks and the addresses of the mutexes they are waiting for:

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

After running `source deadlock_detector.py` in GDB, simply typing `detect-deadlock` reveals all threads waiting for locks.

## Structured Logging: Making `printf` Reliable

When debugging concurrent programs, many people's first reaction is to add `printf` or `std::cout`. This has two serious problems.

First, `printf` and `std::cout` are not thread-safe by default (to be precise, the C++ standard guarantees they won't cause data races, but output from multiple threads writing to `std::cout` simultaneously will be interleaved and chaotic). You might add a bunch of prints, only to see output where one line is truncated by another thread's output—garbled text that is worse than having no logs at all.

Second, logs without timestamps and thread IDs are practically useless. When you see two lines of output `value = 42` and `value = 0`, you have no idea which thread wrote them or when, nor do you know their sequential order.

### A Minimal Thread-Safe Logger

What we need is a logger that is thread-safe and includes a timestamp and thread ID for every log entry. The following implementation is simple yet practical:

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

The key implementation detail is that we first construct the complete log line locally using `std::ostringstream`, and then lock it for output. The benefit of this approach is that the lock is held for a very short time (only for a single `std::cout << string`), which reduces lock contention. If you perform formatting inside the lock, multiple threads will queue up waiting for formatting to complete, which has a non-negligible impact on concurrent performance.

Each log entry contains three key pieces of information: a nanosecond-resolution timestamp (to determine event ordering), a thread ID (to distinguish the behavior of different threads), and a log level. With this information, we can precisely track the timeline of each thread when analyzing concurrency bugs.

Using it is simple:

```cpp
ThreadSafeLogger::instance().log("INFO",
    "Acquired mutex for account " + std::to_string(account_id));
```

```text
Output format:
```

```text
[  123456789012345 ns] [140234567890] [INFO] Acquired mutex for account 42
[  123456789045678 ns] [140234567891] [INFO] Acquired mutex for account 17
```

From the timestamps and thread IDs, we can clearly see that two threads acquired different mutexes almost simultaneously. If they subsequently attempt to acquire the second mutex in reverse order, we have identified the root cause of the deadlock.

> ⚠️ **Note**: This logger uses `std::cout` for the underlying output. If your program requires high-performance logging (e.g., millions of lines per second), this implementation will not suffice—you will need to switch to a lock-free ring buffer solution or use an existing logging library (like spdlog). However, it is fully adequate for the debugging phase.

## Systematic Diagnostic Process

Okay, we have now covered four major tools—TSan, Helgrind, Clang TSA, and structured logging. The question is, when you encounter a concurrency bug in an actual project, in what order should you use these tools? Based on my experience, I have summarized a workflow.

When you discover a suspected concurrency bug, the first step is always to **reproduce it as stably as possible**. This is the hardest but most critical step. You need to record all conditions that trigger the bug: input data, number of threads, system load, and even hardware model. If the bug only appears under high concurrency, write a stress test and run it repeatedly; if it only appears with specific data, keep that data. A bug that cannot be stably reproduced is almost impossible to fix—because you cannot verify if your fix is effective. If stable reproduction is truly impossible, consider adding a loop test in your CI—run the same test 1000 times, and count it as a failure if it fails even once.

After reproduction, the second step is to **determine the category of the bug**. Is it a data race, deadlock, livelock, or dangling reference? If the program outputs incorrect results but does not crash, it is likely a data race. If the program freezes, it might be a deadlock. If the CPU usage is 100% but there is no output, it might be a livelock. If it is a segmentation fault and the stack trace contains strange addresses, it might be a dangling reference. This classification determines which tool you use next.

The third step is to **select and run the tool**. If it is a data race, compile a TSan version and run it. If there is a risk of deadlock, use Helgrind's lock order analysis. If the process is already deadlocked, use GDB to attach and inspect all thread stacks. If it is a dangling reference, ASan is more appropriate (although this article focuses on concurrency tools, ASan is very precise at detecting use-after-free).

The fourth step is to **analyze the tool's report**. TSan's report will precisely tell you which line of code is problematic and which threads are conflicting. Helgrind will tell you where the lock acquisition order is inconsistent. GDB will tell you where each thread is stuck. Read the reports carefully—do not rush to modify the code; first, ensure you understand the root cause of the problem.

The fifth step is to **fix and verify**. After the fix, rerun TSan/Helgrind to confirm the reports are gone, and rerun your reproduction test to confirm the bug no longer appears. If possible, add a TSan build to your CI as a permanent check to prevent similar issues from being reintroduced.

This workflow seems simple, but there are pitfalls in every step. The most common mistake is skipping "reproduction" and reading the code directly to guess the bug's location—in concurrent programs, the location you guess is likely wrong, because the root cause of concurrency bugs often lies in seemingly unrelated code paths. Another common error is not running TSan for verification after the fix—you think you have fixed it, but you may have just changed the timing to make the bug appear less frequently, rather than eliminating it fundamentally.

## Where We Are

In this article, we have built a toolkit and methodology for concurrency debugging. TSan captures data races at runtime through compile-time instrumentation, Helgrind detects lock order issues and races through dynamic analysis, Clang TSA prevents thread safety violations at compile time using annotations, GDB provides on-site snapshots when the program deadlocks, and structured logging helps us track the timeline of events during debugging. These tools each have their focus, and using them in combination can cover the vast majority of concurrency bug scenarios.

However, "correctness" is only half of concurrent programming. A bug-free concurrent program is not necessarily an efficient one—you might spend a week optimizing a mutex, only to find the bottleneck isn't there at all; or you might introduce code that is too complex to maintain in pursuit of lock-free performance. The next article will discuss how to scientifically measure the performance of concurrent programs: multi-threaded usage of Google Benchmark, common traps in concurrent benchmark design, and performance counter analysis with the `perf` tool. Debugging tells us "where it is wrong," and benchmarking tells us "where it is slow"—combining the two makes for complete concurrent engineering capabilities.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `code/volumn_codes/vol5/ch08-debug-testing-perf/`.

## References

- [ThreadSanitizer — LLVM Documentation](https://clang.llvm.org/docs/ThreadSanitizer.html) — Official TSan documentation, covering usage, limitations, and configuration options.
- [Dynamic Race Detection with LLVM Compiler — Google Research](https://research.google.com/pubs/archive/37278.pdf) — The original paper for TSan-LLVM, detailing the hybrid detection algorithm.
- [Helgrind: an experimental thread error detector — Valgrind Manual](https://valgrind.org/docs/manual/hg-manual.html) — Official Helgrind manual, including lock order analysis and annotation APIs.
- [Thread Safety Analysis — Clang Documentation](https://clang.llvm.org/docs/ThreadSafetyAnalysis.html) — Complete reference for Clang TSA, including usage of all annotations.
- [Thread Safety Analysis in C and C++ — CERT/SEI (CMU)](https://www.sei.cmu.edu/blog/thread-safety-analysis-in-c-and-c/) — The design philosophy behind TSA and its industrial application.
- [C/C++ Thread Safety Analysis — Google Research (PDF)](https://research.google.com/pubs/archive/42958.pdf) — The original paper on TSA, by Hutchins et al.
