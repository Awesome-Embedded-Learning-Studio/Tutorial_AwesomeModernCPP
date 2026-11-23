---
title: "When Is It Worth Bringing In Concurrency"
description: "Distinguish what concurrency, parallelism, and asynchronous each govern, see where the two kinds of gain — throughput and latency — come from, list the situations where concurrency should not be introduced, and give the two speedup estimation methods of Amdahl and Gustafson."
chapter: 5
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 23]
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/08-when-concurrency.md
  source_hash: 95cb12b8a0f686ccf6be5800c092e99a452e656955f557581ca117e63d8f6089
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 13213
---
# When Is It Worth Bringing In Concurrency

We have already written our first thread program, and we know how to wind it down. Now you meet a program that feels slow, and a thought probably pops into your head: open a few more threads to work together — how much faster would that be? Faster, yes, but opening threads itself carries a cost.

I say programmers also have to be qualified investment players. Really, er — despite the whiff of AI-favored jargon, my impoverished vocabulary genuinely cannot find any metaphor that isn't lame. The point is: this account must be reckoned clearly.

## What Concurrency, Parallelism, and Asynchronous Each Govern

In casual chat the three words blur together; in design they must come apart. Concurrency answers "how is the program organized," parallelism answers "are the tasks truly running at the same time," and asynchronous answers "how is waiting handled." Ask the three questions separately, and the tools choose themselves.

### Concurrency: How the Program Is Organized

**Concurrency refers to structure**. We divide a program into multiple tasks that can each advance on their own, then manage the order of their advance with some mechanism; a program organized this way is a concurrent program. Put another way, as long as things look like they execute together — whether the tasks may be interleaved (wow, what powerful switching speed: within 1 ms I take turns doing A, B, and C, each for 10 ms, and to the eye, whoa, amazing, you are advancing A, B, and C at the same time, even though you are actually working serially), or whether they truly execute simultaneously (one CPU handles A, one CPU handles B, another CPU handles C) — the structure itself demands no multiple CPU cores. On a single-core machine, the operating system runs several threads in turn by time-slicing (each thread takes a small stretch of CPU time in rotation), and from outside, every task appears to be advancing. That is concurrency, even if at any given instant the CPU is running only one of them.

The C++ mechanisms for expressing concurrent structure you will all learn later: `std::thread`, `std::async` (both standard-library facilities since C++11), and coroutines — each merely grounds "the program consists of multiple advanceable tasks" in language facilities. Whether those tasks can truly run simultaneously is the next subsection's business.

### Parallelism: Execution at the Same Instant

Parallelism refers to execution. Multiple operations each running at the same physical instant — only that counts as parallelism, and it needs hardware backing it up: multi-core CPUs, multiple processors, GPUs all qualify. To find out how many flows of execution your machine can run at once, two ready-made checks exist: the `nproc` command line on Linux and Unix-like systems, or the `std::thread` static member function `hardware_concurrency()` since C++11. [cppreference][cppref-hw] describes the latter as "returning the number of concurrent threads supported by the implementation" — the value is only a hint, and it returns 0 when it cannot be worked out.

<OnlineCompilerDemo
  title="Querying the machine's concurrency capacity"
  source-path="code/examples/vol5/chapter01/01_hardware_concurrency.cpp"
  description="hardware_concurrency returns the number of concurrent threads the implementation supports, 0 when it cannot be determined; the result matches nproc."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 查询机器的并发能力:hardware_concurrency 返回实现支持的并发线程数
#include <print>
#include <thread>

int main() {
    std::println("{}", std::thread::hardware_concurrency());
}
```

</OnlineCompilerDemo>

On our machine it printed 16, matching `nproc`.

Rob Pike, in his talk Concurrency Is Not Parallelism, gave one sentence; I place the original here, with the links collected in the reference resources:

> Concurrency is about dealing with lots of things at once. Parallelism is about doing lots of things at once.
>
> — Rob Pike, Heroku Waza 2012

Now set that sentence against our own code. A concurrent program we write runs correctly even if it only ever gets one core — it simply earns no speedup. A parallel program must have multiple execution units; otherwise "doing at the same time" has nothing to stand on.

Landed in C++, this distinction has one practical consequence. What we write with mechanisms like `std::thread` is concurrent structure; whether the tasks end up time-sharing one core or truly holding a core each is decided by the operating system's scheduling and the hardware's capability — outside the language's control. So a concurrent program's correctness must never be staked on assumptions about "how many cores it runs on": whatever cores it actually gets, your program must be correct.

### Asynchronous: How Waiting Is Organized

Asynchronous governs how waiting is organized. After launching a slow operation, we do not wait for it to finish; we take control back and do something else. When the operation completes, we collect the result via a callback, a future, or a coroutine. A future represents an object whose result only becomes ready later; we will deal with it properly when we cover task organization. Asynchrony has no direct relationship with core count. You can start a disk read in one thread, come back to handle the next request, and resume once the data arrives — the whole affair taking place in a single thread.

C++ has a ready-made example that pulls asynchrony and parallelism apart especially well: `std::async` with the `std::launch::deferred` policy (since C++11). [cppreference][cppref-launch] describes the deferred policy as: the task executes on the calling thread, at the moment you first ask it for the result — lazy evaluation. You launched an "asynchronous" call, yet it runs on no other thread and is in no way parallel; it merely postpones "when it gets computed." The other policy in the same standard, `std::launch::async`, explicitly says the task runs on another thread.

We compiled and ran a minimal experiment (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, WSL2, tested 2026-11-14):

<OnlineCompilerDemo
  title="deferred vs. async: who runs on which thread"
  source-path="code/examples/vol5/chapter01/02_async_deferred_strategy.cpp"
  description="The same std::async with two launch policies: deferred postpones execution until get() and runs on the calling thread; async truly starts another thread."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// deferred 与 async 两种启动策略:谁在哪个线程上跑、耗时落在哪一步
#include <chrono>
#include <future>
#include <print>
#include <sstream>
#include <thread>

using namespace std::chrono;

std::string id_str() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

int main() {
    const std::string main_id = id_str();

    auto t0 = steady_clock::now();
    auto fut = std::async(std::launch::deferred, [] {
        std::this_thread::sleep_for(milliseconds(300));
        return id_str();
    });
    auto t1 = steady_clock::now();
    const std::string who = fut.get();
    auto t2 = steady_clock::now();

    std::println("async() call spent: {} ms", duration_cast<milliseconds>(t1 - t0).count());
    std::println("get()   spent    : {} ms", duration_cast<milliseconds>(t2 - t1).count());
    std::println("deferred task ran on {}",
                 who == main_id ? "the calling thread" : "another thread");

    auto af = std::async(std::launch::async, [] { return id_str(); });
    std::println("std::launch::async task ran on {}",
                 af.get() == main_id ? "the calling thread" : "another thread");
}
```

</OnlineCompilerDemo>

The four lines we got follow (thread IDs vary by machine):

```text
async() call spent: 0 ms
get()   spent    : 300 ms
deferred task ran on the calling thread
std::launch::async task ran on another thread
```

These output lines deserve a line-by-line read. The `async()` call itself spent 0 milliseconds, showing the deferred policy started no flow of execution at launch. The task slept 300 milliseconds, and those 300 milliseconds appear in full in `get()`'s time — and the task ran on the calling thread, matching the main thread's ID. The `std::launch::async` line shows the task indeed ran on a separate flow of execution. The full story of `std::async` (the default policy, the fallback when resources run short, the future's lifetime) I will save for the task-organization coverage; here it is borrowed only to pry the three words apart.

Seen together, the three words come out clean. Concurrency governs structure — how tasks are divided and organized. Parallelism goes one layer deeper, asking whether those tasks truly run at the same instant. Asynchrony governs waiting, and its landing point is one piece of advice: don't wait dumbly. A multithreaded program on a multicore machine has all three properties present at once. Some services keep thousands of connections alive with an event loop (a main loop inside the program watching a pile of events, handling whichever reply arrives) — possibly one thread only, asynchronous throughout, no parallelism at all. Hand matrix multiplication to sixteen cores and that is pure parallelism, with nothing necessarily asynchronous.

## Where the Gains Come From: Throughput and Latency

Throughput is the total number of tasks completed per unit of time; latency is the time a single task spends from submission to completion. What we are after in bringing in concurrency is these two — mostly throughput.

### Source One: Multiple Cores Working at Once

The first channel of gain is the most intuitive. Work that divides into mutually independent chunks can go to multiple cores computing simultaneously — more work done in the same second. We ran a summation experiment on our machine: accumulating 400 million integers (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 16 logical cores, WSL2, tested 2026-11-14).

<OnlineCompilerDemo
  title="Summing 400 million integers: serial vs. two threads"
  source-path="code/examples/vol5/chapter01/03_sum_two_threads.cpp"
  description="Divide divisible pure computation across two cores and read the speedup and the cost of task division from the numbers. The online sandbox limits core count and CPU quota; take local measurements as authoritative for speedup."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 4 亿整数求和:串行对双线程,观察多核加速比与划分开销
#include <chrono>
#include <cstdint>
#include <print>
#include <thread>

using namespace std::chrono;

long long sum_range(long long from, long long to) {
    long long s = 0;
    for (long long i = from; i < to; ++i)
        s += i % 1000;
    return s;
}

int main() {
    const long long n = 400000000LL;

    auto t0 = steady_clock::now();
    long long s1 = sum_range(0, n);
    auto t1 = steady_clock::now();
    std::println("serial    : {} ms, sum {}", duration_cast<milliseconds>(t1 - t0).count(), s1);

    long long sa = 0, sb = 0;
    t0 = steady_clock::now();
    std::thread ta([&] { sa = sum_range(0, n / 2); });
    std::thread tb([&] { sb = sum_range(n / 2, n); });
    ta.join(); // join 等待该线程结束
    tb.join();
    t1 = steady_clock::now();
    std::println("two threads: {} ms, sum {}", duration_cast<milliseconds>(t1 - t0).count(),
                 sa + sb);
}
```

</OnlineCompilerDemo>

```text
serial    : 194 ms, sum 199800000000
two threads: 109 ms, sum 199800000000
```

Two signals in the output deserve your attention. Serial takes about 190 milliseconds, two threads about 110 — roughly 1.7x faster, so two cores truly worked at the same instant. At the same time, 1.7x falls short of the ideal 2x: task division, thread startup, and scheduling overhead all spend money in there. We repeated a few rounds; the speedup held steady between 1.7 and 1.8. The numbers on your machine will likely differ from ours, but divisible pure computation given to multiple cores always gains in direction.

I paused while writing this: two threads each writing their own `sa` and `sb`, read only after the main thread finishes `join()` (since C++11) — is that really safe? It is safe. cppreference states on the [`std::thread::join`][cppref-join] page that a synchronization relationship exists between the thread's completion and join's successful return, so the main thread is guaranteed to see the values both threads wrote. The machinery behind that guarantee belongs to atomic operations and the memory model; for now, knowing that you may read the result only after join completes is already enough.

### Source Two: Putting the Waiting Time to Use

The second channel doesn't depend on multiple cores. Real tasks almost always mix computation with waiting: waiting on the disk, on the network, on a lock. While a task waits on I/O, its thread sits idle. Let other tasks run in that gap and throughput rises — even if all threads share one core. Most server programs I have seen open multiple threads precisely for this second channel.

### The Two Metrics Often Fight

Throughput and latency often optimize in conflicting directions. Lay out a set of hypothetical numbers: suppose each task needs 1 millisecond of pure computation. Handle one as it comes — each task's latency is 1 millisecond, but scheduling and switching overhead discounts the throughput. Switch to batching 100 at a time, with I/O merged inside the batch: throughput climbs markedly, while a task at the tail of a batch waits close to 100 milliseconds. These numbers are estimates, not measurements, but the trade-off they point at appears in any batching design.

The same holds for assignment strategy. The shortest-queue strategy (handing each new task to the worker whose queue is currently shortest) pulls average latency down, but every assignment compares the queues once, and the comparison itself costs. Round-robin assignment is cheap and usually better for throughput, but under uneven load, a few tasks' wait times spike. Latency sitting at the tail of the distribution like this has a name: tail latency. Before designing, think clearly about which metric your system cares about more: real-time trading cares most about latency, batch pipelines care most about throughput, and most web services want throughput pushed up within an acceptable latency range.

Gains covered; the flip side must get a look too. More than one situation makes setting concurrency aside the better deal — check against them before starting; it saves rework afterwards.

## When Not to Use Concurrency

### The Task Cannot Be Divided

Your task is pure computation. If the algorithm itself cannot be cut into mutually independent chunks, extra threads buy no parallelism: the surplus threads can only take turns borrowing the CPU on the side, switching costs time on top, and the program may actually slow down. To judge divisibility, look straight at the dependency structure: wherever a later step must wait for an earlier step's result before computing, you can dig out no parallelism. When we meet such programs, the starting point is rewriting the serial dependencies into a decomposable structure; adding threads comes only after that.

### The Tasks Are Too Fine

Even divisible work loses when the chunks are cut too fine. Creating, scheduling, and waking threads all carry fixed costs. We ran a deliberately exaggerated experiment: 2000 tiny tasks, each running one hundred loop iterations, each iteration doing one addition paired with one modulo (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, tested 2026-11-14).

<OnlineCompilerDemo
  title="The cost of too-fine tasks: one thread per task"
  source-path="code/examples/vol5/chapter01/04_tiny_task_thread_overhead.cpp"
  description="A serial loop vs. one thread per task: thread overhead is hundreds to thousands of times the computation. The online sandbox caps concurrent threads, so the sample is narrowed to 8 tasks; the readings in the text come from a local 2000-task run."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 任务太细的代价:微任务串行循环对一任务一线程
// 在线沙箱限制并发线程数,这里收窄为 8 个任务演示开销量级;正文读数来自本地 2000 任务实测
#include <chrono>
#include <print>
#include <thread>
#include <vector>

using namespace std::chrono;

long long tiny_task(int base) {
    long long s = 0;
    for (int i = 0; i < 100; ++i)
        s += (base + i) % 64;
    return s;
}

int main() {
    constexpr int kTasks = 8;
    std::vector<long long> results(kTasks);

    // 串行:一个循环做完所有任务
    auto t0 = steady_clock::now();
    for (int k = 0; k < kTasks; ++k)
        results[k] = tiny_task(k);
    auto t1 = steady_clock::now();
    std::println("serial loop             : {} us", duration_cast<microseconds>(t1 - t0).count());

    // 每个任务单独开一个线程
    t0 = steady_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(kTasks);
    for (int k = 0; k < kTasks; ++k) {
        threads.emplace_back([&results, k] { results[k] = tiny_task(k); });
    }
    for (auto& t : threads)
        t.join();
    t1 = steady_clock::now();
    std::println("one thread per tiny task: {} us", duration_cast<microseconds>(t1 - t0).count());
}
```

</OnlineCompilerDemo>

```text
serial loop             : 26 us
one thread per tiny task: 144306 us
```

The serial loop finished all 2000 tasks in 26 microseconds; giving each task its own thread cost about 144 milliseconds. Spread 144 milliseconds over 2000 threads and each costs about 70 microseconds, while a single task's computation only fills a dozen-odd nanoseconds — overhead thousands of times the computation.

These numbers are a deliberately chosen extreme; real tasks will not be this small. But you can carry away one order-of-magnitude judgment: a single task's computation must clearly exceed its scheduling overhead before it is worth dispatching on its own.

The real threshold shifts with hardware and runtime; when unsure, return to measurement (yes — get a typical machine and test; in concurrency, any detailed benefit claim must be stated from measurement, not imagination!). Batching too-fine tasks into one thread beats forcing a thread per task. But the other end carries risk too: shove the whole pile into one thread and it is indistinguishable from single-threaded, with the extra cores idling for nothing. Task granularity — how large you cut the work chunks — is found between these two ends. Thread pools amortize the creation cost; they are the advanced form of this, and I plan to write them into the cookbook's optional section, adding a link in the reference resources once it is done.

### It Is Already Fast Enough

When a program's processing latency already sits far below the business threshold, concurrency buys nothing new — what it buys back is the full set of complexity. "Already fast enough" is the plainest of these cases, and the easiest to forget checking. Give yourself a numeric gate before starting: if the latency requirement is 100 milliseconds and current measurements say 5, no reason to introduce concurrency exists.

### The Program Demands Determinism

Some systems demand reproducible execution order — control programs and some embedded settings are typical examples. The scheduling nondeterminism that multithreading brings may be flatly unacceptable to them; this has nothing to do with performance — the requirement itself shuts concurrency out at the door. This volume stays on the host; when you later read this tutorial's embedded volumes, such constraints will appear often.

### What You Need Is Asynchronous I/O

A network service tending thousands of connections at once: give every connection its own thread and the thread count itself becomes the bottleneck, with memory and scheduling both stretched. What such scenes want is asynchronous I/O. The event-driven style manages many connections with a few threads paired with I/O multiplexing (one thread watching many connections at once, handling whichever reply arrives). Coroutines are the modern C++ form of this — able to pause mid-execution and resume later from the pause point — and the coroutine unit later in this volume covers them properly. Practice with event loops and asynchronous I/O also goes into the cookbook's optional section, with links added once written.

Having checked these cases, one more thing running through all of them remains to be told: the complexity itself. It picks no scene; it follows every category. A data race means multiple flows of execution reading and writing the same data at the same time with no synchronization — cppreference's language pages define it formally, link collected in the reference resources. A deadlock is several threads each waiting on a lock held in another's hand, none able to move. Condition variables carry the spurious-wakeup problem — waits get startled awake for no reason. Object-lifetime problems are on the list too. Their shared trait: hard to reproduce, hard to debug. In the concurrent code I have written, mistakes I made on the spot were all easy; what truly eats time is reproduction — the same program runs a hundred times, and only the failing run is willing to show itself. The later article on concurrency's basic problems starts from several broken programs, letting you see with your own eyes what a race looks like.

At this point I gather my stance, used for the rest of this volume: a problem single-threaded code can solve gets no concurrency for cleverness's sake. Only one legitimate reason exists for bringing in concurrency: single-threading is genuinely not enough — it cannot compute it all, or cannot afford the wait. All that remains is choosing the route and paying the price.

## Amdahl and Gustafson: How to Estimate Speedup (Extended)

If you are already mentally reckoning "how much faster with N more threads," two estimation formulas can trade that intuition for numbers; worth reading to the end.

### Amdahl's Law: The Speedup Ceiling Under Fixed Load

Write the program's total work as 1, with fraction $f$ parallelizable and the remaining $(1 - f)$ strictly serial. Run it on $N$ processors and the theoretical speedup is

$$S(N) = \frac{1}{(1 - f) + \frac{f}{N}}$$

Look at the serial part alone: it does not change with core count, and growing $N$ only shrinks $f/N$, so the speedup approaches the ceiling $\frac{1}{1-f}$. The word "ceiling" belongs to Amdahl alone, because it assumes the problem you are solving stays the same size. Gustafson estimates under a different problem setup, covered separately below. Plug in two sets of numbers to feel it: $f = 0.95$, i.e. 5% serial, ceilings at 20x, while 16 cores actually estimates to $1/(0.05 + 0.95/16) \approx 9.1$x. $f = 0.75$, i.e. 25% serial, ceiling 4x, 16 cores about 3.4x. Between 5% and 25% serial fractions, the ceilings differ by five times.

My suggested use: estimate once before investing. When the serial fraction is high, restructuring the algorithm to lower it pays far better than piling on cores. The formula carries two assumptions — fixed load and zero-cost parallelism — so what it gives is a ceiling, and measurements will only come in lower. And precisely because the assumptions are coarse, it suits answering one question before you start: is this rework worth the investment? You only need a rough number.

### Gustafson's Law: The View After Scaling Up

Set the two formulas' assumptions side by side. Amdahl assumes the problem to solve is unchanged: more cores, same computation. In reality the other practice is more common — more resources, so make the problem bigger. Gustafson's starting point is exactly to let the parallel part's workload grow linearly with $N$ while the serial part stays fixed. With the serial fraction at $\alpha$, the speedup is

$$S(N) = \alpha + (1 - \alpha) \cdot N$$

The gap between the two views on identical parameters deserves an eyeball-to-eyeball comparison. At 5% serial ($\alpha = 0.05$), 16 cores gives a Gustafson speedup of $0.05 + 0.95 \times 16 = 15.25$x, while the same configuration under Amdahl's view is 9.1x. Both formulas compute correctly under their own setups; the fork is in the problem setting: Gustafson allows rendering more frames, Amdahl renders only the original frames.

Take rendering as the example. A rendering program needs 10 minutes on one core to render 1 minute of video. Now you get 16 cores. Under Amdahl's accounting, you render the same 1-minute video and see how many minutes it shrinks to. Under Gustafson's accounting, you render 16 minutes of video, and the time still stays on the order of 10 minutes. Neither accounting is superior; they correspond to two problem classes: interactive tasks want results sooner — estimate with Amdahl. Batch processing wants more data digested in the same time — estimate with Gustafson.

With both formulas computed, return to the previous section's judgment: if single-threading already suffices, no matter how high the formulas' multiples, they have nothing to do with you. If it truly does not suffice, then the two numbers Amdahl and Gustafson produce are the first basis for deciding how much to invest.

## Next Steps

If you want to know exactly where a thread's overhead goes, read on to see how a thread on Linux gets built. [What a Thread Is on the Machine](./09-what-is-a-thread)

## Reference Resources

While writing this article I drew on Anthony Williams' C++ Concurrency in Action for chapter progression, and on Rob Pike's way of distinguishing the concepts. The examples and prose are my own. Claims about C++ behavior were compiled and tested or verified against cppreference as noted in the text.

- [Multi-threaded executions and data races (cppreference)][cppref-multithread] — the language-level rules on data races.
- [std::async (cppreference)][cppref-async] — launch-policy semantics and the fallback behavior when resources run short.
- [std::launch (cppreference)][cppref-launch] — the definitions of the async and deferred policies.
- [std::thread::hardware_concurrency (cppreference)][cppref-hw] — return-value semantics.
- [std::thread::join (cppreference)][cppref-join] — join's synchronization guarantees.
- [Amdahl's law — Wikipedia][wiki-amdahl]
- [Gustafson's law — Wikipedia][wiki-gustafson]
- [Concurrency Is Not Parallelism — Rob Pike (Heroku Waza 2012, video)][pike-talk] and [slides][pike-slides] — the classic distinction between concurrency and parallelism.
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm][boehm] — why data races are specified as undefined behavior in C++.

[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[cppref-async]: https://en.cppreference.com/w/cpp/thread/async
[cppref-launch]: https://en.cppreference.com/w/cpp/thread/launch
[cppref-hw]: https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[wiki-amdahl]: https://en.wikipedia.org/wiki/Amdahl%27s_law
[wiki-gustafson]: https://en.wikipedia.org/wiki/Gustafson%27s_law
[pike-talk]: https://www.youtube.com/watch?v=oV9rvDllKEg
[pike-slides]: https://go.dev/talks/2012/waza.slide
[boehm]: https://www.hboehm.info/c++mm/why_undef.html
