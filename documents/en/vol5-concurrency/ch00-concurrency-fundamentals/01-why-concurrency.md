---
title: Why We Need Concurrency
description: Understand the difference between concurrency and parallelism, master
  Amdahl's Law and Gustafson's Law, and build the engineering judgment to know when
  to introduce concurrency.
chapter: 0
order: 1
tags:
- host
- cpp-modern
- beginner
- 基础
- 入门
difficulty: beginner
platform: host
reading_time_minutes: 12
cpp_standard:
- 11
- 17
- 20
related:
- 并发基本问题
- std::thread 基础
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/01-why-concurrency.md
  source_hash: 3d5a79225b0f2761b5ea7403f304f71da6e9ac2f90b4967aaf24e430486e4a79
  translated_at: '2026-05-20T04:34:29.186546+00:00'
  engine: anthropic
  token_count: 1349
---
# Why We Need Concurrency

Honestly, the author sometimes really fears "concurrency." Its introduction forces us to carefully think about and weigh even the simplest read and write operations.

Unlike RAII (Resource Acquisition Is Initialization) or move semantics, concurrency doesn't have a clear conceptual boundary—it is an entirely different way of thinking. You might have written single-threaded code for years where everything is so deterministic and controllable: the order of function calls is the order of execution, and the value you read from a variable is exactly the one you just wrote. Then, one day, you find that a single task is too much to handle, or a network service needs to respond to hundreds of connections simultaneously. You are forced to introduce multithreading—and suddenly, everything becomes unpredictable.

## Concurrency vs. Parallelism: Not the Same Thing

These two terms are often used interchangeably in casual conversation, but in computer science, they have distinct meanings. Simply put, **concurrency is about "structure," while parallelism is about "execution."**

Concurrency means your program is designed to handle multiple tasks at the same time—these tasks might alternate, or they might genuinely execute simultaneously. Concurrency is a way of organizing a program: you break a complex problem down into independent subtasks that can progress alternately, and then use some mechanism (threads, coroutines, event loops) to manage their execution order. The key point is that concurrency does not require multiple CPU cores; you can write a perfectly concurrent program on a single-core machine. The operating system uses time-slicing to let multiple threads take turns using the CPU, so from a macro perspective, they appear to be running "at the same time."

Parallelism, on the other hand, means multiple operations are **truly executing at the exact same physical moment**. This requires hardware support—multi-core CPUs, multiprocessors, or GPUs. Parallelism is an execution strategy: you have multiple computational resources, assign different tasks to them, and let them each do their work in the same clock cycle.

Rob Pike has a classic quote: "Concurrency is about dealing with a lot of things at once. Parallelism is about doing a lot of things at once." To translate, concurrency is about **managing** many things, while parallelism is about **doing** many things. A concurrent program can run perfectly well on a single core (it just won't speed up), whereas a parallel program requires multiple hardware execution units to be of any value (otherwise, there are no CPU cores to handle the work—Linux users who are curious about their core count can simply run `nproc`).

Why does this distinction matter? Because in C++, we use `std::thread`, `std::async`, and coroutines to express concurrency. Whether these concurrent tasks ultimately time-share a single core or actually run on different cores depends on the operating system's scheduler and hardware capabilities. Our responsibility as programmers is to guarantee the correctness of the concurrent program (regardless of how many cores it runs on). Performance improvement is merely an optimization layer built on top of correctness.

## Two Laws: Amdahl and Gustafson

> Those who have studied computer organization and architecture will find these two laws familiar.

Now that we understand the difference between concurrency and parallelism, the next natural question is: if we introduce parallelism, exactly how much faster can we get? Two classic laws help us build intuition for this.

### Amdahl's Law: The Speedup Ceiling Under Fixed Load

The core idea of Amdahl's Law is that a program's speedup is limited by its serial portion. Suppose a program has a total workload of 1, of which a fraction $f$ can be parallelized, leaving $(1 - f)$ that must execute serially. If we use $N$ processors to parallelize that portion, the theoretical speedup is:

$$S(N) = \frac{1}{(1 - f) + \frac{f}{N}}$$

The intuition is straightforward: no matter how many cores you use, that $(1 - f)$ serial portion is always there and will never be accelerated. When $N \to \infty$, the speedup approaches $\frac{1}{1-f}$—this is the theoretical limit.

For example, if 5% of your program is serial ($f = 0.95$), then no matter how many cores you throw at it, the speedup will never exceed $\frac{1}{0.05} = 20$x. If 25% is serial, the ceiling drops to 4x.

This law seems pessimistic—it tells us that the serial proportion is the performance ceiling. But precisely because of this, it is incredibly valuable in engineering: before you spend a massive amount of time parallelizing a program, use Amdahl's Law to estimate the potential gains. If the serial proportion is too high, the effort of parallelization might simply not be worth it. (As the author often says, engineering is divided into code-level and non-code-level concerns, and non-code-level factors often play a role that cannot be ignored.)

### Gustafson's Law: The Speedup Perspective Under Scaled Workloads

Amdahl's Law assumes the problem size remains fixed—we use more cores to solve the exact same problem. But in reality, when we have more computational resources, we often choose to solve larger problems. Gustafson's Law looks at this from a different angle.

Suppose a program takes $T_1$ to run on a single processor, with the serial portion taking $\alpha$ and the parallel portion taking $(1 - \alpha)$. When we run it on $N$ processors, the parallel portion's execution time shrinks to $\frac{1 - \alpha}{N}$, while the serial portion remains unchanged. However, Gustafson pointed out that in practice, we don't use $N$ cores to solve the same-sized problem—we **scale up the problem size**, letting the parallel portion's workload grow linearly with $N$ while keeping the serial portion constant. In this case, the speedup becomes:

$$S(N) = \alpha + (1 - \alpha) \cdot N$$

This formula is much more optimistic: if the serial portion $\alpha$ is very small, the speedup grows almost linearly with $N$. For instance, consider a video rendering program: rendering a one-minute video on one core takes 10 minutes. With 16 cores, you could choose to render a one-minute video faster (the Amdahl perspective), or you could choose to render a 16-minute video in roughly the same amount of time (the Gustafson perspective).

The two laws are not contradictory; they simply look at the same problem from different angles. Amdahl asks "how much faster can you go with a fixed workload?", while Gustafson asks "how much more work can you do in the same amount of time?" In real-world engineering, you will encounter both scenarios. The key is to be clear about your goal and which law you should use to evaluate your problem.

## The Throughput vs. Latency Trade-off

**Throughput and latency are often mutually exclusive.**

Throughput refers to the total number of tasks completed per unit of time, while latency refers to the time it takes for a single task to go from submission to completion. In concurrent design, optimizing for these two metrics frequently conflicts.

A classic example is batching. Suppose you have a task queue, and processing one task takes 1ms of CPU time. If you process a task the moment it arrives, the latency for each task is 1ms, but the overhead from thread switching and lock contention keeps the overall throughput low. If you accumulate tasks into a batch of 100 and process them together, you can apply optimizations within the batch (such as merging I/O operations), which dramatically increases total throughput. However, the latency for tasks at the back of the queue jumps from 1ms to nearly 100ms.

Another classic example is load-balancing strategies. Shortest-queue-first (assigning new tasks to the worker with the shortest current queue) minimizes average latency, but its scheduling overhead is higher than simple round-robin. Round-robin usually yields better throughput, but individual tasks might be assigned to an already busy worker, causing tail latency to spike.

There is no "correct answer" to this trade-off; it depends on your business requirements. Real-time trading systems prioritize lower latency, batch data pipelines prioritize higher throughput, and most web services need to maximize throughput within a reasonable latency bound. Before designing a concurrent architecture, figure out which metric your system cares about more.

## Task Granularity: Too Fine or Too Coarse Won't Work

Another judgment we need to develop is **task granularity**—how large a unit of work you break things into before handing it off to concurrent processing.

Too fine a granularity is problematic. Every time you create or schedule a concurrent task, there is overhead: thread creation and destruction, context switch, lock acquisition and release, and cache invalidation. If the computational workload of the task itself is smaller than this overhead, introducing concurrency will actually slow the program down—you spend more CPU time on management than on actual computation. As an extreme example, if you spawn a separate thread to perform addition for every single element in an array, the thread creation and scheduling overhead could be thousands of times greater than the actual computation.

Too coarse a granularity is also problematic. If you pack all the work into one giant task and hand it to a single thread, it is no different from single-threading. The concurrency level doesn't go up, and the computational resources of a multi-core CPU are wasted.

Therefore, choosing the task granularity requires finding a balance between "concurrency overhead" and "concurrency gains." Generally speaking, the computational workload of a concurrent task should be at least 10 times its scheduling overhead, otherwise it isn't worth it. The exact threshold depends on your hardware and runtime environment, but this order-of-magnitude judgment is universal.

In real-world engineering, task granularity is often determined through experimentation. You can start with a larger granularity and gradually refine it, measuring the total execution time and throughput at each step to find the performance inflection point. This benchmark-driven tuning approach is far more reliable than guessing the granularity by feel.

## When Not to Use Concurrency

At this point, we have talked a lot about why we need concurrency, but it is equally important to know when **not** to use it.

We know that the fundamental basis for concurrency/parallelism lies in whether the CPU has multiple cores and whether its clock speed meets expectations. The core factor is the CPU, right! If your program is a CPU-bound single task with no I/O waits (such as a pure numerical computation program), introducing multithreading might not help and could even slow things down—unless your algorithm is inherently parallelizable (in short, it can be broken down into multiple modules with no sequential dependencies). If your program is already fast enough—where the processing latency is well below the threshold required by the business—then the complexity cost of introducing concurrency is not worth it. If your program has strict determinism requirements (such as certain control systems), the unpredictability introduced by multithreading might be unacceptable (such as in some embedded scenarios).

There is also an easily overlooked scenario: when what you need is not parallel computation but asynchronous I/O, multithreading is not necessarily the best choice. A network service that needs to handle thousands of connections simultaneously will quickly find that the number of threads becomes a bottleneck if you spawn one thread per connection. This scenario is much better suited for event-driven or coroutine-based approaches, where a small number of threads manage a large number of connections through I/O multiplexing. We will discuss this in detail in the asynchronous I/O chapter later in Volume Five.

Finally, and most fundamentally: the complexity introduced by concurrency is real, not imagined. Data races, dead lock, spurious wakeups on condition variables, and object lifetime issues—these bugs are characterized by being difficult to reproduce, difficult to debug, and difficult to test. If single-threading can solve the problem, do not introduce concurrency just to show off. The only legitimate reason for concurrency is that single-threading genuinely isn't enough anymore.

## Where We Are

In this chapter, we established a basic cognitive framework for concurrency: concurrency and parallelism are not the same thing; Amdahl's Law and Gustafson's Law help us understand the upper and lower bounds of speedup; the throughput vs. latency trade-off guides architectural choices; task granularity requires finding a balance between overhead and gains; and some scenarios simply do not need concurrency.

But knowing "why" is only the first step. In the next chapter, we will face a more practical question: what actually goes wrong when you write concurrent code? We will break down data races, race conditions, dead lock, livelock, starvation, and priority inversion one by one—these are the most common sources of bugs in concurrent programming, and they are the problems that the rest of this volume aims to solve. Correctness first, performance second. Remember this principle.

## Reference Resources

- [Multi-threaded executions and data races (cppreference)](https://en.cppreference.com/cpp/language/multithread)
- [Amdahl's Law — Wikipedia](https://en.wikipedia.org/wiki/Amdahl%27s_law)
- [Gustafson's Law — Wikipedia](https://en.wikipedia.org/wiki/Gustafson%27s_law)
- [Concurrency Is Not Parallelism — Rob Pike (Heroku Waza 2012, YouTube)](https://www.youtube.com/watch?v=oV9rvDllKEg) — The classic distinction proposing that "concurrency is about dealing with a lot of things, parallelism is about doing a lot of things," emphasizing that concurrency is a design tool (structuring) while parallelism is an execution attribute (execution)
- [Concurrency Is Not Parallelism — Rob Pike (Slides)](https://go.dev/talks/2012/waza.slide)
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
