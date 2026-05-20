---
title: CPU Cache and OS Threads
description: From the hardware cache hierarchy to the OS thread model, understanding
  the real physical stage where multithreaded programs execute.
chapter: 0
order: 3
tags:
- host
- cpp-modern
- intermediate
- 基础
- atomic
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 17
- 20
prerequisites:
- 为什么需要并发
- 并发基本问题
related:
- std::thread 基础
- 原子操作模式
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/03-cpu-cache-and-os-threads.md
  source_hash: 94c702129dc64029311ea000163119052a11506433a9c538142e54859c958552
  translated_at: '2026-05-20T04:34:28.797438+00:00'
  engine: anthropic
  token_count: 3825
---
# CPU Cache and OS Threads

In the previous two chapters, we built two layers of understanding: why we need concurrency, and what can go wrong. But there is a very practical matter that we have been intentionally or unintentionally skirting: what kind of hardware and operating system do multithreaded programs actually run on? What happens behind the scenes when we write `std::thread t(func)`? Why do multithreaded programs sometimes run not only slower than expected, but even slower than single-threaded ones?

In this chapter, we are going to dive deeper into the lower levels. We will start with the CPU cache hierarchy, figure out how cache coherence is maintained, and then understand a very practical problem—false sharing, which can silently drain more than half of your multithreaded program's performance. After that, we will move up a layer to see how the operating system implements threads, just how expensive a context switch really is, and how Linux's pthread and futex work together. Once we understand these concepts, when we later study `std::atomic` memory ordering and the implementation principles of mutexes, those concepts will no longer feel like they appeared out of thin air.

## CPU Cache Hierarchy

Before rushing into multithreading, let us consider a more fundamental question: why does a CPU need a cache?

The reason is simple—CPUs are too fast, and memory is too slow. A modern x86 CPU often runs at several GHz, with each clock cycle being about 0.5–1 nanosecond. In contrast, a single DDR4/DDR5 memory access takes about 50–100 nanoseconds. This means that if a CPU reads data directly from memory, it will idle for hundreds of cycles waiting for the data to return. It is like a top-tier chef who can chop 100 times per second, but the fridge is three kilometers away—running a round trip for every chop reduces efficiency to zero.

The solution to this bottleneck is straightforward: add a few layers of smaller, faster, but more expensive storage between the CPU and main memory, keeping frequently used data closer to the CPU. This is the famous CPU cache. Modern multi-core processors typically have three levels of cache, which are L1, L2, and L3, from innermost to outermost.

The L1 cache is closest to the CPU core and is divided into an instruction cache (L1i) and a data cache (L1d), each exclusively owned by a single core. A typical L1d size is 32–48 KB, with a latency of about 4–5 clock cycles (this is load-use latency—the number of cycles it takes for data to travel from L1 to a register; do not confuse this with throughput, as L1 can accept one load per cycle). The speed of this cache level is on the same order of magnitude as registers, but its capacity is very limited.

The L2 cache is also exclusive to each core, but it does not separate instructions and data. A typical size ranges from 256 KB to 1 MB, with a latency of about 10–15 cycles. It acts as a buffer between L1 and L3—hot data that cannot fit in L1 spills over here.

The L3 cache is the last line of defense shared by all cores. Typical sizes range from a few MB to tens of MB (server chips can even reach over a hundred MB), with a latency of about 30–50 cycles. Because it is shared by all cores, L3 is also a key hub for inter-core data transfer—when one core writes data, other cores need to be able to see it, and the coherence protocol coordinates at this level.

You can use `lscpu` on Linux to view your machine's cache configuration. The `L1d cache`, `L2 cache`, and `L3 cache` fields in the output will tell you the size of each level. If you are writing multithreaded performance benchmarks, taking a quick look at these numbers is very helpful.

### Cache Lines: The Minimum Unit of Cache

Cache does not exchange data with main memory byte by byte. It operates in units of **cache lines**, which are 64 bytes on almost all modern processors. This means that when you access a certain memory address, the entire 64-byte cache line is loaded into the cache, even if you only read a single byte.

The logic behind this design is **spatial locality**: if you access address A, there is a high probability you will soon access addresses near A. Array traversal is a typical scenario that benefits from this—when the first element is loaded, the next 15 `int` are brought into the cache along with it, making subsequent accesses cache hits with almost zero latency. (Note, one int is 4 bytes in size, which is why 15 + 1 = 16 `int` are actually loaded).

However, for multithreaded programs, cache lines have a very annoying side effect—false sharing, which we will expand on in detail later. For now, just remember one number: **64 bytes**. This is the key parameter for understanding all subsequent cache-related concepts.

## Cache Coherence and the MESI Protocol

In the single-core era, caching was simple—only one core was using it, data existed in only one place, and there was no ambiguity in reads or writes. But multi-core processors broke this assumption: each core has its own L1 and L2, and data for the same memory address might simultaneously exist in the caches of multiple cores. If core A modifies a value in its cache, and core B's cache still holds the old value, how does core B know the data has expired?

This is the problem that **cache coherence** solves. Modern x86 and ARM processors universally use the **MESI protocol** (Modified / Exclusive / Shared / Invalid) to maintain cache coherence among multiple cores. MESI assigns one of four states to each cache line:

**Modified (M)**: This cache line has been modified by the current core and is inconsistent with the value in main memory. The current core is the only one holding a valid copy of this data—if other cores' caches contain data at the same address, their state must be Invalid. When this cache line is evicted, it must be written back to main memory.

**Exclusive (E)**: This cache line is consistent with main memory, and only the current core holds it. Although the data has not been modified, "exclusive" means the current core can modify it at any time without notifying other cores—because no other core holds a copy of it.

**Shared (S)**: This cache line is consistent with main memory and may exist simultaneously in the caches of multiple cores. The current core can read it, but cannot directly write to it—before writing, it must first invalidate the copies held by other cores.

**Invalid (I)**: This cache line is invalid, equivalent to not caching any useful data. Accessing a cache line in the Invalid state triggers a cache miss, requiring it to be reloaded from main memory or another core's cache.

State transitions are driven by a bus snooping protocol or a directory-based protocol. Here is a concrete example: core A reads a certain address, and the cache line is not in any core's cache. It loads the line from main memory and sets the state to Exclusive. Core B also reads the same address; the bus snooping mechanism discovers that core A already has a copy, so it changes both sides' states to Shared. Then core A wants to write to this address. It first issues an **RFO (Read For Ownership)** request—meaning "I want to exclusively own this cache line to write to it, please have other holders invalidate their copies." After core B receives the RFO, it changes its cache line state to Invalid. Core A obtains exclusive ownership, performs the write, and the state becomes Modified.

This RFO request is one of the sources of performance overhead. In a multithreaded program, if two cores frequently write to different locations within the same cache line, RFOs will be triggered repeatedly—the cache line bounces back and forth between the two cores, walking the bus for invalidation every time. This brings us to the false sharing we are about to discuss.

It is worth mentioning that the MESI protocol guarantees **cache coherence**—meaning that for any single memory address, all cores will eventually see the same value. However, "cache coherent" does not mean "immediately visible"—a value written by one core may not be immediately visible to other cores. The reason is not the MESI protocol itself, but rather the processor's internal **store buffer**: write operations first enter the store buffer, and the core can continue executing subsequent instructions, waiting until the cache is ready to commit the write. Before the write actually enters the cache and triggers invalidation, other cores will continue to see the old value. Additionally, on the reading side, there is also an **invalidation queue**—received invalidation messages may queue up waiting to be processed, which further lengthens the time window for a new value to become visible. These microarchitectural buffering mechanisms make the behavior of multithreaded programs much more complex than the pure MESI model would suggest. This is also why C++'s `std::atomic` needs different `memory_order` to control the granularity of visibility—a topic we will explore in the atomic operations chapter later.

## False Sharing: The Invisible Performance Killer

False sharing is arguably the most "insidious" performance problem. Your code has absolutely no logical sharing—thread A only writes to its own variable `a`, and thread B only writes to its own variable `b`, with no data race at all—yet performance just will not improve, and it might even be slower than single-threaded. The reason is that `a` and `b` happen to fall on the same cache line.

Let us look at a typical case: two threads each incrementing a counter one hundred million times.

```cpp
#include <thread>
#include <iostream>
#include <chrono>

struct Counters {
    int a;  // 线程 1 写
    int b;  // 线程 2 写
};

int main()
{
    constexpr int kIterations = 100'000'000;
    Counters counters{0, 0};

    auto start = std::chrono::high_resolution_clock::now();

    std::thread t1([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counters.a++;
        }
    });
    std::thread t2([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counters.b++;
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time: " << ms.count() << " ms\n";
    std::cout << "a = " << counters.a << ", b = " << counters.b << "\n";
    return 0;
}
```

Logically, `counters.a` and `counters.b` are completely independent variables. The two threads each write to their own, with no synchronization needed. But the problem is that the `Counters` struct is only 8 bytes (two `int`), and both members fall on the same 64-byte cache line. When thread 1 (running on core A) writes to `counters.a`, core A's cache line state becomes Modified. When thread 2 (running on core B) wants to write to `counters.b`, it finds that this cache line is in the Modified state on core A, so it issues an RFO request to invalidate core A's copy. The next time core A writes to `counters.a`, it finds the cache line has been invalidated and has to pull it back in again... And so it bounces back and forth a hundred million times, with the cache line ping-ponging frantically between the two cores.

Run it on your own machine and you will see—the execution time of this code is usually several times slower than the single-threaded version. This is entirely due to hardware-level cache line contention, and it has absolutely nothing to do with your code logic, but its impact is very real. This project's `code/volumn_codes/vol5/ch00-concurrency-fundamentals/false_sharing_bench.cpp` provides a complete comparative benchmark (including false sharing, alignas-aligned, and single-threaded versions), which can be compiled and run directly with CMake. Below are the author's actual test results in a WSL2 environment (x86-64, 7 cores, GCC 16.1.1, `-O2`):

| Version | Time | Notes |
|---------|------|-------|
| False sharing | ~500–700 ms | Two `int` on the same cache line, inter-core ping-pong |
| Aligned (`alignas(64)`) | ~23–26 ms | Each occupies its own cache line, true parallelism |
| Single-threaded baseline | ~47–50 ms | Sequential execution of two loops |

As we can see, the false sharing version is **15–30 times slower** than the alignas-aligned version, and even about **10 times slower** than the single-threaded version—while the alignas version, because the two cores run in true parallel, takes only about half the time of the single-threaded version. Note that the counters in the test code use `volatile` to prevent the compiler from optimizing away the entire loop under `-O2`; the teaching code omits this, but it needs to be considered for actual measurements.

## Eliminating False Sharing: alignas and Cache Line Padding

The idea for solving false sharing is straightforward: just make sure the two variables are not on the same cache line. In C++11, we can use `alignas` to specify alignment:

```cpp
#include <thread>
#include <iostream>
#include <chrono>

// 通常定义为一个常量，方便复用
constexpr std::size_t kCacheLineSize = 64;

struct alignas(kCacheLineSize) AlignedCounter {
    int value{0};
};

int main()
{
    constexpr int kIterations = 100'000'000;
    AlignedCounter counter_a{};
    AlignedCounter counter_b{};

    auto start = std::chrono::high_resolution_clock::now();

    std::thread t1([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counter_a.value++;
        }
    });
    std::thread t2([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counter_b.value++;
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time: " << ms.count() << " ms\n";
    std::cout << "a = " << counter_a.value
              << ", b = " << counter_b.value << "\n";
    return 0;
}
```

`alignas(64)` tells the compiler that each instance of `AlignedCounter` must start at a 64-byte aligned address. Because the cache line size is 64 bytes, `counter_a` and `counter_b` each occupy an entire cache line and cannot possibly fall on the same one. RFOs no longer occur, and the two cores can happily write to their own cache lines without interfering with each other.

C++17 also provides a more elegant alternative: `std::hardware_destructive_interference_size`, defined in the `<new>` header. The value of this constant is the "minimum alignment size that causes false sharing" on the target platform—on almost all existing platforms, this is 64. Using this constant instead of a hand-written 64 makes the code more portable. However, note that compiler support for this constant is uneven—it has been available in GCC since version 12 (relying on the `__GCC_DESTRUCTIVE_SIZE` macro), but as of now, Clang still has not implemented it (resulting in a compilation error—the variable is simply undeclared, see [LLVM#60174](https://github.com/llvm/llvm-project/issues/60174)), so in real projects, hand-writing `constexpr std::size_t kCacheLineSize = 64;` is actually more reliable.

You might ask: a `int` is only 4 bytes, and `alignas(64)` makes it occupy 64 bytes—isn't this a waste of memory? Yes, it does waste 60 bytes of space. But this is a classic **space-for-time** tradeoff—60 bytes of memory on a modern machine is negligible, but eliminating false sharing can improve performance by several times. In concurrent programming, this practice of "wasting a little space to gain scalability" is very common. You will see this pattern in many high-performance libraries and frameworks: each thread's local counter `alignas(64)` is laid out neatly, and then aggregated at the end. It looks like it wastes a few hundred bytes, but in exchange for linear multi-core scalability, this is a deal that makes sense no matter how you calculate it.

There is another approach, which is to manually pad the struct:

```cpp
struct PaddedCounter {
    int value{0};
    char padding[60];  // 填满到 64 字节
};
```

This approach also works, but it is not as elegant as `alignas`—you need to calculate how many bytes to pad yourself, and the compiler does not guarantee alignment. `alignas` is the more recommended approach, and its semantics are clearer. Regardless of which method you use, the core idea is the same: ensure that independently written concurrent variables are separated by at least 64 bytes so they do not share the same cache line.

## OS Thread Model: From User Space to Kernel Space

Having discussed hardware-level caching, let us move up a layer and see how the operating system implements threads.

From the operating system's perspective, a thread is the basic unit of CPU scheduling, and a process is the basic unit of resource allocation. A process can contain multiple threads that share the same address space, file descriptor table, signal handlers, and other resources, but each thread has its own independent stack, register state, and program counter. This design of "sharing most resources but executing independently" makes threads the natural vehicle for implementing concurrency.

The reason threads can run "simultaneously" is that the operating system implements a **context switch** mechanism: it saves the current thread's register state to memory (specifically, to the Thread Control Block corresponding to this thread), then restores the next thread's register state and jumps to where it left off to continue execution. All of this happens in kernel space—the creation, scheduling, and switching of threads are all managed by the kernel.

The operating system maintains a **Thread Control Block (TCB)** for each thread, which stores the thread's complete state: register snapshot, stack pointer, program counter, scheduling priority, signal mask, and various scheduling-related metadata. The TCB itself occupies anywhere from a few hundred bytes to a few KB, and with each thread's default stack space (8 MB on Linux), the baseline overhead of a thread is not insignificant. This is also why you cannot casually spawn tens of thousands of threads—the stack space alone would consume dozens of GB of memory.

### The Cost of Context Switching

Just how expensive is a context switch? We can break it down. First is the **direct cost**: saving and restoring general-purpose registers (about 16 on x86-64), floating-point/SIMD registers (the AVX-512 ZMM register set has 32 512-bit registers, and saving them alone involves moving several KB of data), and various system registers. This step is usually on the order of a few microseconds.

Then there is the **indirect cost**, which is often larger than the direct cost. After switching to a new thread, the TLB (Translation Lookaside Buffer) caches the virtual-to-physical address mappings of the previous thread, which are mostly invalid for the new thread. A TLB miss triggers a page table walk, and each walk requires multiple memory accesses, which is costly. Similarly, when the new thread executes, it accesses its own data, which is highly likely not in the current core's cache, leading to a storm of cache misses. The performance gap between a cold cache and a hot cache can be tenfold or even a hundredfold.

If you are interested in specific numbers, you can use `perf stat` on Linux to observe the number of context switches, or use a micro-benchmarking tool like `context_switch_bench` to measure them. Empirically, the total cost of a single context switch (direct + indirect) is between a few microseconds and a few tens of microseconds, depending on the hardware and working set size. For a compute-intensive loop, if your task granularity is only a few microseconds, the context switch overhead might exceed the actual computation—this is the hardware-level manifestation of the "task granularity too fine" problem mentioned in the previous chapter.

## Linux's Thread Implementation: pthread, clone, and futex

Linux's thread implementation has an interesting history. Early Linux kernels (before 2.4) did not have a native concept of threads—the kernel only understood processes. The so-called "threads" were lightweight processes created via the `clone()` system call: they shared the address space, file descriptor table, and other resources with the parent process, but in the kernel's view, they were still independent scheduling entities. This design was later standardized as **NPTL (Native POSIX Thread Library)**, which became the default thread implementation starting with Linux 2.6.

`clone()` is Linux's lowest-level thread creation primitive. You can think of it as a finely controlled version of `fork()`—`fork()` creates a brand-new process (copying all resources), while `clone()` allows you to precisely specify which resources to share with the parent process and which to copy. When we call `pthread_create()`, glibc internally creates a new thread via `clone()` with a specific set of flags, which specify sharing the address space (`CLONE_VM`), sharing the file descriptor table (`CLONE_FILES`), sharing signal handlers (`CLONE_SIGHAND`), and so on.

You might ask: since each thread is an independent scheduling entity in the kernel, what is the relationship between pthread and `std::thread`? It is actually quite simple—`std::thread`'s implementation on Linux wraps `pthread_create()`, which in turn wraps the `clone()` system call. So when you write `std::thread t(func)`, the call chain is: `std::thread` -> `pthread_create` -> `clone` -> the kernel creates a new task_struct. Each layer is a thin wrapper around the next.

### futex: Fast in User Space, Slow in Kernel Space

Having discussed thread creation, let us talk about thread synchronization. The mutex is the most commonly used synchronization primitive, but its implementation has a performance challenge: if the lock is not contested, why make a trip to the kernel at all? **futex** (fast userspace mutex) was designed to solve this problem.

The core idea of futex is that the **fast path completes in user space, and only the slow path enters the kernel**. When you try to acquire a mutex, glibc's implementation first performs an atomic operation in user space (usually `compare-and-swap`) to attempt to acquire the lock. If the lock is free, you get it directly without any system call—this is the fast path, with an overhead of only a few dozen clock cycles. If the lock is held by another thread, the slow path is taken: the `futex(FUTEX_WAIT)` system call is invoked, asking the kernel to suspend this thread until the lock holder wakes it up via `futex(FUTEX_WAKE)`.

This design is very elegant: in the uncontested case, the overhead of a mutex approaches that of a single atomic operation; the cost of a system call is only paid when actual contention occurs. C++'s `std::mutex` is implemented based on this mechanism on Linux. Once you understand how futex works, you will see why "an uncontested mutex is cheap, but a heavily contested mutex is expensive"—the former is completed entirely in user space, while the latter requires switching back and forth between user space and kernel space every time.

## Thread Model Comparison: 1:1, M:N, and N:1

The next question is: what is the mapping relationship between user-space threads and kernel-space threads? This is the so-called thread model.

The **1:1 model** is the most intuitive—every user-space thread corresponds to one kernel thread. Linux's pthread (and `std::thread`) use this model. Its advantage is simplicity: threads can run directly on multiple cores to achieve true parallelism, and blocking operations (like I/O) only block the corresponding kernel thread without affecting other threads. The disadvantage is that thread creation and switching are expensive (both require entering the kernel), and each kernel thread has its own stack and TCB, limiting the number of threads.

The **N:1 model** is the other extreme—multiple user-space threads are all mapped to a single kernel thread. Thread creation and scheduling are done entirely in user space (no system calls needed), making them very lightweight and fast to switch. But its fatal flaw is that if any user-space thread performs a blocking operation (like reading a file), the entire kernel thread gets stuck, and all user-space threads freeze. Moreover, because there is only one kernel thread, these user-space threads can only ever run on one core, with no true parallelism. Some early green thread implementations used this model.

The **M:N model** attempts to get the best of both worlds—M user-space threads are mapped to N kernel threads (usually M >> N). The scheduler runs in user space, assigning user-space threads to kernel threads for execution, maintaining lightness while leveraging multi-core parallelism. Go's goroutine is a classic implementation of this model: goroutines are very lightweight (initial stack is only 2–8 KB), and the Go runtime scheduler is responsible for assigning them to a small number of OS threads; a blocked goroutine does not stall the entire thread. But the implementation complexity of the M:N model is very high—the scheduler needs to handle preemption, system call wrapping, and stack switching between user space and kernel space, and it is easy to inadvertently introduce new problems.

For C++ programmers, `std::thread` uses the 1:1 model on all mainstream platforms. If you need a large number of lightweight concurrent tasks, `std::thread` is not a good choice—you should consider a thread pool (a fixed number of worker threads + a task queue) or coroutines (C++20's `std::coroutine`). Thread pools and coroutines are essentially M:N scheduling strategies built on top of the 1:1 model, except that the scheduling logic is controlled by you or by a runtime library.

Which model to choose depends on your specific scenario. If you only have a few CPU-intensive tasks that need to run in parallel, just use `std::thread` directly—the 1:1 model is simple and reliable, with no extra abstraction layer. If you need to handle thousands or tens of thousands of concurrent connections or tasks, a thread pool is a more pragmatic choice. (We will do some coding in the exercises!) And if you are pursuing extremely low task-switching overhead and need millions of concurrent units, you will need to consider coroutines or an M:N runtime like Go's goroutines.

## Thread Scheduling: Who Runs First, and For How Long

Finally, let us briefly discuss OS thread scheduling. This content is very helpful for understanding the behavior of concurrent programs.

Modern operating systems generally use **preemptive scheduling**—the OS assigns each thread a time slice (usually a few milliseconds to a few tens of milliseconds). When the time slice is used up, it forcibly switches to the next thread, regardless of whether the current thread wants to yield. This is different from **cooperative scheduling**, which requires threads to voluntarily yield the CPU. The advantage of preemptive scheduling is that no single thread can monopolize the CPU (at least under normal circumstances); the disadvantage is that context switches happen at moments you cannot predict, which is one of the reasons concurrent bugs are hard to reproduce.

On Linux, the scheduling policy for normal threads is CFS (Completely Fair Scheduler). CFS does not use fixed time slices; instead, it allocates CPU time proportions based on a thread's **nice value**. The nice value ranges from -20 to +19, with a default of 0; lower values mean higher priority and more CPU time (but it is not a strict priority—CFS pursues "fairness" rather than strict priority scheduling). You can adjust this with the `nice` command or the `setpriority()` system call.

Another useful concept is **CPU affinity**. By default, the OS scheduler can migrate threads between any cores—a thread that ran on core A for 50 ms might be scheduled to run on core B in the next time slice. This kind of migration causes the L1/L2 caches to go completely cold. If you know that a certain thread has a large working set and cache locality is important, you can use `cpu_set_t` and `sched_setaffinity()` to "pin" it to a specific core, preventing the scheduler from migrating it. The following code shows the basic usage:

```cpp
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <iostream>

void pin_thread_to_core(int core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int result = pthread_setaffinity_np(
        pthread_self(), sizeof(cpu_set_t), &cpuset);

    if (result != 0) {
        std::cerr << "Failed to pin to core " << core_id << "\n";
    }
}
```

The C++ standard library itself does not provide an interface for setting CPU affinity (this is a platform-specific concept), but `std::thread::native_handle()` can retrieve the underlying `pthread_t`, and then you can use POSIX interfaces to operate on it. In real high-performance scenarios, reasonably pinning threads to cores (for example, pinning the producer thread to core 0 and the consumer thread to core 1) can significantly improve performance—reducing cross-core cache line migration and lowering the MESI protocol's RFO overhead, which is in the same vein as our earlier discussion of false sharing.

## Summary

In this chapter, we gained a deep understanding of the real stage on which multithreaded programs run, from both the hardware and operating system perspectives. At the hardware level, the CPU cache's L1/L2/L3 hierarchy, the 64-byte granularity of cache lines, the MESI protocol's state transitions, and RFO requests—these mechanisms determine the actual performance of multithreaded programs. False sharing is the easiest cache performance trap to fall into—two seemingly independent variables repeatedly trigger MESI protocol invalidations because they happen to fall on the same cache line, and `alignas(64)` is the most direct and effective solution.

At the operating system level, Linux's threads are implemented using the 1:1 model via the `clone()` system call—each user-space thread corresponds to one kernel scheduling entity. The direct cost of a context switch (register save/restore) plus the indirect cost (TLB flush, cache misses) makes thread switching a non-negligible cost. The futex design of "fast path in user space, slow path in kernel space" makes uncontested mutexes very cheap, but when contention is fierce, the cost of system calls quickly becomes apparent. Different thread models (1:1, M:N, N:1) each have their tradeoffs. C++'s `std::thread` uses the 1:1 model, and for a large number of lightweight concurrent tasks, you need to rely on thread pools or coroutines to compensate.

Now we have the basic understanding of concurrency (ch00-01), we know what can go wrong with concurrency (ch00-02), and we understand how hardware and the OS support multithreading (this chapter). The next step is that we can finally start writing code—the next chapter will formally introduce the interfaces and usage of `std::thread`.

> 💡 The complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch00-concurrency-fundamentals/`.

## Exercises

### Exercise 1: Reproduce and Eliminate False Sharing

Compile and run the `Counters` example above (unaligned version) and record the execution time. Then switch to the `AlignedCounter` version of `alignas(64)` and compare the execution times of the two. What is the performance difference on your machine? Try increasing the number of threads to four (with four independent counters) and observe whether the performance difference is even larger.

### Exercise 2: Observe the Cache Line Size

Run `lscpu` or `cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size` on Linux to view your machine's cache line size. Then, in C++, use `std::hardware_destructive_interference_size` (C++17, defined in `<new>`) to obtain the cache line size visible at compile time. If the compiler does not support this constant, hand-writing `constexpr size_t kCacheLineSize = 64;` works too—almost all mainstream platforms currently use 64 bytes.

### Exercise 3: Measure the Cost of a Context Switch

Write a program that creates two threads and does ping-pong-style alternating wakeups via `std::atomic<bool>`: thread A sets `flag = true` and then waits for `flag` to change back to `false`, thread B waits for `flag` to become `true` and then sets it back to `false`, looping one million times. Divide the total time by the number of switches to estimate the approximate cost of a single context switch. This number will include the overhead of atomic operations and the context switch itself, but it gives a sense of the order of magnitude.

## References

- [MESI protocol — Wikipedia](https://en.wikipedia.org/wiki/MESI_protocol)
- [False Sharing — Intel Developer Zone](https://www.intel.com/content/www/us/en/developer/articles/technical/avoiding-and-identifying-false-sharing-among-threads.html)
- [A futex overview and update — Ulrich Drepper (Red Hat)](https://man7.org/linux/man-pages/man7/futex.7.html)
- [The Native POSIX Thread Library for Linux — Ulrich Drepper, Ingo Molnar](https://www.akkadia.org/drepper/nptl-design.pdf)
- [CFS Scheduler Design — kernel.org](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
