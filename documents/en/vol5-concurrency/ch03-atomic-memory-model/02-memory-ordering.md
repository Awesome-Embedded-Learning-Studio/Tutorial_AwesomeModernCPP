---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: From compiler reordering to CPU reordering, we break down the six `memory_order`
  types and happens-before relationships one by one.
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
title: Detailed Explanation of Memory Order
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md
  source_hash: e9518f80952e6e053dd85a07c494de2063d8d534c1ab40c13fe89e94c7167527
  translated_at: '2026-06-16T04:04:05.381822+00:00'
  engine: anthropic
  token_count: 2910
---
# A Deep Dive into Memory Ordering

In the previous post, we broke down the complete operation set of `std::atomic`—load, store, fetch_add, compare_exchange—and saw that we could get by just using the default parameters. But did you notice that almost every atomic operation has an optional `std::memory_order` parameter? Many people (including the author back in the day) ignore it completely; after all, the default value works fine.

This is often true for simple scenarios. However, once you start using atomic variables for synchronization between threads—where one thread writes data and another reads it—strange phenomena start to appear: data written first is invisible to the other thread, or the order of operations observed by two threads is completely inconsistent. The problem isn't with the atomic operations themselves, but rather that **both the compiler and the CPU are reordering instructions behind your back**. Memory ordering is the tool you use to control this reordering.

In this post, we will break down the six memory orders one by one to understand what each guarantees, what it doesn't, and when to use which one.

## Why Reorder: Compiler Optimization and CPU Optimization

Before diving into the six memory orders, we must understand a fundamental fact: the order in which you write code and the order in which the CPU actually executes it may not be the same. This isn't a bug; it's a necessary result of performance optimization.

Compilers perform instruction reordering during the optimization phase. When the compiler sees two pieces of code that are independent of each other, it might swap their order—for example, writes to two different variables. The compiler figures that the order doesn't affect single-threaded semantics, so it might swap them. Consider this classic example:

```cpp
// Thread 1
int x = 0;
int y = 0;

void write() {
    x = 10;  // A
    y = 1;   // B
}
```

From a single-threaded perspective, the order of A and B is irrelevant (there is no data dependency between `x` and `y`). The compiler might well schedule B before A. But from a multi-threaded perspective, this means Thread 2 might see `y == 1` but `x` is still 0—it thinks the data is ready when it isn't.

Reordering also exists at the CPU level. Modern CPUs are superscalar and deeply pipelined; to keep the pipeline full and reduce stalls, hardware dynamically adjusts the execution order of instructions. x86 has a strong memory model (TSO, Total Store Ordering) and only allows store-load reordering. Architectures like ARM and PowerPC have much weaker memory models, allowing store-store, load-load, store-load, and load-store reordering. The same code might run fine on x86 but fail on ARM—this is why the C++ standard defines a platform-independent memory model.

To summarize: compiler reordering is for instruction scheduling and register allocation efficiency; CPU reordering is for pipeline throughput. Both are "transparent" to single-threaded semantics—in a single-threaded program, no matter how you reorder, the final result remains the same (as-if rule). However, multi-threaded programs rely not just on the final result, but also on the **visibility order** between operations, and reordering breaks precisely that order.

## Overview of the Six Memory Orders

C++ defines six memory orders in the `std::memory_order` enum. Listed from weakest to strongest, they are as follows. Note that `memory_order_consume` was marked as "deprecated" in C++17 and formally deprecated in C++26. In practice, mainstream compilers treat it as `memory_order_acquire`, so we will mention it briefly but not discuss it in depth.

- `memory_order_relaxed`: Guarantees only atomicity, provides no ordering constraints.
- `memory_order_consume`: Dependency ordering (deprecated, use acquire instead).
- `memory_order_acquire`: Used for load operations, guarantees subsequent reads/writes cannot be reordered before this load.
- `memory_order_release`: Used for store operations, guarantees previous reads/writes cannot be reordered after this store.
- `memory_order_acq_rel`: Used for read-modify-write operations, has both acquire and release semantics.
- `memory_order_seq_cst`: The default value, provides the strongest guarantee, where all seq_cst operations exist in a single global total order.

Let's go through them one by one.

## memory_order_relaxed: Atomicity Only

`memory_order_relaxed` is the lightest memory order. It guarantees that the operation itself is atomic—there will be no torn reads or torn writes, and different threads will not see an intermediate state. However, it **guarantees no ordering between operations**, meaning the compiler and CPU are free to reorder relaxed operations with other operations around them.

A typical scenario is a simple counter. You only care about the final value of the counter, not the relative order between the counting operation and other operations:

```cpp
std::atomic<int> counter{0};

void increment() {
    // Relaxed is sufficient for a simple counter
    counter.fetch_add(1, std::memory_order_relaxed);
}
```

The danger of relaxed is that you cannot use it for thread synchronization. Many newcomers make this mistake—using a relaxed store/load combination as a "data ready" flag:

```cpp
// Thread 1: Writer
data = 42;
ready.store(true, std::memory_order_relaxed); // ❌ Wrong!

// Thread 2: Reader
if (ready.load(std::memory_order_relaxed)) {
    use(data); // data might not be 42 yet!
}
```

Why is this wrong? Because `memory_order_relaxed` doesn't prevent reordering. The compiler or CPU might reorder `ready.store` before `data = 42`. From Thread 2's perspective, `ready` becomes true, but `data` still holds the old value. To use a flag for synchronization, you must use acquire-release—which is exactly what the next section covers.

## memory_order_acquire and memory_order_release: The Golden Partners of Synchronization

Acquire and release are the most commonly used pair of memory orders. Together, they form the basic mechanism for synchronization between threads. Understanding this pair is the key to understanding the entire memory model.

### release: The "Publish" Semantics on Write

`memory_order_release` is used for store operations. It guarantees that **all read and write operations before this store (whether atomic or non-atomic) will not be reordered after this store**. You can think of it as a "publish" action—all preparations before this store are complete, and it is now officially published.

```cpp
std::atomic<bool> ready{false};

void thread1() {
    data = 42;        // A: Prepare data
    ready.store(true, std::memory_order_release); // B: Publish
}
```

A release store is like a sealed letter—the contents of the letter (all previous writes) are written before sealing, and nothing will be stuffed in after sealing.

### acquire: The "Subscribe" Semantics on Read

`memory_order_acquire` is used for load operations. It guarantees that **all read and write operations after this load will not be reordered before this load**. More importantly, if a thread reads a value written by another thread using release with an acquire load, then all writes made by the writing thread before the release are visible to the reading thread.

```cpp
void thread2() {
    while (!ready.load(std::memory_order_acquire)) { // C: Wait
        // spin
    }
    assert(data == 42); // D: Use data
}
```

An acquire load is like opening a letter—you can only read the letter after breaking the seal. The content you see after opening the letter is definitely what the writer wrote before sealing it.

### synchronizes-with and happens-before

Now we can introduce the most core relationships in the C++ memory model. When Thread A executes a release store and Thread B executes an acquire load that reads the value written by Thread A, we say that Thread A's store **synchronizes-with** Thread B's load.

The synchronizes-with relationship establishes a **happens-before** relationship: all operations executed by Thread A before the release store happen-before all operations executed by Thread B after the acquire load. The meaning of happens-before is: the side effects of the earlier operations are **visible** to the later operations.

This chain can be extended further. If operation A happens-before operation B, and operation B happens-before operation C, then A also happens-before C—this is transitivity. In a multi-threaded environment, this transitivity is established through the **inter-thread-happens-before** relationship, which chains the sequenced-before relationship (program order) within the same thread with the synchronizes-with relationship across threads to form a complete "visibility chain".

Returning to our example: `data = 42` (A) sequenced-before `ready.store` (B) (same thread), `ready.store` (B) synchronizes-with `ready.load` (C) == true (cross-thread), `ready.load` (C) sequenced-before `assert` (D) (same thread). Through transitivity, `data = 42` (A) happens-before `assert` (D)—so the assertion is guaranteed to see `data == 42`.

### The message passing Pattern

The most classic application of acquire-release is the message passing pattern: one thread prepares data and then notifies another thread via an atomic flag that "data is ready".

```cpp
// Writer Thread
int payload = 0;
std::atomic<bool> ready{false};

void send() {
    payload = compute(); // Prepare data
    ready.store(true, std::memory_order_release); // Publish
}

// Reader Thread
void receive() {
    while (!ready.load(std::memory_order_acquire)) {
        // Wait
    }
    process(payload); // Safe to read
}
```

Note that `payload` itself is not an atomic variable—it is a plain `int` object. However, the happens-before relationship established by acquire-release guarantees that `process(payload)` will see the complete `payload` written by `compute()` after reading `ready == true`. This is the power of memory ordering: by synchronizing one atomic variable, you indirectly synchronize all non-atomic data surrounding it.

## memory_order_acq_rel: Bidirectional Guarantee for Read-Modify-Write Operations

`memory_order_acq_rel` is used for read-modify-write (RMW) operations—such as `fetch_add`, `exchange`, `compare_exchange`. These operations involve both reading and writing, so they possess both acquire and release semantics: acquire guarantees that operations after this RMW won't be reordered before it, and release guarantees that operations before this RMW won't be reordered after it.

```cpp
std::atomic<int> ref_count{0};

void decrement() {
    // Acquire-release ensures we see the object state when ref drops to 0
    if (1 == ref_count.fetch_sub(1, std::memory_order_acq_rel)) {
        destroy_object(); // Safe to destroy
    }
}
```

When do we need `memory_order_acq_rel`? The most typical scenario is reference counting. When the reference count decrements to 0, the object needs to be destroyed—acquire ensures you see the complete construction result of the object, and release ensures all previous usage happened before the decrement:

```cpp
// Shared pointer implementation (simplified)
class ControlBlock {
    std::atomic<size_t> ref_count;
    T* data;
public:
    void release() {
        // acq_rel: synchronize with other threads sharing this pointer
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete data; // Acquire ensures seeing complete data
            delete this; // Release ensures previous accesses are done
        }
    }
};
```

## memory_order_seq_cst: The Default Global Total Order

`memory_order_seq_cst` (sequentially consistent) is the default memory order for all atomic operations and provides the strongest guarantee. It adds an extra constraint on top of acquire-release: **there exists a single global total order among all `seq_cst` operations**—all threads see the same execution order for `seq_cst` operations.

What does this mean? Consider a scenario involving multiple atomic variables:

```cpp
std::atomic<bool> x{false}, y{false};

// Thread 1
x.store(true, std::memory_order_seq_cst);

// Thread 2
y.store(true, std::memory_order_seq_cst);

// Thread 3
if (x.load(std::memory_order_seq_cst)) {
    assert(!y.load(std::memory_order_seq_cst)); // Might fail?
}

// Thread 4
if (y.load(std::memory_order_seq_cst)) {
    assert(!x.load(std::memory_order_seq_cst)); // Might fail?
}
```

If `seq_cst` is used, it is impossible for "Thread 3 sees x changed first (y is false)" and "Thread 4 sees y changed first (x is false)" to happen simultaneously. Because `seq_cst` guarantees that all threads agree on the order of modifications to x and y—either globally x changed first, or globally y changed first.

If we switch to `acq_rel`, this consistency is not guaranteed. Acquire-release only establishes a synchronizes-with relationship between paired load/store operations, but does not impose global constraints on the order between different atomic variables. In scenarios where multiple atomic variables need to coordinate, `seq_cst` is the safest choice.

What is the cost? On x86, the cost is small—x86's TSO model is already very strong, and a `seq_cst` store only requires one `mfence` or `xchg` instruction. However, on architectures with weak memory models like ARM and PowerPC, `seq_cst` requires a full memory barrier (`dmb ish` in ARMv8, `sync` in PowerPC), and the performance overhead can be 3 to 6 times that of `relaxed`.

A practical rule: **Start with `seq_cst`. If it runs and performance is satisfactory, don't touch it.** Only consider downgrading to acquire-release or even relaxed when you have a clear performance bottleneck and profiling confirms that atomic operations are the culprit. Premature optimization of memory ordering is a subtle source of bugs in concurrent programming.

## memory_order_consume: Deprecated Dependency Ordering in C++26

`memory_order_consume` was originally designed to be lighter than `acquire`: it only guarantees that operations depending on this load value won't be reordered before this load, while operations not depending on this value are unconstrained. In scenarios involving publishing pointers, this is theoretically more efficient than `acquire`—you only need to guarantee that data accessed through the pointer is correct, without synchronizing all other memory operations.

However, in reality, no mainstream compiler has truly implemented the precise semantics of consume. It is extremely difficult for compilers to track dependency chains, so both GCC and Clang promote `memory_order_consume` to `memory_order_acquire`. C++17 marked `memory_order_consume` as "deprecated", and in practice, using `memory_order_acquire` directly is sufficient.

## When to Use Which Order: A Practical Guide

At this point, we have dissected all memory orders. The following practical decision flow can help you make choices in actual coding.

**Pure counters, statistics, metrics**: Use `relaxed`. You only care about the accuracy of the final value, not the order between it and other operations.

**One thread writes data, another thread reads data** (message passing pattern): Use `release` on the writing side and `acquire` on the reading side. This is the most common and most essential pattern to master.

**Reference counting, semaphores, and other RMW operations**: Use `acq_rel`. When the reference count decrements to 0, the object needs to be destroyed; you must see the complete object state (acquire) and ensure all previous accesses are complete (release).

**Multiple atomic variables need to coordinate**: Use `seq_cst`. If you aren't sure what to use, start with `seq_cst`.

**Absolutely do not use `consume`**: Use `acquire` instead.

A simpler rule of thumb is: when you can explicitly point out "here needs to synchronizes-with there", use acquire-release; when you need "all threads to agree on the order of all atomic operations", use seq_cst; when you don't need any synchronization and only care about atomicity itself, use relaxed.

## Exercises

### Exercise 1: Message Passing Experiment

Write a program to verify the correctness of acquire-release synchronization. Create two threads: a producer thread writes to a non-atomic variable `payload`, then stores a `ready` flag with release semantics; a consumer thread loads `ready` with acquire semantics, and after reading true, reads `payload`. Confirm that the consumer always sees the correct payload value.

Then, change the memory order on both sides to `relaxed` and run repeatedly under high concurrency. Can you observe the payload reading an old value? (Hint: This is hard to reproduce on x86 because x86's hardware model is stronger than relaxed. You can try on an ARM device or use ThreadSanitizer to increase the probability of reproduction.)

```cpp
// TODO: Implement this experiment
```

### Exercise 2: Behavior Comparison Between relaxed and acquire-release

Write a program using two atomic variables `x` and `y` (both initialized to 0). Thread 1 stores 1 to x and y respectively; Thread 2 reads y and x (reads y first, then x). Run with two configurations:

1. All operations use `memory_order_relaxed`.
2. All operations use `memory_order_seq_cst`.

Run repeatedly in a loop (e.g., 1 million times) and count the number of times Thread 2 sees `x == 0 && y == 1`. Theoretically, in relaxed mode this situation might appear (because there is no ordering constraint between the two stores), while in seq_cst mode it should not appear. Note: It is difficult to observe differences on x86; this experiment is better suited for running on weak memory model architectures.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `exercises/memory_order`.

## Reference Resources

- [std::memory_order -- cppreference](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [C++ Standard Draft [intro.multithread] -- eel.is](https://eel.is/c++draft/intro.multithread)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams, Chapter 5](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Herb Sutter: atomic Weapons -- CppCon 2012](https://www.youtube.com/watch?v=A8e5OjAVHEA)
- [Memory Ordering in Modern Microprocessors -- Paul E. McKenney](https://www.linuxjournal.com/content/memory-ordering-modern-microprocessors-part-i)
