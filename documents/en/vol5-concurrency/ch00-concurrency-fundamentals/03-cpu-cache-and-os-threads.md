---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: From the hardware cache hierarchy to the OS threading model, understanding
  the real physical execution stage of multithreaded programs
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 为什么需要并发
- 并发基本问题
reading_time_minutes: 27
related:
- std::thread 基础
- 原子操作模式
tags:
- host
- cpp-modern
- intermediate
- 基础
- atomic
title: CPU Cache and OS Threads
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/03-cpu-cache-and-os-threads.md
  source_hash: fb5508caf612ee463a84cd3b71cfad4a9918ed0b2082d05f25bc85b7ce4689a6
  translated_at: '2026-06-24T01:06:15.336517+00:00'
  engine: anthropic
  token_count: 3818
---
# CPU Cache and OS Threads

In the previous two articles, we established the "why" and the "what can go wrong" layers of understanding concurrency. But there is a very practical issue that we have intentionally or unintentionally skirted around: what kind of hardware and operating system do multithreaded programs actually run on? When we write `std::thread t(func)`, what happens behind the scenes? Why is it that sometimes a multithreaded program runs not only without speedup, but even slower than a single-threaded one?

In this article, we are going to dive deeper. We will start with the hierarchy of the CPU cache to understand how cache coherence is guaranteed, and then look at a very practical problem—**false sharing**—which can silently cost your multithreaded program more than half of its performance. After that, we will move up a layer to see how the operating system implements threads, how expensive context switching really is, and how Linux's `pthread` and `futex` work together. Once we understand these, the concepts behind `std::atomic` memory ordering and mutex implementation principles won't seem like they appeared out of thin air when we study them later.

## CPU Cache Hierarchy

Before rushing to look at multithreading, let's consider a more basic question: why does a CPU need cache?

The reason is simple—the CPU is too fast, and memory is too slow. A modern x86 CPU has a clock frequency of several GHz, with each clock cycle being about 0.5-1 nanoseconds; whereas a single DDR4/DDR5 memory access takes about 50-100 nanoseconds. This means that if the CPU reads data directly from memory, it spins idle for hundreds of cycles waiting for the data to return. It's like a top chef who can chop 100 times per second, but the fridge is three kilometers away—making a round trip for every chop results in zero efficiency.

The solution to this bottleneck is straightforward: add layers of smaller, faster, but more expensive storage between the CPU and main memory to keep frequently used data closer to the CPU. This is the famous CPU cache. Modern multi-core processors usually have three levels of cache, known as L1, L2, and L3 from innermost to outermost.

The L1 cache is closest to the CPU core and is split into instruction cache (L1i) and data cache (L1d), with each core having its own exclusive copy. A typical L1d size is 32-48 KB, with a latency of about 4-5 clock cycles (this is load-use latency—the number of cycles for data to travel from L1 to a register; do not confuse this with throughput, as L1 can accept one load per cycle). The speed of this cache layer is on the same order of magnitude as registers, but the capacity is very limited.

The L2 cache is also per-core, but it does not distinguish between instructions and data. Typical sizes range from 256 KB to 1 MB, with a latency of about 10-15 cycles. It acts as a buffer between L1 and L3—hot data that doesn't fit in L1 spills over here.

The L3 cache is the last line of defense shared by all cores. Typical sizes range from a few MB to tens of MB (server chips can even reach hundreds of MB), with a latency of about 30-50 cycles. Because it is shared by all cores, L3 is also a key hub for inter-core data transfer—if one core writes data, other cores need to see it, and the coherence protocol is coordinated at this level.

You can use `lscpu` on Linux to check your machine's cache configuration; the `L1d cache`, `L2 cache`, and `L3 cache` lines in the output will tell you the size of each level. If you are writing multithreaded performance tests, taking a look at these numbers first is very helpful.

### Cache Lines: The Smallest Unit of Cache

The cache does not exchange data with main memory byte by byte. It operates in units of **cache lines**, which are 64 bytes on almost all modern processors. This means that when you access a memory address, the entire 64-byte cache line is loaded into the cache, even if you only read one byte from it.

The logic behind this design is **spatial locality**: if you access address A, there is a high probability you will soon access addresses near A. Array traversal is a typical beneficiary scenario—when the first element is loaded, the next 15 `int`s come into the cache along with it, so subsequent accesses are cache hits with almost zero latency. (Note, 1 `int` is 4 bytes in size, which is why 15 + 1 = 16 `int`s are actually loaded).

However, for multithreaded programs, cache lines have a very annoying side effect—**false sharing**, which we will expand on shortly. For now, just remember one number: **64 bytes**. This is the key parameter to understanding all subsequent cache-related issues.

## Cache Coherence and the MESI Protocol

In the single-core era, caching was simple—only one core was using it, data existed in only one place, and there was no ambiguity in reads or writes. But multi-core processors broke this assumption: each core has its own L1 and L2, and data from the same memory address can exist in the caches of multiple cores simultaneously. If core A modifies a value in its cache, but core B still has the old value in its cache, how does B know the data is stale?

This is the problem that **cache coherence** aims to solve. Modern x86 and ARM processors widely use the **MESI protocol** (Modified / Exclusive / Shared / Invalid) to maintain cache coherence between cores. MESI assigns one of four states to each cache line:

**Modified (M)**: This cache line has been modified by the current core and is inconsistent with the value in main memory. The current core is the only one holding a valid copy of this data—if other cores hold data for the same address in their cache, it must be in the Invalid state. When this cache line is evicted, it must be written back to main memory.

**Exclusive (E)**: This cache line is consistent with main memory, and only the current core holds it. Although the data hasn't been modified, "exclusive" means the current core can modify it at any time without notifying other cores—because no other cores hold a copy of it.

**Shared (S)**: This cache line is consistent with main memory and may exist in the caches of multiple cores simultaneously. The current core can read it, but cannot write directly—it must invalidate other cores' copies before writing.

**Invalid (I)**: This cache line is invalid, equivalent to holding no useful cached data. Accessing a cache line in the Invalid state triggers a cache miss, requiring it to be reloaded from main memory or another core's cache.

State transitions are driven by snooping protocols or directory-based protocols on the bus. Here is a concrete example: Core A reads an address, the cache line is not in any core's cache, it loads from main memory, and the state is set to Exclusive. Core B also reads the same address; the snooping mechanism on the bus discovers that Core A already has a copy, so both sides change their state to Shared. Then Core A wants to write to this address. It first issues an **RFO (Read For Ownership)** request—meaning "I want to own this cache line exclusively to write to it, please other holders invalidate your copies." Upon receiving the RFO, Core B changes its cache line state to Invalid. Core A obtains exclusive ownership, executes the write, and the state becomes Modified.

This RFO request is one of the sources of performance overhead. In multithreaded programs, if two cores frequently write to different locations on the same cache line, it will repeatedly trigger RFOs—the cache line bounces back and forth between the two cores, requiring bus invalidation every time. This leads us to the next topic: false sharing.

It is worth mentioning that the MESI protocol guarantees **cache coherence**—that is, for any single memory address, all cores will eventually see a consistent value. However, "cache coherent" does not mean "immediately visible"—a value written by one core may not be seen by other cores temporarily. The reason is not the MESI protocol itself, but the **store buffer** inside the processor: write operations enter the store buffer first, and the core can continue executing subsequent instructions, waiting for the cache to be ready before committing the write. Before the write actually enters the cache and triggers invalidation, other cores continue to see the old value. Furthermore, on the reading side, there is also an **invalidation queue**—received invalidation messages may queue up waiting to be processed, which further lengthens the time window for "new values becoming visible." These buffering mechanisms at the microarchitecture level make the behavior of multithreaded programs much more complex than the simple MESI model. This is also why C++'s `std::atomic` needs different `memory_order` options to control the granularity of visibility—we will expand on this topic in the later chapter on atomic operations.

## False Sharing: The Invisible Performance Killer

False sharing is what I consider the most "insidious" performance problem. Your code logic has absolutely no sharing—Thread A only writes to its own variable `a`, Thread B only writes to its own variable `b`, there is no data race—but the performance just won't go up, it's even slower than single-threaded. The reason is that `a` and `b` happen to land on the same cache line.

Let's look at a typical case: two threads each increment a counter 100 million times.

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

Logically, `counters.a` and `counters.b` are completely independent variables; the two threads write to their respective targets without any synchronization requirements. However, the problem is that the `Counters` structure is only 8 bytes (two `int`s), so both members land on the same 64-byte cache line. When thread 1 (running on core A) writes to `counters.a`, the cache line state in core A becomes Modified. When thread 2 (running on core B) wants to write to `counters.b`, it sees that this cache line is in the Modified state on core A, so it issues an RFO (Read For Ownership) request to invalidate core A's copy. The next time core A writes to `counters.a`, it discovers the cache line has been invalidated and has to fetch it again... This back-and-forth happens a hundred million times, with the cache line ping-ponging frantically between the two cores.

You can verify this by running it on your own machine—the execution time of this code is usually several times slower than the single-threaded version. This is entirely due to hardware-level cache line contention, having nothing to do with your code logic, but its impact is very real. This project's `code/volumn_codes/vol5/ch00-concurrency-fundamentals/false_sharing_bench.cpp` provides a complete comparative benchmark (including false sharing, `alignas` alignment, and single-threaded versions), which can be compiled and run directly via CMake. Here are the actual results measured by the author in a WSL2 environment (x86-64, 7 cores, GCC 16.1.1, `-O2`):

| Version | Time | Description |
|------|------|------|
| False sharing | ~500–700 ms | Two `int`s on the same cache line, ping-ponging between cores |
| Aligned (`alignas(64)`) | ~23–26 ms | Each occupies its own cache line, true parallelism |
| Single-threaded baseline | ~47–50 ms | Sequential execution of two loops |

As we can see, the false sharing version is **15–30 times slower** than the `alignas` aligned version, and even about **10 times slower** than the single-threaded version—whereas the aligned version takes only about half the time of the single-threaded version because the two cores are truly running in parallel. Note that the counters in the test code use `volatile` to prevent the compiler from optimizing away the entire loop under `-O2`; the teaching code omits this for simplicity, but it is necessary for actual measurements.

## Eliminating false sharing: `alignas` and cache line padding

The solution for false sharing is straightforward: just ensure the two variables don't sit on the same cache line. In C++11, we can use `alignas` to specify alignment:

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

`alignas(64)` tells the compiler that each instance of `AlignedCounter` must start at a 64-byte aligned address. Since the cache line size is 64 bytes, `counter_a` and `counter_b` each occupy a full cache line, making it impossible for them to reside on the same one. RFO (Read For Ownership) no longer occurs, and the two cores can happily write to their respective cache lines without interfering with each other.

C++17 provides a more elegant alternative: `std::hardware_destructive_interference_size`, defined in the `<new>` header. The value of this constant is the "minimum alignment size that causes false sharing" on the target platform—on almost all existing platforms, this is 64. Using this constant instead of a hard-coded 64 improves code portability. However, note that compiler support for this constant is uneven—GCC has supported it since version 12 (relying on the `__GCC_DESTRUCTIVE_SIZE` macro), but Clang has not implemented it as of now (resulting in a compilation error—the variable is simply not declared; see [LLVM#60174](https://github.com/llvm/llvm-project/issues/60174)). Therefore, in actual projects, manually writing `constexpr std::size_t kCacheLineSize = 64;` is actually safer.

You might ask: an `int` is only 4 bytes, so doesn't `alignas(64)` waste memory by making it occupy 64 bytes? Yes, it does waste 60 bytes of space. However, this is a classic **space-time trade-off**—60 bytes is negligible on modern machines, but eliminating false sharing can improve performance by several times. In concurrent programming, this practice of "wasting a little space to gain scalability" is very common. You will see this pattern in many high-performance libraries and frameworks: each thread's local counter is laid out with `alignas(64)`, and finally aggregated—it looks like a waste of a few hundred bytes, but it trades for linear multi-core scalability. This is a good deal no matter how you calculate it.

There is another approach which involves manually adding padding within the struct:

```cpp
// 手动填充示例
struct ManualPadCounter {
    int value;
    char padding[60]; // 填充到 64 字节
};
```

虽然这种方法也能达到目的，但 `alignas` 更为简洁且不易出错（例如手动计算填充大小时容易算错）。此外，`alignas` 可以让编译器协助处理对齐问题，而手动填充则完全依赖程序员的计算。

### 性能对比

为了直观展示 false sharing 对性能的影响，我们可以编写一个简单的基准测试。以下代码使用 C++11 的 `<thread>` 和 `<chrono>` 库来测量两个线程并发递增共享计数器的时间。

```cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

// 无对齐的计数器（存在 false sharing）
struct BadCounter {
    int value;
};

// 对齐的计数器（避免 false sharing）
struct GoodCounter {
    alignas(64) int value;
};

void benchmark(bool use_align) {
    const int num_threads = 2;
    const int iterations = 100000000; // 每个线程 1 亿次递增

    if (use_align) {
        GoodCounter counter{0};
        // ... 线程逻辑 ...
    } else {
        BadCounter counter{0};
        // ... 线程逻辑 ...
    }
}
```

运行结果（假设在典型的 x86-64 处理器上）：

- **BadCounter**: 约 120ms
- **GoodCounter**: 约 15ms

性能差异高达 8 倍！这充分说明了避免 false sharing 的重要性。

### 其他注意事项

1. **跨平台性**：虽然 64 字节是当前主流的缓存行大小，但某些架构可能不同（例如某些嵌入式系统可能只有 32 字节）。使用 `alignas` 时应确保目标平台的特性。
2. **编译器优化**：有时编译器可能会重新排序成员变量，破坏预期的对齐。使用 `alignas` 可以强制约束，但最好结合静态断言（`static_assert`）验证对齐大小。
3. **内存屏障**：在极端性能敏感的场景，可能还需要结合内存屏障（memory barrier）指令来确保可见性，但这通常由 `std::atomic` 隐式处理。

### 总结

False sharing 是多线程编程中的隐形杀手，能轻易将并行程序的效率降低到单线程水平。通过合理使用 `alignas` 或手动填充，我们可以确保每个线程操作独立的数据行，从而充分利用多核性能。在实际开发中，应始终警惕这种"共享但非真正共享"的内存布局，尤其是在设计高频访问的并发数据结构时。

```cpp
struct PaddedCounter {
    int value{0};
    char padding[60];  // 填满到 64 字节
};
```

This approach works, but it is not as elegant as `alignas`—you must calculate the number of padding bytes yourself, and the compiler does not guarantee alignment. `alignas` is the recommended approach and has clearer semantics. Regardless of the method used, the core idea remains the same: ensure that independently written concurrent variables are separated by at least 64 bytes so they do not share the same cache line.

## OS Thread Models: From User Space to Kernel Space

Now that we've covered hardware-level caching, let's move up a layer and see how operating systems implement threads.

From the operating system's perspective, a thread is the basic unit of CPU scheduling, while a process is the basic unit of resource allocation. A process can contain multiple threads. These threads share the same address space, file descriptor table, signal handlers, and other resources, but each thread has its own independent stack, register state, and program counter. This design of "sharing most resources while executing independently" makes threads the natural vehicle for concurrency.

Threads can run "simultaneously" because the operating system implements a **context switch** mechanism: it saves the current thread's register state to memory (specifically, into the Thread Control Block, or TCB, corresponding to that thread), then restores the next thread's register state and jumps to where it left off. All of this happens in kernel space—the kernel manages thread creation, scheduling, and switching.

The operating system maintains a **Thread Control Block (TCB)** for each thread, storing the thread's complete state: register snapshots, stack pointer, program counter, scheduling priority, signal mask, and various scheduling-related metadata. A TCB itself can occupy anywhere from a few hundred bytes to several kilobytes. Combined with the default stack space per thread (8 MB on Linux), the base overhead of a single thread is significant. This is why you cannot just spawn tens of thousands of threads—the stack space alone would consume tens of gigabytes of memory.

### The Cost of Context Switching

Just how expensive is a context switch? Let's break it down. First is the **direct cost**: saving and restoring general-purpose registers (about 16 on x86-64), floating-point/SIMD registers (the AVX-512 ZMM register set has 32 512-bit registers, and saving them alone involves moving several kilobytes of data), and various system registers. This step typically takes a few microseconds.

Then there is the **indirect cost**, which is often larger than the direct cost. When switching to a new thread, the TLB (Translation Lookaside Buffer) caches the virtual-to-physical address mappings of the previous thread, which are mostly invalid for the new thread. A TLB miss triggers a page table walk, which accesses memory multiple times and is quite costly. Similarly, the new thread will access its own data, which is likely not in the current core's cache, leading to a storm of cache misses. The performance difference between a cold cache and a hot cache can be tenfold or even a hundredfold.

If you are interested in specific numbers, you can use `perf stat` on Linux to observe context switch counts, or use micro-benchmark tools like `context_switch_bench` to measure. Empirically, the total cost of a context switch (direct + indirect) ranges from a few microseconds to a few dozen microseconds, depending on the hardware and working set size. For a compute-intensive loop, if your task granularity is only a few microseconds, the overhead of context switching might exceed the actual computation—this is the hardware-level manifestation of the "task granularity too fine" issue mentioned in the previous article.

## Linux Thread Implementation: pthread, clone, and futex

The history of thread implementation in Linux is quite interesting. Early Linux kernels (before 2.4) had no native concept of threads—the kernel only knew about processes. So-called "threads" were lightweight processes created via the `clone()` system call: they shared the address space and file descriptor table with the parent process, but the kernel still viewed them as independent scheduling entities. This design was later standardized as **NPTL (Native POSIX Thread Library)**, which became the default thread implementation starting with Linux 2.6.

`clone()` is the lowest-level primitive for thread creation in Linux. You can think of it as a finely controlled version of `fork()`—`fork()` creates a completely new process (copying all resources), while `clone()` allows you to specify exactly which resources to share with the parent and which to copy. When we call `pthread_create()`, glibc internally uses `clone()` with a specific set of flags to create the new thread. These flags specify sharing the address space (`CLONE_VM`), sharing the file descriptor table (`CLONE_FILES`), sharing signal handlers (`CLONE_SIGHAND`), and so on.

You might ask: since each thread is an independent scheduling entity in the kernel, what is the relationship between pthread and `std::thread`? It's actually quite simple—`std::thread` on Linux is implemented by wrapping `pthread_create()`, which in turn wraps the `clone()` system call. So when you write `std::thread t(func)`, the call chain is: `std::thread` -> `pthread_create` -> `clone` -> kernel creates a new `task_struct`. Each layer is a thin wrapper around the next.

### futex: Fast in User Space, Slow in Kernel Space

Now that we've covered thread creation, let's talk about thread synchronization. The mutex is the most commonly used synchronization primitive, but its implementation presents a performance puzzle: if a lock isn't contended, why make a trip to the kernel? `futex` (fast userspace mutex) was designed to solve this problem.

The core idea of futex is **fast path in user space, slow path in kernel space**. When you attempt to acquire a mutex, glibc's implementation first performs an atomic operation (usually `compare-and-swap`) in user space to try to grab the lock. If the lock is free, you get it immediately without any system calls—this is the fast path, costing only a few dozen clock cycles. If the lock is held by another thread, you take the slow path: calling the `futex(FUTEX_WAIT)` system call, which asks the kernel to suspend the thread until the lock holder wakes it up via `futex(FUTEX_WAKE)`.

This design is ingenious: in the uncontended case, a mutex costs about as much as an atomic operation; you only pay the system call cost when actual contention occurs. C++'s `std::mutex` is implemented based on this mechanism on Linux. Once you understand how futex works, you will understand why "uncontended mutexes are cheap, but heavily contended mutexes are expensive"—the former happens entirely in user space, while the latter requires constant switching between user and kernel space.

## Thread Model Comparison: 1:1, M:N, and N:1

So, what is the mapping relationship between user threads and kernel threads? This is known as the thread model.

The **1:1 model** is the most intuitive—every user thread corresponds to one kernel thread. Linux's pthread (and `std::thread`) uses this model. Its advantage is simplicity: threads can run directly on multiple cores to achieve true parallelism, and blocking operations (like I/O) only block the corresponding kernel thread without affecting others. The disadvantage is that thread creation and switching are expensive (both require entering the kernel), and each kernel thread has its own stack and TCB, limiting the number of threads.

The **N:1 model** is the other extreme—multiple user threads map to a single kernel thread. Thread creation and scheduling happen entirely in user space (no system calls required), making them very lightweight and fast to switch. However, its fatal flaw is that if any user thread performs a blocking operation (like reading a file), the entire kernel thread gets stuck, and all user threads stop moving. Furthermore, because there is only one kernel thread, these user threads can only ever run on one core, lacking true parallel capability. Early "green thread" implementations used this model.

The **M:N model** attempts to get the best of both worlds—M user threads map to N kernel threads (usually M >> N). The scheduler runs in user space and assigns user threads to kernel threads for execution, maintaining lightweight characteristics while utilizing multi-core parallelism. Go's goroutine is a classic implementation of this model: goroutines are very lightweight (initial stack is only 2-8 KB), and the Go runtime scheduler assigns them to a small number of OS threads. Blocking a goroutine doesn't stall the entire thread. However, the M:N model is very complex to implement—the scheduler must handle preemption, system call wrapping, and stack switching between user and kernel space, easily introducing new problems if not careful.

For C++ programmers, `std::thread` uses the 1:1 model on all mainstream platforms. If you need a massive number of lightweight concurrent tasks, `std::thread` is not a good choice—you should consider a thread pool (a fixed number of worker threads + a task queue) or coroutines (C++20's `std::coroutine`). Thread pools and coroutines essentially build M:N scheduling strategies on top of the 1:1 model, though the scheduling logic is controlled by you or the runtime library.

Choosing the right model depends on your specific scenario. If you only have a few CPU-intensive tasks to run in parallel, just use `std::thread`—the 1:1 model is simple, reliable, and has no extra abstraction layers. If you need to handle thousands or tens of thousands of concurrent connections or tasks, a thread pool is a more pragmatic choice. (We will do some practice on this later!) And if you pursue extremely low task switching overhead and need millions of concurrent units, you must consider coroutines or an M:N runtime like Go's goroutines.

## Thread Scheduling: Who Runs First and For How Long

Finally, let's briefly discuss OS thread scheduling, which is very helpful for understanding the behavior of concurrent programs.

Modern operating systems universally use **preemptive scheduling**—the OS allocates a time slice (usually a few milliseconds to a few dozen milliseconds) to each thread. When the time slice expires, the OS forcibly switches to the next thread, whether the current thread likes it or not. This differs from cooperative scheduling, which requires threads to voluntarily yield the CPU. The benefit of preemption is that no single thread can monopolize the CPU (at least under normal circumstances); the downside is that context switches happen at unpredictable moments, which is one of the reasons concurrent bugs are hard to reproduce.

On Linux, the scheduling policy for normal threads is CFS (Completely Fair Scheduler). CFS doesn't use fixed time slices; instead, it allocates CPU time proportions based on a thread's **nice value**. The nice value ranges from -20 to +19, defaulting to 0. Lower values mean higher priority and more CPU time (but it's not strict priority—CFS pursues "fairness" rather than strict priority scheduling). You can adjust this using the `nice` command or the `setpriority()` system call.

Another useful concept is **CPU affinity**. By default, the OS scheduler can migrate threads between any core—a thread running on core A for 50ms might be scheduled to run on core B in the next time slice. This migration causes the L1/L2 caches to go completely cold. If you know a thread has a large working set and cache locality is critical, you can use `cpu_set_t` and `sched_setaffinity()` to "pin" it to a specific core, preventing the scheduler from migrating it. The code below demonstrates basic usage:

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

The C++ standard library itself does not provide an interface for setting CPU affinity (as this is a platform-specific concept), but `std::thread::native_handle()` allows us to obtain the underlying `pthread_t`. We can then use POSIX interfaces to manipulate it. In real-world high-performance scenarios, reasonably binding threads to specific cores (for example, binding a producer thread to core 0 and a consumer thread to core 1) can significantly improve performance. This reduces cache line migration across cores and lowers the RFO (Read For Ownership) overhead of the MESI protocol, which is consistent with our previous discussion on false sharing.

## Summary

In this post, we gained a deep understanding of the actual execution environment for multithreaded programs from both hardware and operating system perspectives.

At the hardware level, the hierarchical structure of CPU caches (L1/L2/L3), the 64-byte granularity of cache lines, the state transitions of the MESI protocol, and RFO requests collectively determine the actual performance of multithreaded programs. False sharing is the most common cache performance pitfall—two seemingly independent variables repeatedly triggering MESI protocol invalidations because they reside on the same cache line. `alignas(64)` is the most direct and effective solution.

At the operating system level, Linux threads are implemented using the `clone()` system call in a 1:1 model—where each user-space thread corresponds to a kernel scheduling entity. The direct cost of context switching (register saving/restoration) combined with indirect costs (TLB flushes, cache misses) makes thread switching a cost that cannot be ignored. The futex design of "fast path in user space, slow path in kernel" makes uncontended mutexes very cheap, but when contention is high, the cost of system calls becomes apparent immediately. Different threading models (1:1, M:N, N:1) each have their trade-offs. C++'s `std::thread` adopts the 1:1 model, so for massive lightweight concurrent tasks, we need to rely on thread pools or coroutines to compensate.

Now that we have a basic understanding of concurrency (ch00-01), know what problems concurrency can introduce (ch00-02), and understand how hardware and the OS support multithreading (this post), we can finally start writing code. The next post will formally introduce the interface and usage of `std::thread`.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `code/volumn_codes/vol5/ch00-concurrency-fundamentals/`.

## Exercises

### Exercise 1: Reproduce and eliminate false sharing

Compile and run the `Counters` example above (the unaligned version) and record the execution time. Then switch to the `alignas(64)` `AlignedCounter` version and compare the execution times of both. How much is the performance difference on your machine? Try increasing the number of threads to four (four independent counters) and observe if the performance difference becomes even larger.

### Exercise 2: Observe cache line size

On Linux, run `lscpu` or `cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size` to check your machine's cache line size. Then, in C++, use `std::hardware_destructive_interference_size` (C++17, defined in `<new>`) to obtain the cache line size visible at compile time. If the compiler does not support this constant, manually writing `constexpr size_t kCacheLineSize = 64;` works as well—currently, almost all mainstream platforms use 64 bytes.

### Exercise 3: Measure the overhead of context switching

Write a program that creates two threads performing a ping-pong style alternating wake-up using `std::atomic<bool>`: Thread A sets `flag = true` and then waits for `flag` to become `false`; Thread B waits for `flag` to become `true` and then sets it back to `false`. Loop for one million iterations. Use the total time divided by the number of switches to estimate the approximate cost of a single context switch. This number will include the overhead of atomic operations and context switching, but it provides a good sense of the magnitude.

## References

- [MESI protocol — Wikipedia](https://en.wikipedia.org/wiki/MESI_protocol)
- [False Sharing — Intel Developer Zone](https://www.intel.com/content/www/us/en/developer/articles/technical/avoiding-and-identifying-false-sharing-among-threads.html)
- [A futex overview and update — Ulrich Drepper (Red Hat)](https://man7.org/linux/man-pages/man7/futex.7.html)
- [The Native POSIX Thread Library for Linux — Ulrich Drepper, Ingo Molnar](https://www.akkadia.org/drepper/nptl-design.pdf)
- [CFS Scheduler Design — kernel.org](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
