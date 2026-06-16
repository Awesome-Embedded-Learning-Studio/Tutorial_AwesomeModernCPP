---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: From the hardware cache hierarchy to the OS thread model, understanding
  the real physical stage where multithreaded programs execute
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
  source_hash: e4b36e66ac07f5f61498162766eb183cd10d2ce1048a12fb15daf4237d52eb29
  translated_at: '2026-06-16T04:03:30.337233+00:00'
  engine: anthropic
  token_count: 3818
---
# CPU Cache and OS Threads

In the previous two articles, we established the "why" and the "what can go wrong" layers of understanding for concurrency. But there is a very practical issue that we have intentionally or unintentionally skirted around: what kind of hardware and operating system do multithreaded programs actually run on? What happens behind the scenes when we write `std::thread`? Why do multithreaded programs sometimes run slower than their single-threaded counterparts instead of faster?

In this article, we will dive into a lower level to investigate. We will start with the hierarchy of the CPU cache to understand how cache coherence is maintained, and then tackle a very practical problem—false sharing—which can silently cost your multithreaded program more than half its performance. After that, we will move up a layer to see how the operating system implements threads, how expensive context switching really is, and how Linux's pthread and futex work together. With this understanding, when we later study C++ atomics' memory ordering and the implementation principles of mutexes, those concepts won't seem like they appeared out of thin air.

## CPU Cache Hierarchy

Before rushing to look at multithreading, let's consider a more basic question: Why does a CPU need cache?

The reason is simple—the CPU is too fast, and memory is too slow. A modern x86 CPU has a clock speed of several GHz, with each clock cycle being about 0.5–1 nanosecond; whereas a single DDR4/DDR5 memory access takes about 50–100 nanoseconds. This means that if the CPU reads data directly from memory, it spins its wheels for hundreds of cycles waiting for the data to return. It's like a top chef who can chop 100 times per second, but the fridge is three kilometers away—running back and forth for every chop results in zero efficiency.

The solution to this bottleneck is straightforward: add layers of smaller, faster, but more expensive storage between the CPU and main memory to keep frequently used data closer to the CPU. This is the famous CPU cache. Modern multi-core processors usually have three levels of cache, known as L1, L2, and L3 from the inside out.

The L1 cache is closest to the CPU core and is split into instruction cache (L1i) and data cache (L1d), with each core having its own. A typical L1d size is 32–48 KB, with a latency of about 4–5 clock cycles (this is load-use latency—the number of cycles it takes for data to travel from L1 to a register; don't confuse this with throughput, as L1 can accept one load per cycle). The speed of this cache layer is on the same order of magnitude as registers, but the capacity is very limited.

The L2 cache is also per-core, but it does not distinguish between instructions and data. Typical sizes range from 256 KB to 1 MB, with a latency of about 10–15 cycles. It acts as a buffer between L1 and L3—hot data that doesn't fit in L1 spills over here.

The L3 cache is the last line of defense shared by all cores. Typical sizes range from a few MB to tens of MB (server chips can even reach hundreds of MB), with a latency of about 30–50 cycles. Because it is shared by all cores, L3 is also a key hub for inter-core data transfer—when one core writes data, other cores need to see it, and the coherence protocol coordinates at this level.

You can use `lscpu` on Linux to view your machine's cache configuration; the output for `L1d cache`, `L2 cache`, and `L3 cache` will tell you the size of each level. If you are writing multithreaded performance tests, taking a quick look at these numbers is very helpful.

### Cache Line: The Minimum Unit of Cache

The cache does not exchange data with main memory byte by byte. It operates in units of **cache lines**, which are 64 bytes on almost all modern processors. This means that when you access a memory address, the entire 64-byte cache line is loaded into the cache, even if you only read one byte.

The logic behind this design is **spatial locality**: if you accessed address A, there is a high probability you will soon access an address near A. Array traversal is a typical beneficiary scenario—when the first element is loaded, the next 15 `int`s are loaded into the cache along with it, making subsequent accesses cache hits with almost zero latency. (Note, 1 `int` is 4 bytes, which is why 15 + 1 = 16 `int`s are actually loaded).

However, for multithreaded programs, cache lines have a very annoying side effect—false sharing, which we will expand on shortly. For now, just remember one number: **64 bytes**. This is the key parameter to understanding all subsequent cache-related issues.

## Cache Coherence and the MESI Protocol

In the single-core era, caching was simple—only one core was using it, data existed in only one place, and there was no ambiguity in reading or writing. But multi-core processors broke this assumption: each core has its own L1 and L2, and data from the same memory address can exist in the caches of multiple cores simultaneously. If core A modifies a value in its cache, but core B still has the old value in its cache, how does it know the data has expired?

This is the problem that **cache coherence** solves. Modern x86 and ARM processors widely use the **MESI protocol** (Modified / Exclusive / Shared / Invalid) to maintain cache coherence between cores. MESI assigns one of four states to each cache line:

**Modified (M)**: This cache line has been modified by the current core and is inconsistent with the value in main memory. The current core is the only one holding a valid copy of this data—if other cores have data for the same address in their cache, it must be in the Invalid state. When this cache line is evicted, it must be written back to main memory.

**Exclusive (E)**: This cache line is consistent with main memory, and only the current core holds it. Although the data hasn't been modified, "exclusive" means the current core can modify it at any time without notifying other cores—because no other core holds a copy.

**Shared (S)**: This cache line is consistent with main memory and may exist in the caches of multiple cores simultaneously. The current core can read it, but cannot write directly—it must first invalidate the copies on other cores before writing.

**Invalid (I)**: This cache line is invalid, equivalent to not caching any useful data. Accessing a cache line in the Invalid state triggers a cache miss, requiring it to be reloaded from main memory or another core's cache.

State transitions are driven by snooping protocols on the bus or directory-based protocols. Here is a specific example: Core A reads an address, the cache line is not in any core's cache, it loads from main memory, and the state is set to Exclusive. Core B also reads the same address; the snooping mechanism on the bus discovers that Core A already has a copy, so both sides change their state to Shared. Then Core A wants to write to this address; it first issues an **RFO (Read For Ownership)** request—meaning "I want to own this cache line exclusively to write to it, please other holders invalidate your copies." After receiving the RFO, Core B changes its cache line state to Invalid; Core A obtains exclusive ownership, performs the write, and the state becomes Modified.

This RFO request is one of the sources of performance overhead. In multithreaded programs, if two cores frequently write to different locations on the same cache line, it will repeatedly trigger RFOs—the cache line bounces back and forth between the two cores, requiring the bus to perform invalidations every time. This leads us to our next topic: false sharing.

It is worth mentioning that the MESI protocol guarantees **cache coherence**—that is, for any single memory address, all cores will eventually see a consistent value. However, "cache coherent" does not mean "immediately visible"—a value written by one core may not be seen by other cores immediately. The reason is not the MESI protocol itself, but the **store buffer** inside the processor: write operations enter the store buffer first, and the core can continue executing subsequent instructions, waiting for the cache to be ready before committing the write. Before the write actually enters the cache and triggers invalidation, other cores continue to see the old value. Additionally, on the reading side, there is also an **invalidation queue**—received invalidation messages may be queued waiting to be processed, which further lengthens the time window for "new values to become visible." These micro-architectural buffering mechanisms make the behavior of multithreaded programs much more complex than the simple MESI model, which is why C++ `std::atomic` needs different memory orders to control the granularity of visibility—we will expand on this topic in the later chapter on atomic operations.

## False Sharing: The Invisible Performance Killer

False sharing is, in my opinion, the most "insidious" performance problem. Your code logic has absolutely no sharing—Thread A only writes to its own variable `counter0`, Thread B only writes to its own variable `counter1`, there is no data race—but the performance just won't go up, it's even slower than single-threaded. The reason is that `counter0` and `counter1` happen to fall on the same cache line.

Let's look at a typical case: two threads each increment a counter 100 million times.

```cpp
struct Counter {
    int counter0;
    int counter1;
};

Counter c;

void thread0() {
    for (int i = 0; i < 100'000'000; ++i) {
        c.counter0++;
    }
}

void thread1() {
    for (int i = 0; i < 100'000'000; ++i) {
        c.counter1++;
    }
}
```

Logically, `counter0` and `counter1` are completely independent variables, two threads writing to their own, no synchronization needed. But the problem is that the `Counter` struct is only 8 bytes (two `int`s), so both members fall on the same 64-byte cache line. When Thread 1 (running on Core A) writes to `counter0`, Core A's cache line state becomes Modified; Thread 2 (running on Core B) wants to write to `counter1`, discovers this cache line is in the Modified state on Core A, and issues an RFO request to invalidate Core A's copy. When Core A writes to `counter0` again, it finds the cache line has been invalidated and has to pull it back in... and so on, bouncing back and forth 100 million times, the cache line frantically ping-ponging between the two cores.

Run it on your own machine and you will see—the execution time of this code is usually several times slower than the single-threaded version. This is entirely due to hardware-level cache line contention, having nothing to do with your code logic, but its impact is very real. This project's `demo_cache_bench` provides a complete comparison benchmark (including false sharing, `alignas` aligned, and single-threaded versions), which can be compiled and run directly with CMake. Here are the author's actual test results in a WSL2 environment (x86-64, 7 cores, GCC 16.1.1, `-O2`):

| Version | Time | Description |
|---------|------|-------------|
| False sharing | ~500–700 ms | Two `int`s share a cache line, ping-ponging between cores |
| Aligned (`alignas(64)`) | ~23–26 ms | Each occupies a cache line, truly parallel |
| Single-threaded baseline | ~47–50 ms | Sequential execution of two loops |

You can see that the false sharing version is **15–30 times slower** than the `alignas` aligned version, and even about **10 times slower** than the single-threaded version—while the `alignas` version, because the two cores are truly parallel, takes only about half the time of the single-threaded version. Note that the counter in the test code uses `std::atomic<int>` with `memory_order_relaxed` to prevent the compiler from optimizing away the entire loop under `-O2`; the teaching code omits this, but it needs to be considered when actually measuring.

## Eliminating False Sharing: `alignas` and Cache Line Padding

The idea for solving false sharing is straightforward: just make sure the two variables aren't on the same cache line. In C++11, we can use `alignas` to specify alignment:

```cpp
struct alignas(64) Counter {
    int counter0;
    int counter1;
};
```

`alignas(64)` tells the compiler that each instance of `Counter` must start at a 64-byte aligned address. Because the cache line size is 64 bytes, `counter0` and `counter1` each occupy a full cache line and cannot fall on the same one. RFOs no longer occur, and the two cores can happily write to their own cache lines without interfering with each other.

C++17 also provides a more elegant alternative: `std::hardware_destructive_interference_size`, defined in the `<new>` header file. The value of this constant is the "minimum alignment unit that causes false sharing" on the target platform—on almost all existing platforms, it is 64. Using this constant instead of hand-writing 64 makes the code more portable. However, note that compiler support for this constant varies—GCC has it available from version 12 onwards (relying on the `__STDCPP_DEFAULT_NEW_ALIGNMENT__` macro), but Clang has not implemented it as of now (compilation error—the variable is simply not declared, see [LLVM#60174](https://github.com/llvm/llvm-project/issues/60174)), so in actual projects, hand-writing `alignas(64)` is actually safer.

You might ask: an `int` is only 4 bytes, `alignas(64)` makes it take up 64 bytes, isn't that a waste of memory? Yes, it does waste 60 bytes of space. But this is a typical **space-for-time** tradeoff—60 bytes of memory is nothing on a modern machine, but eliminating false sharing can improve performance several times over. In concurrent programming, this practice of "wasting a little space to buy scalability" is very common. You will see this pattern in many high-performance libraries and frameworks: each thread's local counter is `alignas`-ed nicely, and finally aggregated—it looks like it wastes a few hundred bytes, but it buys linear multi-core scalability, a trade that is always worth it.

There is another way to write this, which is to manually pad inside the struct:

```cpp
struct Counter {
    int counter0;
    char padding0[60];
    int counter1;
    char padding1[60];
};
```

This method also works, but it is not as elegant as `alignas`—you need to calculate the padding bytes yourself, and the compiler does not guarantee alignment. `alignas` is the recommended approach, and the semantics are clearer. Regardless of the method used, the core idea is the same: ensure that independently written variables in concurrent execution are separated by at least 64 bytes so they do not share the same cache line.

## OS Thread Model: From User Space to Kernel Space

Having discussed hardware-level caching, let's move up a layer and see how the operating system implements threads.

From the operating system's perspective, threads are the basic unit of CPU scheduling, and processes are the basic unit of resource allocation. A process can contain multiple threads; these threads share the same address space, file descriptor table, signal handlers, and other resources, but each thread has its own independent stack, register state, and program counter. This design of "sharing most resources but executing independently" makes threads the natural vehicle for implementing concurrency.

The reason threads can run "simultaneously" is that the operating system implements a **context switch** mechanism: saving the current thread's register state to memory (specifically, saving it to the Thread Control Block, TCB, corresponding to this thread), then restoring the next thread's register state and jumping to where it left off to continue execution. All of this happens in kernel space—thread creation, scheduling, and switching are all managed by the kernel.

The operating system maintains a **Thread Control Block (TCB)** for each thread, which stores the thread's complete state: register snapshot, stack pointer, program counter, scheduling priority, signal mask, and various scheduling-related metadata. The TCB itself takes up a few hundred bytes to a few KB, plus the default stack space for each thread (8 MB on Linux), so the base overhead of a thread is not small. This is also why you can't just spawn tens of thousands of threads—the stack space alone would eat up tens of GB of memory.

### The Cost of Context Switching

How expensive is a context switch really? We can break it down. First is the **direct cost**: saving and restoring general-purpose registers (about 16 general-purpose registers on x86-64), floating-point/SIMD registers (the AVX-512 ZMM register set has 32 512-bit registers, saving them alone involves moving several KB of data), and various system registers. This step is usually on the order of a few microseconds.

Then there is the **indirect cost**, which is often larger than the direct cost. After switching to a new thread, the TLB (Translation Lookaside Buffer, page table cache) contains the virtual-to-physical address mappings of the previous thread, which are mostly invalid for the new thread. A TLB miss triggers a page table walk, which accesses memory several times per walk, at a significant cost. Similarly, when the new thread executes, it will access its own data, which is likely not in the current core's cache, causing a round of cache misses. The performance gap between a cold cache and a hot cache can be tenfold or even a hundredfold.

If you are interested in specific numbers, you can use `vmstat` or `pidstat` on Linux to observe the number of context switches, or use micro-benchmark tools like `google-benchmark` to measure. Empirically, the total cost of a context switch (direct + indirect) is between a few microseconds and a few dozen microseconds, depending on hardware and working set size. For a compute-intensive loop, if your task granularity is only a few microseconds, the overhead of context switching might be greater than the actual computation—this is the hardware-level manifestation of the "task granularity too fine" problem mentioned in the previous article.

## Linux Thread Implementation: pthread, clone, and futex

Linux's thread implementation has an interesting history. Early Linux kernels (before 2.4) did not have a native concept of threads—the kernel only knew about processes. The so-called "threads" were lightweight processes created via the `clone` system call: they shared the address space, file descriptor table, and other resources with the parent process, but were still independent scheduling entities in the kernel's eyes. This design was later standardized as **NPTL (Native POSIX Thread Library)** and became the default thread implementation starting with Linux 2.6.

`clone` is the lowest-level thread creation primitive in Linux. You can understand it as a finely controlled version of `fork`—`fork` creates a completely new process (copying all resources), while `clone` allows you to precisely specify which resources are shared with the parent process and which are copied. When we call `pthread_create`, glibc internally uses `clone` with a specific set of flags to create the new thread; these flags specify sharing the address space (`CLONE_VM`), sharing the file descriptor table (`CLONE_FILES`), sharing signal handlers (`CLONE_SIGHAND`), and so on.

You might ask: since each thread is an independent scheduling entity in the kernel, what is the relationship between pthread and `fork`? It's actually simple—`pthread_create` on Linux is implemented by wrapping `clone`, which in turn wraps the `clone` system call. So when you write `pthread_create`, the call chain is: `pthread_create` -> `clone` -> `sys_clone` -> kernel creates a new `task_struct`. Each layer is a thin wrapper around the next.

### futex: Fast in User Space, Slow in Kernel Space

Having talked about thread creation, let's talk about thread synchronization. The mutex is the most commonly used synchronization primitive, but its implementation presents a performance puzzle: if the lock is not contended, why make a trip to the kernel? `futex` (fast userspace mutex) is designed to solve this problem.

The core idea of futex is **fast path in user space, slow path in kernel space**. When you try to acquire a mutex, glibc's implementation first performs an atomic operation in user space (usually `atomic_compare_exchange`) to try to acquire the lock. If the lock is free, you get it directly without any system calls—this is the fast path, with an overhead of only a few dozen clock cycles. If the lock is held by another thread, you take the slow path: calling the `futex` system call to let the kernel suspend the thread until the lock holder wakes it up via `futex` (specifically `FUTEX_WAKE`).

This design is ingenious: in the uncontended case, the mutex overhead is close to a single atomic operation; the cost of a system call is paid only when contention actually occurs. C++'s `std::mutex` is implemented based on this mechanism on Linux. Understanding how futex works explains why "uncontended mutexes are cheap, but heavily contended mutexes are expensive"—the former happens entirely in user space, while the latter requires constant switching between user space and kernel space.

## Thread Model Comparison: 1:1, M:N, N:1

Next question: what is the mapping relationship between user-space threads and kernel-space threads? This is the so-called thread model.

The **1:1 model** is the most intuitive—every user-space thread corresponds to one kernel thread. Linux's pthread (as well as `std::thread`) is this model. Its advantage is simplicity: threads can run directly on multiple cores to achieve true parallelism, and blocking operations (like I/O) only block the corresponding kernel thread without affecting other threads. The disadvantage is that thread creation and switching overhead is large (both must enter the kernel), and each kernel thread has its own stack and TCB, limiting the number of threads.

The **N:1 model** is the other extreme—multiple user-space threads are all mapped to a single kernel thread. Thread creation and scheduling are completed entirely in user space (no system calls needed), so it is very lightweight and fast to switch. But its fatal problem is: if any user-space thread performs a blocking operation (like reading a file), the entire kernel thread gets stuck, and all user-space threads can't move. Moreover, because there is only one kernel thread, these user-space threads can only ever run on one core, with no true parallel capability. Some early green thread implementations were this model.

The **M:N model** attempts to get the best of both worlds—M user-space threads mapped to N kernel threads (usually M >> N). The scheduler runs in user space, assigning user-space threads to kernel threads for execution, maintaining both lightweight characteristics and the ability to utilize multi-core parallelism. Go's goroutine is a classic implementation of this model: goroutines are very lightweight (initial stack is only 2–8 KB), and Go's runtime scheduler is responsible for assigning them to a small number of OS threads; a blocked goroutine won't stall the entire thread. However, the M:N model is very complex to implement—the scheduler needs to handle preemption, system call wrapping, and stack switching between user space and kernel space, easily introducing new problems if not careful.

For C++ programmers, `std::thread` is a 1:1 model on all mainstream platforms. If you need a large number of lightweight concurrent tasks, `std::thread` is not a good choice—you should consider thread pools (a fixed number of worker threads + task queues) or coroutines (C++20 coroutines). Thread pools and coroutines essentially build M:N scheduling strategies on top of the 1:1 model, except the scheduling logic is controlled by you or the runtime library.

Choosing which model depends on your specific scenario. If you only have a few CPU-intensive tasks to run in parallel, just use `std::thread`—the 1:1 model is simple and reliable, with no extra abstraction layers. If you need to handle thousands or even tens of thousands of concurrent connections or tasks, a thread pool is a more pragmatic choice. (We will do some exercises on this!). And if you pursue extremely low task switching overhead and need millions of concurrent units, then you have to consider coroutines or an M:N runtime like Go's goroutines.

## Thread Scheduling: Who Runs First, and for How Long

Finally, let's briefly talk about OS thread scheduling; this content is very helpful for understanding the behavior of concurrent programs.

Modern operating systems generally use **preemptive scheduling**—the OS allocates a time slice (time slice, usually a few milliseconds to a few dozen milliseconds) to each thread, and when the time slice is up, it forcibly switches to the next thread, whether the current thread likes it or not. This is different from cooperative scheduling, which requires threads to voluntarily yield the CPU. The benefit of preemptive scheduling is that no single thread can monopolize the CPU (at least under normal circumstances); the downside is that context switches happen at moments you cannot predict, which is one of the reasons concurrent bugs are hard to reproduce.

On Linux, the scheduling policy for ordinary threads is CFS (Completely Fair Scheduler). CFS does not use fixed time slices but allocates CPU time proportions based on the thread's **nice value**. The nice value ranges from -20 to +19, defaulting to 0; the lower the value, the higher the priority, and the more CPU time can be allocated (but it's not strict priority—CFS pursues "fairness" rather than strict priority scheduling). You can adjust this with the `nice` command or the `setpriority` system call.

Another useful concept is **CPU affinity**. By default, the OS scheduler can migrate threads between any cores—a thread that ran on Core A for 50ms might be scheduled to run on Core B in the next time slice. This migration causes the entire L1/L2 cache to go cold. If you know a thread has a large working set and cache locality is important, you can use `pthread_setaffinity_np` and `CPU_SET` to "bind" it to a fixed core, preventing the scheduler from migrating it. The code below shows basic usage:

```cpp
#include <pthread.h>
#include <cstdio>

void* thread_func(void* arg) {
    // Thread work here
    return nullptr;
}

int main() {
    pthread_t thread;
    pthread_create(&thread, nullptr, thread_func, nullptr);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // Bind to core 0

    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

    pthread_join(thread, nullptr);
    return 0;
}
```

The C++ standard library itself does not provide an interface to set CPU affinity (this is a platform-specific concept), but `std::thread::native_handle` can get the underlying `pthread_t`, and then you can use POSIX interfaces to operate on it. In actual high-performance scenarios, reasonably binding threads to cores (for example, binding the producer thread to core 0 and the consumer thread to core 1) can significantly improve performance—reducing cross-core cache line migration and lowering the RFO overhead of the MESI protocol, which is consistent with our earlier discussion of false sharing.

## Summary

In this article, we gained a deep understanding of the real stage where multithreaded programs run from both hardware and operating system perspectives. At the hardware level, the CPU cache's L1/L2/L3 hierarchy, the 64-byte granularity of cache lines, the state transitions of the MESI protocol, and RFO requests determine the actual performance of multithreaded programs. False sharing is the easiest cache performance trap to fall into—two seemingly independent variables repeatedly trigger MESI protocol invalidations because they fall on the same cache line, and `alignas` is the most direct and effective solution.

At the operating system level, Linux's threads are a 1:1 model implemented via the `clone` system call—each user-space thread corresponds to a kernel scheduling entity. The direct cost of context switching (register save/restore) plus indirect costs (TLB flush, cache miss) make thread switching a non-negligible cost. futex's "fast path in user space, slow path in kernel space" design makes uncontended mutexes very cheap, but when contention is high, the cost of system calls quickly becomes apparent. Different thread models (1:1, M:N, N:1) have their own trade-offs; C++'s `std::thread` adopts the 1:1 model, and for a large number of lightweight concurrent tasks, thread pools or coroutines are needed to compensate.

Now we have a basic understanding of concurrency (ch00-01), know what problems concurrency can cause (ch00-02), and understand how hardware and the OS support multithreading (this article). The next step, we can finally write code—the next article will formally introduce the interface and usage of C++ `std::thread`.

> 💡 Complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `demo_cache_bench`.

## Exercises

### Exercise 1: Reproduce and Eliminate False Sharing

Compile and run the `demo_cache_bench` example above (unaligned version) and record the execution time. Then switch to the `alignas(64)` aligned version and compare the execution times. How much is the performance difference on your machine? Try increasing the number of threads to 4 (4 independent counters) and observe if the performance difference is even larger.

### Exercise 2: Observe Cache Line Size

Run `getconf LEVEL1_DCACHE_LINESIZE` or `lscpu` on Linux to view your machine's cache line size. Then use `std::hardware_destructive_interference_size` in C++ (C++17, defined in `<new>`) to get the cache line size visible at compile time. If the compiler does not support this constant, hand-writing `alignas(64)` is also fine—on almost all current mainstream platforms, it is 64 bytes.

### Exercise 3: Measure the Cost of Context Switching

Write a program that creates two threads and performs ping-pong style alternating wakeups using `std::atomic`: Thread A sets `flag0` then waits for `flag1` to become `true`, Thread B waits for `flag0` to become `true` then sets `flag1` back, looping 1 million times. Divide the total time by the number of switches to estimate the approximate cost of one context switch. This number will include the overhead of atomic operations and context switching, but it gives a sense of the order of magnitude.

## Reference Resources

- [MESI protocol — Wikipedia](https://en.wikipedia.org/wiki/MESI_protocol)
- [False Sharing — Intel Developer Zone](https://www.intel.com/content/www/us/en/developer/articles/technical/avoiding-and-identifying-false-sharing-among-threads.html)
- [A futex overview and update — Ulrich Drepper (Red Hat)](https://man7.org/linux/man-pages/man7/futex.7.html)
- [The Native POSIX Thread Library for Linux — Ulrich Drepper, Ingo Molnar](https://www.akkadia.org/drepper/nptl-design.pdf)
- [CFS Scheduler Design — kernel.org](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
