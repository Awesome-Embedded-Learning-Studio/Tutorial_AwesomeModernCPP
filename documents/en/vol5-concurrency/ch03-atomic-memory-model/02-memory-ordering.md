---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: From compiler reordering to CPU reordering, breaking down the six `memory_order`
  values and the happens-before relationship one by one.
difficulty: advanced
order: 2
platform: host
prerequisites:
- atomic 操作
reading_time_minutes: 16
related:
- fence 与编译器屏障
- 原子操作模式
tags:
- host
- cpp-modern
- advanced
- atomic
- memory_order
title: Memory Order Explained
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md
  source_hash: daf000fe389a45fe175cafa1f72f5dafcd40f2b83bb51d0dc03260d73dfe648b
  token_count: 2916
  translated_at: '2026-05-20T04:38:31.241349+00:00'
---
# Memory Order Explained

In the previous article, we fully broke down the operation set of `std::atomic`—load, store, fetch_add, and compare_exchange—and saw that simply using the default parameters gets things running. But did you notice that almost every atomic operation has an optional `std::memory_order` parameter? Many people (including the author back in the day) simply ignore it, since the default value works fine anyway.

This is indeed true in simple scenarios. But once you start using atomic variables for inter-thread synchronization—one thread writing data and another reading it—all sorts of bizarre phenomena pop up: data that was clearly written first is simply invisible to the other thread, or two threads observe completely inconsistent operation orders. The problem isn't with the atomic operations themselves, but rather that **both the compiler and the CPU are rearranging instructions behind your back**, and memory order is the tool you use to control this rearrangement.

In this article, we will break down the six `std::memory_order` values one by one, clarifying what each order guarantees, what it doesn't guarantee, and when to use which one.

## Why Rearrange: Compiler Optimization and CPU Optimization

Before diving into the six memory orders, we must first understand a fundamental fact: the order in which you write your code and the order in which the CPU actually executes it may not be the same thing. This isn't a bug, but rather an inevitable result of performance optimization.

The compiler performs instruction reordering during the optimization phase. When the compiler sees two pieces of code that don't depend on each other, it might swap their order—for example, writes to two different variables. The compiler figures that the order doesn't affect single-threaded semantics, so it might swap them. Consider this classic example:

```cpp
int data = 0;
bool ready = false;

// 线程 1
data = 42;         // 步骤 A
ready = true;      // 步骤 B

// 线程 2
if (ready) {       // 步骤 C
    use(data);     // 步骤 D
}
```

From a single-threaded perspective, the order of A and B doesn't matter (there is no data dependency between `data = 42` and `ready = true`). The compiler could easily place B before A. But from a multi-threaded perspective, this means Thread 2 might see `ready == true` but `data` is still 0—it thinks the data is ready, but it isn't.

The CPU level also has out-of-order execution. Modern CPUs feature superscalar, deep-pipeline designs. To keep the pipeline full and reduce stalls, hardware dynamically adjusts the execution order of instructions. x86 has a strong memory model (TSO, Total Store Ordering) that only allows store-load reordering; ARM and PowerPC have much weaker memory models that allow store-store, load-load, store-load, and load-store reordering. The same code might run fine on x86 but break on ARM—this is exactly why the C++ standard defines a platform-independent memory model.

To summarize: compiler reordering is for the efficiency of instruction scheduling and register allocation, while CPU reordering is for pipeline throughput. Both are "transparent" to single-threaded semantics—in a single-threaded program, no matter how you reorder, the final result remains the same (the as-if rule). However, multi-threaded programs depend not only on the final result but also on the **visibility order** between operations, and reordering precisely destroys this order.

## Overview of the Six Memory Orders

C++ defines six memory orders in the `std::memory_order` enumeration. Ordered from weakest to strongest, they are as follows. Among them, `memory_order_consume` was marked as "deprecated" in C++17 and is officially deprecated in C++26. In practice, mainstream compilers all treat it as `memory_order_acquire`. We will briefly mention it later but won't discuss it in depth.

- `memory_order_relaxed`: Only guarantees atomicity, providing no ordering constraints.
- `memory_order_consume`: Data-dependent ordering (deprecated, use acquire instead).
- `memory_order_acquire`: Used for load operations, guarantees that subsequent reads and writes cannot be reordered before this load.
- `memory_order_release`: Used for store operations, guarantees that prior reads and writes cannot be reordered after this store.
- `memory_order_acq_rel`: Used for read-modify-write operations, has both acquire and release semantics.
- `memory_order_seq_cst`: The default value, providing the strongest guarantee. All seq_cst operations exist in a globally consistent total order.

Let's break them down one by one.

## memory_order_relaxed: Atomicity Only

`memory_order_relaxed` is the lightest memory order. It guarantees that the operation itself is atomic—there will be no torn reads or torn writes, and different threads will not see intermediate states. However, it **does not guarantee any ordering between operations**, meaning the compiler and CPU are free to reorder relaxed operations with other operations before and after them.

A typical scenario is a pure counter. You only care about the final value of the counter, not the relative order between the counting operation and other operations:

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

std::atomic<int> request_count{0};
std::atomic<int> error_count{0};

void handle_request()
{
    request_count.fetch_add(1, std::memory_order_relaxed);
    // ... 处理请求 ...
}

void log_error()
{
    error_count.fetch_add(1, std::memory_order_relaxed);
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([]() {
            for (int j = 0; j < 100000; ++j) {
                handle_request();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "Total requests: " << request_count.load(
                     std::memory_order_relaxed) << "\n";
    // 输出：Total requests: 400000
    return 0;
}
```

The danger of relaxed is that you cannot use it for inter-thread synchronization. Many beginners make this mistake—using a combination of relaxed store/load as a "data is ready" flag:

```cpp
// 危险示例：用 relaxed 做同步
std::atomic<bool> data_ready{false};
int data = 0;

// 线程 1：生产者
data = 42;
data_ready.store(true, std::memory_order_relaxed);

// 线程 2：消费者
if (data_ready.load(std::memory_order_relaxed)) {
    // data 可能还是 0！
    use(data);
}
```

Why is this wrong? Because `memory_order_relaxed` does not prevent reordering. The compiler or CPU might reorder `data = 42` before `ready.store(true, ...)`. From Thread 2's perspective, `ready` has become true, but `data` still holds the old value. To use a flag for synchronization, you must use acquire-release—which is exactly what the next section covers.

## memory_order_acquire and memory_order_release: The Golden Pair for Synchronization

acquire and release are the most commonly used pair of memory orders. Together, they form the basic mechanism for inter-thread synchronization. Understanding this pair is the key to understanding the entire memory model.

### release: The "Publish" Semantics on Write

`memory_order_release` is used for store operations. It guarantees that **all read and write operations before this store (whether atomic or non-atomic) will not be reordered after this store**. You can think of it as a "publish" action—all preparations before this store are complete, and it is now officially published.

```cpp
int data = 0;
std::atomic<bool> ready{false};

// 线程 1：生产者
data = 42;                                  // 准备数据
ready.store(true, std::memory_order_release); // 发布：保证 data 先写入
```

A release store is like a sealed envelope—the contents of the letter (all prior writes) were written before it was sealed, and no content will be stuffed in after the fact.

### acquire: The "Subscribe" Semantics on Read

`memory_order_acquire` is used for load operations. It guarantees that **all read and write operations after this load will not be reordered before this load**. More importantly, if one thread reads a value with acquire that was written by another thread with release, then all writes made by the writing thread before the release become visible to the reading thread.

```cpp
// 线程 2：消费者
if (ready.load(std::memory_order_acquire)) {  // 订阅
    // 一定能看到 data == 42
    use(data);
}
```

An acquire load is like opening an envelope—you can only read the letter after breaking the seal. The content you see after opening it must have been written by the sender before they sealed it.

### synchronizes-with and happens-before

Now we can introduce the most core relationships in the C++ memory model. When Thread A executes a release store, and Thread B executes an acquire load and reads the value written by Thread A, we say that Thread A's store **synchronizes-with** Thread B's load.

The synchronizes-with relationship establishes a **happens-before** relationship: all operations executed by Thread A before the release store happen-before all operations executed by Thread B after the acquire load. The meaning of happens-before is that the preceding operations are **visible** to the subsequent operations.

This chain can be extended further. If operation A happens-before operation B, and operation B happens-before operation C, then A also happens-before C—this is transitivity. In a multi-threaded environment, this transitivity is established through the **inter-thread-happens-before** relationship, which chains together the sequenced-before relationship (program order) within the same thread and the cross-thread synchronizes-with relationship, forming a complete "visibility chain."

Returning to our example: `data = 42` sequenced-before `ready.store(...)` (within the same thread), `ready.store(...)` synchronizes-with `ready.load(...) == true` (cross-thread), `ready.load(...)` sequenced-before `use(data)` (within the same thread). Through transitivity, `data = 42` happens-before `use(data)`—so `use(data)` is guaranteed to see `42`.

### Message Passing Pattern

The most classic application of acquire-release is the message passing pattern: one thread prepares data and then notifies another thread that "the data is ready" through an atomic flag.

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <string>

struct Message {
    int id;
    std::string content;
};

Message msg;
std::atomic<bool> ready{false};

void producer()
{
    msg.id = 1;
    msg.content = "Hello from producer";
    // release：保证上面的赋值在 store 之前完成
    ready.store(true, std::memory_order_release);
}

void consumer()
{
    // 自旋等待，直到看到 ready == true
    while (!ready.load(std::memory_order_acquire)) {
        // 在实际代码中可以加 yield 或 sleep 避免纯自旋
    }
    // 此时一定能看到完整的 msg
    std::cout << "Received message #" << msg.id
              << ": " << msg.content << "\n";
}

int main()
{
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

Note that `data` itself is not an atomic variable—it's a plain `std::array` object. But the happens-before relationship established by acquire-release guarantees that after `ready.load(...)` reads `true`, it will definitely see the complete `std::array` written by `data.fill(...)`. This is the power of memory order: by synchronizing one atomic variable, you indirectly synchronize all the non-atomic data around it.

## memory_order_acq_rel: Bidirectional Guarantee for Read-Modify-Write Operations

`memory_order_acq_rel` is used for read-modify-write (RMW) operations—such as `fetch_add`, `fetch_sub`, and `compare_exchange`. These operations involve both reading and writing, so they simultaneously have acquire and release semantics: acquire guarantees that operations after this RMW won't be reordered before it, and release guarantees that operations before this RMW won't be reordered after it.

```cpp
std::atomic<int> counter{0};

// acq_rel：同时具有 acquire 和 release 语义
int old = counter.fetch_add(1, std::memory_order_acq_rel);
```

When do we need `memory_order_acq_rel`? The most typical scenario is reference counting. When a reference count decrements to 0, the object needs to be destroyed—acquire guarantees you can see the complete constructed state of the object, and release guarantees all prior uses happened before the reference decrement:

```cpp
class RefCounted {
public:
    void add_ref()
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release()
    {
        // acq_rel：减引用同时保证可见性
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // 最后一个引用被释放，安全销毁
            delete this;
        }
    }

protected:
    virtual ~RefCounted() = default;

private:
    std::atomic<int> ref_count_{1};
};
```

## memory_order_seq_cst: The Default Global Total Order

`memory_order_seq_cst` (sequentially consistent) is the default memory order for all atomic operations and provides the strongest guarantee. On top of acquire-release, it adds an extra constraint: **there exists a globally consistent single total order among all `seq_cst` operations**—all threads see the same execution order for `seq_cst` operations.

What does this mean? Consider a scenario involving multiple atomic variables:

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程 1
x.store(1, std::memory_order_seq_cst);

// 线程 2
y.store(1, std::memory_order_seq_cst);

// 线程 3
int r1 = x.load(std::memory_order_seq_cst);
int r2 = y.load(std::memory_order_seq_cst);

// 线程 4
int r3 = y.load(std::memory_order_seq_cst);
int r4 = x.load(std::memory_order_seq_cst);
```

If we use `memory_order_seq_cst`, it's impossible for "Thread 3 sees `x==1 && y==0` (x changed first)" and "Thread 4 sees `y==1 && x==0` (y changed first)" to occur simultaneously. Because `seq_cst` guarantees that all threads agree on the modification order of x and y—either globally x changed first, or globally y changed first.

If we switch to `memory_order_acq_rel`, this consistency is no longer guaranteed. Acquire-release only establishes a synchronizes-with relationship between paired load/store operations, but doesn't impose global constraints on the order between different atomic variables. In scenarios requiring multiple atomic variables to coordinate, `seq_cst` is the safest choice.

What's the cost? On x86, the cost is very small—x86's TSO model is already very strong, and a `seq_cst` store only requires a single `XCHG` or `MFENCE` instruction. But on weak memory model architectures like ARM and PowerPC, `seq_cst` requires a full memory barrier (ARMv8's `DMB ISH`, PowerPC's `sync`), and the performance overhead can be 3 to 6 times that of `acq_rel`.

A practical principle: **start with `seq_cst`, and if it runs fine and performance is satisfactory, don't touch it**. Only consider downgrading to acquire-release or even relaxed when you have a clear performance bottleneck and profiling confirms that atomic operations are the culprit. Prematurely optimizing memory order is a hidden source of bugs in concurrent programming.

## memory_order_consume: The Dependency Order Deprecated in C++26

`memory_order_consume` was originally designed to be lighter than `memory_order_acquire`: it only guarantees that operations dependent on the loaded value won't be reordered before this load, while operations that don't depend on this value are unconstrained. In scenarios involving publishing a pointer, this is theoretically more efficient than `acquire`—you only need to guarantee that data accessed through the pointer is correct, without synchronizing all other memory operations.

In reality, however, no mainstream compiler truly implements the precise semantics of consume. It is very difficult for compilers to perform dependency chain tracking, so both GCC and Clang promote `memory_order_consume` to `memory_order_acquire`. C++17 marked `memory_order_consume` as "deprecated," and in practice, you should just use `memory_order_acquire`.

## When to Use Each Order: A Practical Guide

At this point, we have broken down all memory orders one by one. The following practical decision flow can help you make choices in actual coding.

**Pure counters, statistics, and metrics**: Use `memory_order_relaxed`. You only care about the accuracy of the final value, not the order between it and other operations.

**One thread writes data, another thread reads data** (message passing pattern): Use `memory_order_release` on the writing side, and `memory_order_acquire` on the reading side. This is the most common and most essential pattern to master.

**Reference counting, semaphores, and other RMW operations**: Use `memory_order_acq_rel`. When the reference count decrements to 0, the object must be destroyed, requiring you to simultaneously see the complete object state (acquire) and ensure all prior accesses are finished (release).

**Multiple atomic variables need to coordinate**: Use `memory_order_seq_cst`. If you're unsure what to use, start with `seq_cst` too.

**Never use `memory_order_consume`**: Use `memory_order_acquire` instead.

A more concise rule of thumb: when you can explicitly point out in your code "here needs to synchronizes-with there," use acquire-release; when you need "all threads to agree on a consistent order for all atomic operations," use seq_cst; when you don't need any synchronization and only care about atomicity itself, use relaxed.

## Exercises

### Exercise 1: Message Passing Experiment

Write a program to verify the correctness of acquire-release synchronization. Create two threads: a producer thread writes to a non-atomic variable `payload`, then stores a `std::atomic<bool>` with release semantics; a consumer thread loads the `std::atomic<bool>` with acquire semantics, and after reading true, reads `payload`. Confirm that the consumer always sees the correct payload value.

Then, change the memory order on both sides to `memory_order_relaxed`, and run it repeatedly under high concurrency. Can you observe the payload reading an old value? (Hint: This is very hard to reproduce on x86 because x86's hardware model is stronger than relaxed. You can try running on an ARM device or using ThreadSanitizer to increase the probability of reproduction.)

```cpp
#include <atomic>
#include <thread>
#include <iostream>

int payload = 0;
std::atomic<bool> ready{false};

void producer()
{
    payload = 42;
    ready.store(true, std::memory_order_release);
}

void consumer()
{
    while (!ready.load(std::memory_order_acquire)) {}
    std::cout << "payload = " << payload << "\n";
}

int main()
{
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

### Exercise 2: Behavior Comparison Between Relaxed and Acquire-Release

Write a program using two atomic variables `x` and `y` (both initialized to 0). Thread 1 stores 1 to x and y respectively; Thread 2 reads y and x (reading y first, then x). Run using two configurations:

1. All operations use `memory_order_relaxed`.
2. All operations use `memory_order_seq_cst`.

Execute repeatedly in a loop (for example, one million times), and count how many times Thread 2 sees `y == 1 && x == 0`. Theoretically, in relaxed mode this situation can occur (because there is no ordering constraint between the two stores), while in seq_cst mode it should not occur. Note: It is very difficult to observe a difference on x86; this experiment is better suited for weak memory model architectures.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## Reference Resources

- [std::memory_order -- cppreference](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [C++ Standard Draft [intro.multithread] -- eel.is](https://eel.is/c++draft/intro.multithread)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams, Chapter 5](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Herb Sutter: atomic Weapons -- CppCon 2012](https://www.youtube.com/watch?v=A8e5OjAVHEA)
- [Memory Ordering in Modern Microprocessors -- Paul E. McKenney](https://www.linuxjournal.com/content/memory-ordering-modern-microprocessors-part-i)
