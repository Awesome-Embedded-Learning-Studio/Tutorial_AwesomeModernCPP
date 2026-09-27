---
title: "Why We Need Concurrency"
description: "Understand the difference between concurrency and parallelism, master Amdahl's Law and Gustafson's Law, and build the engineering judgment for when to introduce concurrency"
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
cpp_standard: [11, 17, 20]
related:
  - "Fundamental Concurrency Problems"
  - "std::thread Basics"
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/01-why-concurrency.md
  source_hash: 3d5a79225b0f2761b5ea7403f304f71da6e9ac2f90b4967aaf24e430486e4a79
  translated_at: '2026-09-26T06:12:59+00:00'
  engine: anthropic
  token_count: 8400
---

# Why We Need Concurrency

Honestly, sometimes we're genuinely a little afraid of concurrency. Once it enters the picture, we have to think hard and weigh even reads and writes themselves.

Unlike RAII or move semantics, concurrency has no crisp conceptual boundary—it is an entire way of thinking. You may have written single-threaded code for years, and everything felt so deterministic, so controllable: the order of function calls is the order of execution, and the value you read from a variable is exactly the one you just wrote. Then one day a task outgrows what one thread can handle, or a network service has to answer hundreds of connections at the same time, and you have no choice but to bring in multiple threads—and everything starts to become unpredictable.

## Concurrency and Parallelism: Not the Same Thing

These two words get mixed up constantly in casual conversation, but in computer science they have a precise, distinct difference. Put simply, **concurrency is about "structure," while parallelism is about "execution."**

Concurrency means your program is designed to handle multiple tasks at once—those tasks may take turns executing, or they may truly run simultaneously. Concurrency is a way of organizing a program: you break a complex problem into independent subtasks that can each advance in turn, then use some mechanism (threads, coroutines, an event loop) to manage the order in which they execute. The key point: concurrency does not require multiple CPU cores. You can absolutely write a concurrent program on a single-core machine—the operating system rotates time slices so multiple threads take turns on the CPU, and from a macro perspective they appear to run "at the same time."

Parallelism, on the other hand, means multiple operations physically execute **at the very same instant**. This takes hardware support—multi-core CPUs, multiple processors, GPUs. Parallelism is a style of execution: you have multiple computing resources, hand different tasks to each of them, and let them all do their work within the same clock cycle.

Rob Pike has a classic line: "Concurrency is about dealing with a lot of things at once. Parallelism is about doing a lot of things at once." In other words, concurrency is about **juggling** many things, while parallelism is about **doing** many things simultaneously. A concurrent program runs perfectly well on a single core (it just gets no speedup), whereas a parallel program must have multiple hardware execution units to be worth anything (otherwise there are no CPU cores to hand the work to—and if you work with Linux and are curious how many cores you have, it's simple: just run `nproc`).

Why does this distinction matter? Because in C++, we use mechanisms like `std::thread`, `std::async`, and coroutines to express concurrency—whether those concurrent tasks end up time-sharing a single core or genuinely landing on different cores depends on the operating system's scheduling and the hardware's capability. Our responsibility as programmers is to keep the concurrent program correct (no matter how many cores it runs on); the performance gain is only a layer of optimization on top of correctness.

## Two Laws: Amdahl and Gustafson

> These two laws will feel familiar to anyone who has studied computer organization and architecture.

Now that we know the difference between concurrency and parallelism, the next natural question is: if I introduce parallelism, how much faster do things actually get? Two classic laws help us build intuition here.

### Amdahl's Law: The Speedup Ceiling Under a Fixed Workload

The core idea of Amdahl's Law: a program's speedup is capped by its serial portion. Suppose a program's total workload is 1, of which a fraction $f$ can be parallelized, leaving $(1 - f)$ that can only execute serially. If we use $N$ processors to parallelize that portion, the theoretical speedup is:

$$S(N) = \frac{1}{(1 - f) + \frac{f}{N}}$$

The intuition is plain: no matter how many cores you use, that $(1 - f)$ serial portion is always there, never accelerated. As $N \to \infty$, the speedup approaches $\frac{1}{1-f}$—that is the theoretical ceiling.

For example, if 5% of your program is serial ($f = 0.95$), then no matter how many cores you pile on, the speedup will never exceed $\frac{1}{0.05} = 20$ times. If 25% is serial, the ceiling drops to 4 times.

The law looks pessimistic—it is telling us that the serial fraction is the performance ceiling. But precisely because of that, it is enormously valuable in engineering: before you sink a large amount of time into parallelizing a program, first use Amdahl's Law to estimate the possible gain. If the serial fraction is too high, the investment in parallelization may simply not be worth it. (As we often say, engineering splits into what is inside the code and what is outside it, and what lies outside the code often plays a role too significant to ignore.)

### Gustafson's Law: The Speedup Perspective Under Scaled Workloads

Amdahl's Law assumes the problem size stays fixed—we use more cores to solve the same-sized problem. In reality, though, once we have more computing resources, we usually choose to take on bigger problems. Gustafson's Law looks at the question from that other angle.

Suppose a program's running time on a single processor is $T_1$, with the serial portion taking $\alpha$ and the parallel portion $(1 - \alpha)$. When we put it on $N$ processors, the parallel portion's execution time shrinks to $\frac{1 - \alpha}{N}$, while the serial portion stays unchanged. But Gustafson pointed out that in practice, we do not use $N$ cores to solve the same-sized problem—we **scale the problem up**, letting the parallel portion's workload grow linearly with $N$ while the serial portion holds constant. The speedup then becomes:

$$S(N) = \alpha + (1 - \alpha) \cdot N$$

This formula is far more optimistic: if the serial portion $\alpha$ is small, the speedup grows almost linearly with $N$. For instance, a video rendering program needs 10 minutes to render a 1-minute video on 1 core; with 16 cores, you can choose to render a 1-minute video (the Amdahl view), or you can choose to render a 16-minute video in about the same amount of time (the Gustafson view).

The two laws are not contradictory; they simply view the same problem from different angles. Amdahl says "how much faster can you go under a fixed workload," Gustafson says "how much more work can you get done in the same amount of time." In real engineering you will run into both scenarios—the key is being clear about what your goal is and which of the two you intend to evaluate your problem with.

## The Trade-off Between Throughput and Latency

**Throughput and latency often cannot be had at the same time**.

Throughput is the total number of tasks completed per unit of time; latency is the time a single task takes from submission to completion. In concurrent design, the directions in which the two are optimized frequently conflict.

Batching is a textbook example. Suppose you have a task queue where processing each task takes 1ms of CPU time. If you process each task the moment it arrives, every task sees 1ms of latency, but the overhead of thread switching and lock contention keeps overall throughput low. If you instead accumulate tasks into batches of 100 and process each batch together, you can apply optimizations inside the batch (merging I/O operations, for example), and total throughput rises sharply—but the latency of tasks sitting at the back of the queue goes from 1ms to nearly 100ms.

Another classic example is load-balancing strategy. Shortest-queue-first (assigning each new task to the worker whose queue is currently shortest) minimizes average latency, but its scheduling overhead is higher than that of simple round-robin. Round-robin usually delivers better throughput, but an unlucky task may land on an already-busy worker, sending tail latency soaring.

There is no "correct answer" to this trade-off; it depends on your business requirements. Real-time trading systems prioritize lowering latency, batch data pipelines prioritize raising throughput, and most web services need to maximize throughput within a reasonable latency bound. Before you start designing a concurrent architecture, get clear on which metric your system cares about more.

## Task Granularity: Neither Too Fine Nor Too Coarse

Another judgment we need to build is **task granularity**—how large the units of work are that you break things into before handing them to concurrent processing.

Too fine is a problem. Every creation or scheduling of a concurrent task carries overhead: thread creation and destruction, context switches, lock acquisition and release, cache invalidation. If a task's own computation is smaller than this overhead, introducing concurrency actually slows the program down—you spend more CPU time on management than on computation. As an extreme example, if you spawn one thread per array element just to do an addition, the thread creation and scheduling overhead can be a thousand times the actual computation.

Too coarse is also a problem. If you pack all the work into one big task and hand it to a single thread, that is hardly different from single-threading: the concurrency level never rises, and the multi-core CPU's computing resources are wasted.

So choosing task granularity means finding the balance point between "concurrency overhead" and "concurrency gains." As a rule of thumb, a concurrent task's computation should be at least 10 times its scheduling overhead—anything less is not worth it. The exact threshold depends on your hardware and runtime environment, but this order-of-magnitude judgment is universal.

In real engineering, task granularity is usually settled through experiment. You can start with a coarser granularity, refine it step by step, and measure total execution time and throughput at each step to find the inflection point with the best performance. This benchmark-driven way of tuning is far more reliable than guessing a granularity by feel.

## When Not to Use Concurrency

At this point we have talked a lot about why to use concurrency, but it is equally important to know when **not** to use it.

As we know, whether concurrency/parallelism works at all comes down to whether the CPU has multiple cores and whether its clock performance meets expectations. It all hinges on the CPU, right! If your program is a single CPU-bound task with no I/O waits (a pure numerical computation program, for example), introducing multithreading may not help and may even slow it down—unless your algorithm is itself parallelizable (in short, it can be split into multiple modules with no before-after dependencies between them). If your program is already fast enough—processing latency well below the threshold your business requires—then the complexity cost of introducing concurrency is not worth paying. If your program has strict determinism requirements (certain control systems, say), the unpredictability that multithreading introduces may be unacceptable (some embedded scenarios, for instance).

There is also an easily overlooked scenario: when what you need is not parallel computation but asynchronous I/O, more threads is not necessarily the best choice. A network service that must handle thousands of connections at once will find thread count becoming the bottleneck very quickly if you open one thread per connection. Event-driven or coroutine-based approaches suit this scenario better, with a small number of threads managing a large number of connections through I/O multiplexing—we will discuss this in detail in the asynchronous I/O chapter later in Volume Five.

Finally, the most fundamental point of all: the complexity that concurrency introduces is real, not imagined. Data races, deadlocks, spurious wakeups on condition variables, object lifetime problems—bugs like these are hard to reproduce, hard to debug, and hard to test. If a single thread can solve the problem, do not introduce concurrency just to show off. The only legitimate justification for concurrency is that single-threading has genuinely run out of headroom.

## Where We Are

In this article we built the basic cognitive framework for concurrency: concurrency and parallelism are not the same thing; Amdahl's Law and Gustafson's Law help us understand the upper and lower bounds of speedup; the trade-off between throughput and latency guides architectural choices; task granularity needs a balance point between overhead and gains; and some scenarios simply do not need concurrency at all.

But knowing "why" is only the first step. In the next article we face a more practical question: once you have actually written concurrent code, what exactly goes wrong? We will take apart data races, race conditions, deadlocks, livelock, starvation, and priority inversion one by one—these are the most common sources of bugs in concurrent programming, and the problems the rest of this volume exists to solve. Correctness first, performance second—remember this principle.

## Reference Resources

- [Multi-threaded executions and data races (cppreference)](https://en.cppreference.com/cpp/language/multithread)
- [Amdahl's Law — Wikipedia](https://en.wikipedia.org/wiki/Amdahl%27s_law)
- [Gustafson's Law — Wikipedia](https://en.wikipedia.org/wiki/Gustafson%27s_law)
- [Concurrency Is Not Parallelism — Rob Pike (Heroku Waza 2012, YouTube)](https://www.youtube.com/watch?v=oV9rvDllKEg) — The classic distinction that concurrency is about dealing with many things while parallelism is about doing many things at once, emphasizing that concurrency is a design tool (structuring) while parallelism is a property of execution
- [Concurrency Is Not Parallelism — Rob Pike (Slides)](https://go.dev/talks/2012/waza.slide)
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
