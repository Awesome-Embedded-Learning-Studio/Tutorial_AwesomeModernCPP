---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: From the CPU cache hierarchy to the OS thread model, understand the
  real physical stage that multithreaded programs run on
difficulty: intermediate
order: 3
platform: host
prerequisites:
- Why We Need Concurrency
- Fundamental Concurrency Problems
reading_time_minutes: 27
related:
- std::thread Basics
- Atomic Operation Patterns
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
  translated_at: '2026-09-26T06:13:10+00:00'
  engine: anthropic
  token_count: 6800
---
# CPU Cache and OS Threads

In the previous two articles we built up two layers of understanding about concurrency: the "why" and the "what goes wrong". But there is one very practical question we have been circling around, deliberately or not: what hardware and operating system do multithreaded programs actually run on? What really happens behind the scenes when we write `std::thread t(func)`? And why is it that multithreaded programs sometimes not only fail to get faster, but actually run slower than the single-threaded version?

In this article we are going to dive down a level and take a look. We start with the CPU cache hierarchy and how cache coherence is maintained, then take on a very practical problem—false sharing—which can silently cost your multithreaded program more than half of its performance. After that we move up one level to see how the operating system implements threads, how expensive a context switch really is, and how Linux's pthread and futex work together. Once you understand all this, when we later study the memory orders of `std::atomic` or the implementation principles of mutexes, those concepts will not feel like they came out of nowhere.

## The CPU Cache Hierarchy

Before rushing into multithreading, let's think about a more basic question: why does a CPU need a cache at all?

The reason is simple—the CPU is too fast, and memory is too slow. A modern x86 CPU easily runs at several GHz, so one clock cycle is roughly 0.5–1 nanoseconds, while a single DDR4/DDR5 memory access has a latency of about 50–100 nanoseconds. In other words, if the CPU read data straight from memory, it would sit idle for hundreds of cycles waiting for the data to come back. It's like a top chef who can make 100 cuts per second, but whose refrigerator is three kilometers away—one round trip per cut, and the efficiency drops straight to zero.

The idea for fixing this bottleneck is very direct: insert a few layers of smaller, faster, but more expensive storage between the CPU and main memory, and keep frequently used data closer to the CPU. This is the famous CPU cache. Modern multicore processors usually have three cache levels, named L1, L2, and L3 from innermost to outermost.

The L1 cache sits closest to the CPU core and is split into an instruction cache (L1i) and a data cache (L1d), each private to a core. A typical L1d is 32–48 KB with a latency of about 4–5 clock cycles (this is load-use latency—the number of cycles for data to travel from L1 to a register; don't confuse it with throughput, since L1 can accept one load every cycle). This level of cache is roughly in the same speed class as registers, but its capacity is very limited.

The L2 cache is also private to each core, though it does not separate instructions from data. Typical size is 256 KB to 1 MB, with a latency of about 10–15 cycles. It acts as a buffer between L1 and L3—hot data that no longer fits in L1 spills over here.

The L3 cache is the last line of defense, shared by all cores. Typical size ranges from a few MB to a few tens of MB (server chips can even reach a hundred-plus MB), with a latency of about 30–50 cycles. Because it is shared by all cores, L3 is also the key hub for moving data between cores—when one core writes some data, the other cores have to be able to see it, and the coherence protocol is what coordinates things at this level.

You can check your machine's cache configuration with `lscpu` on Linux; the `L1d cache`, `L2 cache`, and `L3 cache` entries in the output tell you the size of each level. If you are writing multithreaded performance tests, taking a look at these numbers first is well worth it.

### Cache Lines: The Smallest Unit of the Cache

The cache does not exchange data with main memory byte by byte. It operates in units of **cache lines**, and on virtually all modern processors a line is 64 bytes. This means that when you access an address in memory, the entire 64-byte cache line gets loaded into the cache, even if you only read a single byte of it.

The logic behind this design is **spatial locality**: if you accessed address A, chances are you will soon access addresses near A. Array traversal is a classic beneficiary—when the first element is loaded, the following 15 `int`s come along into the cache, and every subsequent access is a cache hit with nearly zero latency. (Note that one `int` is 4 bytes, which is why 15 + 1 = 16 `int`s actually get loaded.)

For multithreaded programs, however, cache lines have a particularly nasty side effect—false sharing, which we will unpack in detail later. For now, just remember one number: **64 bytes**. It is the key parameter for understanding every cache-related issue that follows.

## Cache Coherence and the MESI Protocol

In the single-core era, caches were simple—only one core used the cache, data lived in exactly one place, and there was no ambiguity about reads or writes. Multicore processors broke that: each core has its own L1 and L2, so the data at the same memory address can exist simultaneously in the caches of several cores. If core A modifies a value in its own cache while core B's cache still holds the old value, how does core B ever find out the data has gone stale?

This is the problem **cache coherence** solves. Modern x86 and ARM processors widely use the **MESI protocol** (Modified / Exclusive / Shared / Invalid) to maintain cache coherence across cores. MESI assigns each cache line one of four states:

**Modified (M)**: The line has been modified by the current core and no longer matches the value in main memory. The current core holds the only valid copy of this data—if any other core's cache holds data at the same address, its state must be Invalid. When this line is evicted, it must be written back to main memory.

**Exclusive (E)**: The line matches the value in main memory, and only the current core holds it. Although the data has not been modified, being "exclusive" means the current core can modify it at any time without notifying other cores—because none of them hold a copy of it.

**Shared (S)**: The line matches main memory and may exist in the caches of multiple cores at the same time. The current core can read it, but cannot write to it directly—before writing, it must first invalidate the other cores' copies.

**Invalid (I)**: The line is invalid, equivalent to not caching anything useful. Accessing a line in the Invalid state triggers a cache miss, and the data must be reloaded from main memory or from another core's cache.

Transitions between states are driven by a bus snooping protocol or a directory-based protocol. Here is a concrete example: core A reads an address whose cache line is not in any core's cache, so the line is loaded from main memory with state Exclusive. Core B then reads the same address; the snooping mechanism on the bus notices that core A already has a copy, so both sides switch to Shared. Now core A wants to write to this address, so it first issues an **RFO (Read For Ownership)** request—meaning "I want this cache line exclusively in order to write it; other holders, please invalidate your copies". Core B receives the RFO and flips its copy to Invalid; core A gets exclusive ownership, performs the write, and the state becomes Modified.

This RFO request is one of the sources of performance overhead. In a multithreaded program, if two cores frequently write to different parts of the same cache line, RFOs fire over and over—the cache line bounces between the two cores, and every bounce costs a trip across the bus to do the invalidation. This leads straight into the false sharing we are about to discuss.

It is worth noting that the MESI protocol guarantees **cache coherence**—that is, for any single memory address, all cores eventually see a consistent value. But "cache coherent" does not mean "immediately visible"—a value written by one core may be temporarily invisible to the other cores. The reason lies not in the MESI protocol itself but in the processor's internal **store buffer**: a write first enters the store buffer, the core keeps executing subsequent instructions, and the write is committed once the cache is ready. Until the write actually enters the cache and triggers invalidation, the other cores keep seeing the old value. On the read side there is also an **invalidation queue**—received invalidation messages may sit queued waiting to be processed, which stretches the window before the new value becomes visible even further. These microarchitectural buffering mechanisms make the behavior of multithreaded programs far more complicated than the plain MESI model suggests, and they are exactly why C++'s `std::atomic` needs different `memory_order` values to control the granularity of visibility—a topic we will expand on in the later chapter on atomic operations.

## False Sharing: The Invisible Performance Killer

False sharing is, in our opinion, the most insidious performance problem of them all. Logically your code shares nothing at all—thread A only writes its own variable `a`, thread B only writes its own variable `b`, and there is no data race anywhere—yet the performance just will not go up, and can even end up slower than single-threaded. The cause: `a` and `b` happen to land on the same cache line.

Let's look at a typical case: two threads, each incrementing its own counter 100 million times.

```cpp
#include <thread>
#include <iostream>
#include <chrono>

struct Counters {
    int a;  // written by thread 1
    int b;  // written by thread 2
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

Logically, `counters.a` and `counters.b` are completely independent variables; the two threads each write their own, with no synchronization requirements whatsoever. The problem is that the `Counters` struct is only 8 bytes (two `int`s), so both members land on the same 64-byte cache line. When thread 1 (running on core A) writes `counters.a`, core A's copy of the line becomes Modified; thread 2 (running on core B) then wants to write `counters.b`, discovers that the line is in the Modified state over at core A, and issues an RFO request to invalidate core A's copy. The next time core A writes `counters.a`, it finds the line invalidated and has to pull it back... And so the line bounces back and forth a hundred million times, ping-ponging madly between the two cores.

Run it on your own machine and you will see—this code usually takes several times longer than the single-threaded version. It is entirely cache-line contention at the hardware level and has absolutely nothing to do with your code's logic, but the impact is very real. The project's `code/volumn_codes/vol5/ch00-concurrency-fundamentals/false_sharing_bench.cpp` provides a complete comparison benchmark (three versions: false sharing, alignas alignment, and single-threaded) that you can build and run directly with CMake. Here are our measured results on a WSL2 environment (x86-64, 7 cores, GCC 16.1.1, `-O2`):

| Version | Time | Notes |
|------|------|------|
| False sharing | ~500–700 ms | Two `int`s share one cache line, ping-pong between cores |
| Aligned (`alignas(64)`) | ~23–26 ms | One cache line each, true parallelism |
| Single-threaded baseline | ~47–50 ms | The two loops run sequentially |

As you can see, the false-sharing version is **15–30 times slower** than the alignas-aligned version, and even about **10 times slower** than single-threaded—while the alignas version, with both cores truly running in parallel, takes only about half the time of the single-threaded run. Note that the counters in the benchmark code use `volatile` to prevent the compiler from optimizing the entire loop away under `-O2`; the teaching code omits this, but you need to account for it when doing real measurements.

## Eliminating False Sharing: alignas and Cache-Line Padding

The idea for solving false sharing is very direct: just keep the two variables off the same cache line. In C++11 we can specify alignment with `alignas`:

```cpp
#include <thread>
#include <iostream>
#include <chrono>

// usually defined as a constant for easy reuse
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

`alignas(64)` tells the compiler that every instance of `AlignedCounter` must start at a 64-byte-aligned address. Since the cache line size is exactly 64 bytes, `counter_a` and `counter_b` each occupy one full cache line and can never land on the same one. RFOs no longer happen, and the two cores can happily write to their own cache lines without disturbing each other.

C++17 also offers a more elegant alternative: `std::hardware_destructive_interference_size`, defined in the `<new>` header. The value of this constant is "the smallest unit of alignment that can cause false sharing" on the target platform—on virtually every existing platform, that is 64. Replacing the hand-written 64 with this constant makes the code more portable. Be aware, though, that compiler support for this constant is uneven: GCC supports it from version 12 (it depends on the `__GCC_DESTRUCTIVE_SIZE` macro), but as of today Clang still has not implemented it (compile error—the variable simply is not declared; see [LLVM#60174](https://github.com/llvm/llvm-project/issues/60174)). So in real projects, hand-writing `constexpr std::size_t kCacheLineSize = 64;` is actually the safer bet.

You might ask: an `int` is only 4 bytes, and `alignas(64)` makes it take up 64 bytes—isn't that a waste of memory? Yes, it really does waste 60 bytes of space. But this is the classic **trading space for time** trade-off: 60 bytes of memory is nothing at all on a modern machine, while eliminating false sharing can multiply performance several times over. In concurrent programming, this "waste a little space to buy scalability" move is very common. You will see this pattern in many high-performance libraries and frameworks: each thread's local counter sits neatly on its own `alignas(64)` line, with a final aggregation step—it looks like a few hundred bytes wasted, but what you get back is linear multicore scaling. Run the numbers any way you like; this trade always pays off.

Another way to write it is to pad the struct manually:

```cpp
struct PaddedCounter {
    int value{0};
    char padding[60];  // pad out to 64 bytes
};
```

This approach works too, but it is less elegant than `alignas`—you have to compute how many bytes to pad yourself, and the compiler gives no alignment guarantee. `alignas` is the recommended approach and carries clearer semantics. Whichever way you go, the core idea is the same: make sure that independently written variables accessed concurrently stay at least 64 bytes apart, so they never share the same cache line.

## The OS Thread Model: From User Space to Kernel Space

That covers the hardware-level cache; now we move up one more level and look at how the operating system implements threads.

From the operating system's point of view, the thread is the basic unit of CPU scheduling, and the process is the basic unit of resource allocation. A process can contain multiple threads; these threads share the same address space, file descriptor table, signal handlers, and other resources, but each thread has its own independent stack, register state, and program counter. This design—share most resources, but each execute independently—is exactly what makes threads the natural vehicle for concurrency.

Threads can run "at the same time" because the operating system implements a **context switch** mechanism: the current thread's register state is saved to memory (specifically, into the Thread Control Block, TCB, belonging to that thread), then the next thread's register state is restored, and execution jumps to where it last paused and continues. All of this happens in kernel space—thread creation, scheduling, and switching are all managed by the kernel.

The operating system maintains a **Thread Control Block (TCB)** for each thread, which stores the thread's complete state: a register snapshot, stack pointer, program counter, scheduling priority, signal mask, and all kinds of scheduling-related metadata. The TCB itself takes anywhere from a few hundred bytes to a few KB, and on top of that comes each thread's default stack space (8 MB by default on Linux), so a thread's baseline overhead is not small. This is also why you cannot just casually spin up tens of thousands of threads—the stack space alone would eat tens of GB of memory.

### The Cost of a Context Switch

Just how expensive is a context switch? We can take it apart. First comes the **direct cost**: saving and restoring general-purpose registers (about 16 of them on x86-64), floating-point/SIMD registers (the AVX-512 ZMM register file has 32 512-bit registers—saving them alone involves moving several KB of data), plus various system registers. This step is typically on the order of a few microseconds.

Then comes the **indirect cost**, and this part is often larger than the direct one. After switching to a new thread, the TLB (Translation Lookaside Buffer, the page-table cache) holds the previous thread's virtual-to-physical address mappings, most of which are invalid as far as the new thread is concerned. A TLB miss triggers a page table walk, and each walk accesses memory several times—not cheap. Likewise, the new thread accesses its own data as it executes, and that data is most likely not in the current core's cache, causing a storm of cache misses. The performance gap between a cold cache and a warm one can be a factor of ten or even a hundred.

If you are interested in concrete numbers, on Linux you can watch context-switch counts with `perf stat`, or measure them with a microbenchmark tool such as `context_switch_bench`. As a rule of thumb, the total cost of one context switch (direct + indirect) is roughly between a few microseconds and a few tens of microseconds, depending on the hardware and the working set size. For a compute-intensive loop, if your task granularity is only a few microseconds, the context-switch overhead can be larger than the actual computation—this is the hardware-level face of the "tasks too fine-grained" problem from the previous article.

## Linux's Thread Implementation: pthread, clone, and futex

Linux's thread implementation has quite an interesting history. Early Linux kernels (before 2.4) had no native notion of threads—the kernel only knew processes. The so-called "threads" were lightweight processes created through the `clone()` system call: they shared the address space, file descriptor table, and other resources with the parent, but as far as the kernel was concerned each was still an independent scheduling entity. This design was later standardized as the **NPTL (Native POSIX Thread Library)**, which became the default thread implementation starting from Linux 2.6.

`clone()` is Linux's lowest-level thread creation primitive. You can think of it as a fine-grained control version of `fork()`—`fork()` creates a brand-new process (copying every resource), while `clone()` lets you specify precisely which resources are shared with the parent and which get copied. When we call `pthread_create()`, glibc internally creates the new thread via `clone()` plus a specific set of flags—flags that request sharing the address space (`CLONE_VM`), sharing the file descriptor table (`CLONE_FILES`), sharing the signal handlers (`CLONE_SIGHAND`), and so on.

You might ask: since every thread is an independent scheduling entity in the kernel, what is the relationship between pthread and `std::thread`? It's actually quite simple—the implementation of `std::thread` on Linux is a wrapper around `pthread_create()`, and `pthread_create()` in turn wraps the `clone()` system call. So when you write `std::thread t(func)`, the call chain is: `std::thread` -> `pthread_create` -> `clone` -> the kernel creates a new task_struct. Each layer is a thin wrapper around the one below it.

### futex: Fast in User Space, Slow in the Kernel

That was thread creation; now let's talk about thread synchronization. The mutex is the most commonly used synchronization primitive, but its implementation has a performance puzzle: if the lock is not contended, why make a trip into the kernel at all? `futex` (fast userspace mutex) was designed precisely to solve this problem.

The core idea of futex is that **the fast path completes in user space, and only the slow path enters the kernel**. When you try to acquire a mutex, glibc's implementation first performs an atomic operation in user space (usually `compare-and-swap`) to try to take the lock. If the lock is free, you take it directly, with no system call at all—this is the fast path, and the overhead is only a few dozen clock cycles. If the lock is held by another thread, the slow path takes over: call the `futex(FUTEX_WAIT)` system call and let the kernel suspend this thread, until the lock holder wakes it via `futex(FUTEX_WAKE)`.

This design is wonderfully clever: in the uncontended case, a mutex costs roughly one atomic operation; only when real contention occurs do you pay the price of a system call. C++'s `std::mutex` is implemented on top of exactly this mechanism on Linux. Once you understand how futex works, you understand why "an uncontended mutex is cheap, but a heavily contended mutex is expensive"—the former completes entirely in user space, while the latter has to bounce back and forth between user space and kernel space every single time.

## Comparing Thread Models: 1:1, M:N, N:1

So the next question arrives: what is the mapping relationship between user-space threads and kernel threads? That is what we call the thread model.

The **1:1 model** is the most intuitive—every user-space thread corresponds to one kernel thread. Linux's pthread (and `std::thread`) is this model. Its advantage is simplicity: threads can run directly on multiple cores for true parallelism, and a blocking operation (such as I/O) blocks only the corresponding kernel thread, leaving other threads unaffected. The disadvantage is that thread creation and switching are expensive (both have to enter the kernel), and every kernel thread has its own stack and TCB, so the number of threads is limited.

The **N:1 model** is the other extreme—many user-space threads all mapped onto a single kernel thread. Thread creation and scheduling are done entirely in user space (no system calls needed), so threads are extremely lightweight and switching is fast. But its fatal flaw is this: if any one user-space thread performs a blocking operation (reading a file, say), the entire kernel thread is stuck, and none of the user-space threads can move. And because there is only one kernel thread, these user-space threads can forever run on only one core—no true parallelism. Some early green thread implementations were this model.

The **M:N model** tries to get the best of both worlds—M user-space threads mapped onto N kernel threads (usually M >> N). The scheduler runs in user space and assigns user-space threads onto kernel threads for execution, keeping things lightweight while still exploiting multicore parallelism. Go's goroutine is the classic implementation of this model: a goroutine is extremely lightweight (its initial stack is only 2–8 KB), the Go runtime's scheduler takes care of distributing them onto a small number of OS threads, and a blocked goroutine does not stall the entire thread. But the implementation complexity of the M:N model is very high—the scheduler has to handle preemption, wrapping system calls, and stack switching between user space and kernel space, and one careless step introduces brand-new problems.

For C++ programmers, `std::thread` is a 1:1 model on all mainstream platforms. If you need large numbers of lightweight concurrent tasks, `std::thread` is not a good choice—you should be looking at thread pools (a fixed number of worker threads + a task queue) or coroutines (C++20's `std::coroutine`). Thread pools and coroutines are, in essence, both M:N scheduling strategies built on top of the 1:1 model; the only difference is that the scheduling logic is controlled by you or by a runtime library.

Which model to choose depends on your concrete scenario. If you only have a few CPU-intensive tasks to run in parallel, just use `std::thread`—the 1:1 model is simple and reliable, with no extra abstraction layers. If you need to handle thousands or tens of thousands of concurrent connections or tasks, a thread pool is the more pragmatic choice. (We will get to write exactly this in the exercises!) And if you are chasing extremely low task-switching overhead and need concurrency units at the million scale, then coroutines—or an M:N runtime in the style of Go's goroutines—are what you have to consider.

## Thread Scheduling: Who Runs First, and for How Long

Finally, a quick chat about operating system thread scheduling; this material helps a lot in understanding the behavior of concurrent programs.

Modern operating systems universally adopt **preemptive scheduling**—the operating system gives each thread a time slice (usually a few milliseconds to a few tens of milliseconds), and when the slice is used up, it forcibly switches to the next thread, regardless of whether the current thread is willing. This differs from cooperative scheduling, where threads are required to yield the CPU voluntarily. The benefit of preemption is that no single thread can monopolize the CPU (at least under normal circumstances); the drawback is that context switches happen at moments you cannot predict, which is one of the reasons concurrency bugs are so hard to reproduce.

On Linux, the scheduling policy for ordinary threads is CFS (Completely Fair Scheduler). CFS does not use fixed time slices; instead, it apportions CPU time according to each thread's **nice value**. The nice value ranges from -20 to +19, with a default of 0; the lower the value, the higher the priority, and the more CPU time the thread gets (but it is not a strict priority—CFS pursues "fairness", not strict priority scheduling). You can adjust it with the `nice` command or the `setpriority()` system call.

Another useful concept is **CPU affinity**. By default, the operating system's scheduler can migrate a thread between any cores—a thread that ran 50 ms on core A might be scheduled onto core B for its next time slice. Such migrations leave the L1/L2 caches completely cold. If you know that a certain thread has a large working set and that cache locality matters, you can use `cpu_set_t` and `sched_setaffinity()` to "pin" it to a fixed core and stop the scheduler from migrating it. The code below shows the basic usage:

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

The C++ standard library itself provides no interface for setting CPU affinity (this is a platform-dependent concept), but `std::thread::native_handle()` can hand you the underlying `pthread_t`, and from there you can operate on it with the POSIX interfaces. In real high-performance scenarios, binding threads to cores sensibly (say, pinning the producer thread to core 0 and the consumer thread to core 1) can noticeably improve performance—fewer cross-core cache-line migrations, less RFO overhead in the MESI protocol, very much in the same vein as the false sharing we discussed earlier.

## Summary

In this article we took a deep look, from both the hardware and the operating system levels, at the real stage that multithreaded programs run on. At the hardware level, the L1/L2/L3 hierarchy of the CPU cache, the 64-byte granularity of cache lines, the state transitions of the MESI protocol, and RFO requests—these mechanisms determine the actual performance of multithreaded programs. False sharing is the easiest cache performance trap to step into—two seemingly independent variables repeatedly trigger MESI protocol invalidations because they land on the same cache line, and `alignas(64)` is the most direct and effective fix.

At the operating system level, Linux's threads implement the 1:1 model through the `clone()` system call—every user-space thread corresponds to one kernel scheduling entity. The direct cost of a context switch (saving/restoring registers) plus the indirect cost (TLB flush, cache misses) makes thread switching a cost that cannot be ignored. Futex's "fast path in user space, slow path into the kernel" design makes an uncontended mutex very cheap, but under heavy contention the price of system calls shows up quickly. The different thread models (1:1, M:N, N:1) each come with trade-offs; C++'s `std::thread` adopts the 1:1 model, and for large numbers of lightweight concurrent tasks you need the help of thread pools or coroutines to fill the gap.

We now have the basic rationale of concurrency (ch00-01), we know what goes wrong with concurrency (ch00-02), and we understand how hardware and the OS support multithreading (this article). Next, we can finally get our hands on some code—the next article formally introduces the interface and usage of `std::thread`.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch00-concurrency-fundamentals/`.

## Exercises

### Exercise 1: Reproduce and Eliminate False Sharing

Compile and run the `Counters` example above (the unaligned version) and record the execution time. Then switch to the `alignas(64)` `AlignedCounter` version and compare the execution times of the two. How large is the performance difference on your machine? Try increasing the number of threads to 4 (4 independent counters) and observe whether the performance difference gets bigger.

### Exercise 2: Inspect the Cache Line Size

On Linux, run `lscpu` or `cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size` to check your machine's cache line size. Then, in C++, use `std::hardware_destructive_interference_size` (C++17, defined in `<new>`) to obtain the cache line size as visible at compile time. If the compiler does not support this constant, hand-writing `constexpr size_t kCacheLineSize = 64;` works too—virtually all mainstream platforms are 64 bytes today.

### Exercise 3: Measure the Cost of a Context Switch

Write a program that creates two threads and uses `std::atomic<bool>` to do a ping-pong style of alternating wake-ups: thread A sets `flag = true` and then waits for `flag` to turn back to `false`; thread B waits for `flag` to become `true` and then sets it back to `false`; loop one million times. Divide the total time by the number of switches to estimate the approximate cost of one context switch. This number includes both the atomic-operation overhead and the context-switch overhead, but it gives you a feel for the order of magnitude.

## References

- [MESI protocol — Wikipedia](https://en.wikipedia.org/wiki/MESI_protocol)
- [False Sharing — Intel Developer Zone](https://www.intel.com/content/www/us/en/developer/articles/technical/avoiding-and-identifying-false-sharing-among-threads.html)
- [A futex overview and update — Ulrich Drepper (Red Hat)](https://man7.org/linux/man-pages/man7/futex.7.html)
- [The Native POSIX Thread Library for Linux — Ulrich Drepper, Ingo Molnar](https://www.akkadia.org/drepper/nptl-design.pdf)
- [CFS Scheduler Design — kernel.org](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
