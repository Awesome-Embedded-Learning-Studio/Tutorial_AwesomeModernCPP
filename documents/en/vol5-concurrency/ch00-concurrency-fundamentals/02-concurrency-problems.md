---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 'Identify the most common bugs in concurrent programming: data race,
  race condition, dead lock, livelock, starvation, and priority inversion.'
difficulty: beginner
order: 2
platform: host
prerequisites:
- 为什么需要并发
reading_time_minutes: 16
related:
- mutex 与 RAII 守卫
- std::atomic 原子操作
tags:
- host
- cpp-modern
- beginner
- atomic
- mutex
title: Fundamental Concurrency Issues
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/02-concurrency-problems.md
  source_hash: d336eb3333a93d0c5756f6d12e6aaa776b0bd2dfa36ea281f9e3cf2d2cde736a
  token_count: 2616
  translated_at: '2026-05-20T04:31:55.772461+00:00'
---
# Fundamental Concurrency Problems

In the previous chapter, we discussed "why we need concurrency" and built a basic ability to make that judgment. But knowing why isn't enough; we also need to know what actually goes wrong in concurrent code. Frankly, the maddening thing about concurrency bugs isn't their complexity—it's that they are **unpredictable**. A multithreaded program might run perfectly one hundred thousand times on your local machine, then crash at 3 AM in a customer's environment after deployment. You pull up the core dump, and it completely contradicts your expectations!

These problems actually have well-defined concepts. We can start by simply listing them: data race, race condition, dead lock, livelock, starvation, and priority inversion. We will provide code examples for each problem—both buggy versions and fixed versions. Our goal isn't to memorize definitions, but to cultivate an intuition: when you look at a piece of multithreaded code, you can quickly identify where potential problems lie.

## Data Race: Undefined Behavior as Defined by the C++ Standard

This is the most important section in the entire volume. If you only remember one concept from this chapter, make it this: **a data race is undefined behavior (UB) in the C++ standard**. It's not "might go wrong," and it's not "uncertain result"—it is pure UB. This means the compiler is entitled to do absolutely anything when a data race occurs, including but not limited to returning incorrect results, crashing, or appearing to function normally while silently harboring hidden dangers.

### What the C++ Standard Says

The C++ standard ([intro.races]) defines a data race as follows: when two threads access the same memory location, at least one access is a write, and there is no happens-before relationship between them, a data race occurs. Any data race results in undefined behavior.

Why does the standard define this so strictly? Hans Boehm, one of the primary designers of the C++ memory model, explained the reason in an article: if data races were allowed to have any defined semantics (such as "might read a stale value"), many compiler optimizations would have to be prohibited. This is because compilers perform instruction reordering, loop transformations, constant propagation, and other optimizations on single-threaded code, and these optimizations can change the outcome of a data race in a multithreaded environment. The standard chose to define data races as UB specifically to avoid restricting the compiler's optimization capabilities—the trade-off is that programmers must ensure their programs are free of data races.

### A Minimal Data Race Example

```cpp
#include <thread>
#include <iostream>

// 不知道有没有学习单片机的朋友，笔者就注意到很多人很喜欢直接丢一个全局变量放在这里
// 当然，自己熟悉不是一种罪过，但是下面的代码中，我们这样编程就会出现问题。。。
int counter = 0;  // 全局变量，非 atomic

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        ++counter;  // 非原子写入
    }
}

int main()
{
    std::thread t1(increment, 1000000);
    std::thread t2(increment, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter << "\n";
    // 期望 2000000，实际可能是任何值：1345687, 1789234, ...
    return 0;
}
```

`++counter` looks like a single statement, but at the machine level it is a three-step operation: "read → add → write." When two threads execute this sequence simultaneously, the following situation can occur: Thread A reads counter=100, Thread B also reads counter=100, Thread A writes 101, and Thread B also writes 101—an increment is lost. In a loop of one million iterations, this loss happens frequently, and the final result will be far less than the expected 2,000,000.

### Fix: Using std::atomic

The most straightforward fix is to change `counter` to `std::atomic<int>`:

```cpp
#include <thread>
#include <iostream>
#include <atomic>

std::atomic<int> counter{0};

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
        // 或简单地写 ++counter;
    }
}

int main()
{
    std::thread t1(increment, 1000000);
    std::thread t2(increment, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter.load() << "\n";
    // 现在稳定输出 2000000
    return 0;
}
```

`std::atomic` guarantees that `fetch_add` is atomic—no intermediate state will be visible to other threads. We will dive deep into `memory_order_relaxed` and other memory order options in the later chapter on atomic operations. For now, you just need to know: `std::atomic` can eliminate data races.

Additionally, protecting the critical section with a `std::mutex` also eliminates data races. For more complex critical section logic, a mutex is often more appropriate. The choice between atomic and mutex depends on how complex your critical section is—if it's just a simple counter, atomic is lighter; if the critical section involves coordinated modifications to multiple variables, a mutex is safer and clearer.

## Race Condition: Logic-Level Contention

Race condition and data race are often used interchangeably, but they are not the same concept. A data race is a definition at the C++ standard level (two unsynchronized conflicting accesses), whereas a race condition is a broader concept: **the program's output depends on the execution order of threads**, and that order is nondeterministic.

A classic race condition example is the "check-then-act" pattern:

```cpp
#include <thread>
#include <iostream>
#include <vector>

std::vector<int> data;

void add_if_not_full(int value)
{
    if (data.size() < 100) {     // 检查
        data.push_back(value);   // 操作
    }
}
```

Even if we protect `push_back` with a `std::mutex` (thereby eliminating the data race), this function still has a race condition: two threads might simultaneously pass the `size() < 100` check, and then both execute `push_back`, causing the vector to actually hold more than 100 elements. The problem isn't whether memory accesses conflict, but rather that there is a time window between the "check" and the "act" where another thread can step in and change the state.

The key to fixing this is making the "check" and "act" an indivisible atomic operation—we will detail how to achieve this in the mutex chapter.

We can summarize the relationship between the two like this: a data race is always a race condition (because the result depends on the interleaving order), but a race condition is not necessarily a data race (even with correct synchronization primitives, logical contention can still exist). Eliminating data races is a baseline requirement; eliminating race conditions requires more careful interface design.

## Dead Lock: Waiting Forever

Dead lock is probably the most well-known concurrency bug. Its definition is straightforward: two or more threads wait on resources held by each other, causing all threads to be unable to continue execution. (When I was writing operating system code, I ran into this literally every day—just move, will you!)

For a dead lock to occur, four conditions must be met simultaneously (known as the Coffman conditions):

1. Mutual exclusion (a resource can only be held by one thread at a time)
2. Hold and wait (a thread holds at least one resource while waiting for other resources)
3. No preemption (resources cannot be forcibly taken away)
4. Circular wait (a circular chain of waiting threads exists).

As long as we break any one of these conditions, a dead lock cannot occur. Unfortunately, in real-world code, these four conditions are often very easily satisfied all at once.

Let's look at a minimal dead lock reproduction!

```cpp
#include <thread>
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 先锁 A
    std::cout << "thread1: locked A, waiting for B\n";
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 再锁 B
    std::cout << "thread1: locked A and B\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 先锁 B
    std::cout << "thread2: locked B, waiting for A\n";
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 再锁 A
    std::cout << "thread2: locked A and B\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    return 0;
}
```

If thread1 grabs mtx_a while thread2 grabs mtx_b at the same time, both sides get stuck—thread1 waits for mtx_b (held by thread2), and thread2 waits for mtx_a (held by thread1). Neither will let go.

### Fix: Consistent Lock Ordering

The most practical dead lock prevention strategy is **consistent lock ordering**: all code that needs to acquire multiple locks simultaneously must acquire them in the same order. If both thread1 and thread2 lock A first and then B, a dead lock is impossible—because only one thread can grab A first, and the other will wait on A, never waiting for A while holding B.

C++17 provides `std::scoped_lock`, which can acquire multiple mutexes at once using a dead lock-avoidance algorithm (internally trying different acquisition orders):

```cpp
#include <thread>
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void worker(int id)
{
    // scoped_lock 同时获取 mtx_a 和 mtx_b，内部避免死锁
    std::scoped_lock lock(mtx_a, mtx_b);
    std::cout << "thread" << id << ": locked both mutexes\n";
}

int main()
{
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();
    return 0;
}
```

Under the hood, `scoped_lock` uses a `std::try_lock` strategy: it tries to acquire all locks in a certain order, and if any acquisition fails, it releases the already-acquired locks and retries. This is a way to avoid dead locks without guaranteeing fairness. We will discuss various dead lock prevention strategies in more depth in the mutex chapter.

## Livelock: Busy Waiting

Livelock is the exact opposite of dead lock: the threads aren't stuck, the CPU is spinning, but the program just isn't making progress.

A typical scenario is "polite yielding"—two threads meet on a narrow bridge, each backs up to let the other pass, then they both move forward simultaneously, meet again, back up again... In code, this often happens in retry-based locking strategies: after a conflict, both sides back off and retry, but their backoff rhythms are too synchronized, causing them to collide on every retry.

Let's look at a simplified piece of code:

```cpp
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>

std::atomic<bool> flag1{false};
std::atomic<bool> flag2{false};

void thread1()
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        flag1.store(true);
        if (flag2.load()) {
            // 对方也想进，我退让
            flag1.store(false);
            continue;
        }
        // 进入临界区
        std::cout << "thread1 in critical section\n";
        flag1.store(false);
        return;
    }
    std::cout << "thread1: gave up after 100 attempts\n";
}

void thread2()
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        flag2.store(true);
        if (flag1.load()) {
            // 对方也想进，我退让
            flag2.store(false);
            continue;
        }
        // 进入临界区
        std::cout << "thread2 in critical section\n";
        flag2.store(false);
        return;
    }
    std::cout << "thread2: gave up after 100 attempts\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    return 0;
}
```

The problem with this code is that if the execution rhythms of two threads happen to align, they will keep yielding to each other. Of course, in actual execution, due to scheduling nondeterminism, they will most likely eventually enter the critical section (which is why the code uses a finite retry limit as a fallback), but the risk of livelock is real.

How do we solve this? The idea is to introduce **random backoff**—instead of retrying immediately after a conflict, wait for a random amount of time before trying again. This makes it very difficult for the two threads' rhythms to stay in sync. This idea is also everywhere in network protocols; for example, Ethernet's CSMA/CD relies on random backoff to resolve channel collisions.

## Starvation: Never Getting a Turn

Starvation is different from dead lock: dead lock means all threads are stuck, whereas starvation means certain threads are "starving"—they want to acquire a resource but never get a turn, while other threads run and eat as they please.

The most common scenario is an unfair scheduling strategy. For example, if a read-write lock always prioritizes read locks, then under a continuous stream of read requests, a writer thread might never get a chance—this is "writer starvation." Similarly, if a thread pool's task queue uses priority scheduling, low-priority tasks might never get scheduled.

The core idea for solving starvation is introducing **fairness**, and the specific approach depends on the scenario: a read-write lock can switch to a writer-priority strategy, a task queue can use round-robin or priority aging, and a lock implementation can use a fair lock like a ticket lock. Fairness usually comes at the cost of some throughput—after all, a fair scheduling strategy is more conservative than a greedy one—but this is a necessary price to pay to guarantee stable system operation.

## Priority Inversion: When High Priority Is Blocked by Low Priority

Priority inversion is a subtle but hugely impactful problem. Are there any folks coming from an embedded background? I'm sure you've all played with RTOSes and can recite the textbook answers better than anyone! The most classic case is the 1997 NASA **Mars Pathfinder** mission—the real-time system on the rover would suddenly reset while running. The ground team spent a good while investigating before discovering that priority inversion was the culprit: a high-priority bus management task was indirectly deadlocked by a low-priority meteorological task, causing the system to reboot repeatedly.

Let's break down this process. Suppose we have three tasks: `high_prio_task`, `mid_prio_task`, and `low_prio_task`, in decreasing order of priority. `low_prio_task` acquires a lock first and is using it. At this point, `mid_prio_task` becomes ready; since it has higher priority, it preempts `low_prio_task`. Immediately after, `high_prio_task` also becomes ready—it has the highest priority, but it needs the lock held by `low_prio_task`, so it can only block and wait. The problem is, `low_prio_task` has already been preempted by `mid_prio_task` at this moment, so it has no chance to run and naturally cannot release the lock. The result is: `high_prio_task`, the highest-priority task, is indirectly blocked by `mid_prio_task`, which has lower priority. This isn't a bug in any specific piece of code, but a structural flaw in the scheduling mechanism itself.

Back on the C++ side, `std::mutex` itself has no concept of priority, and the standard library doesn't manage scheduling policies, so on general-purpose platforms you generally don't need to worry about this. But if you are running C++ on an RTOS (like FreeRTOS or ThreadX), priority inversion is an unavoidable issue. The most common solution is **priority inheritance**—when `low_prio_task` holds a lock needed by `high_prio_task`, temporarily elevate `low_prio_task`'s priority to match `high_prio_task`'s. This way, `mid_prio_task` can't preempt it, `low_prio_task` can release the lock as quickly as possible, and `high_prio_task` doesn't have to keep waiting. The POSIX threads library provides `pthread_mutexattr_setprotocol` paired with `PTHREAD_PRIO_INHERIT` to enable this mechanism, and mainstream RTOSes basically all support similar operations.

## Categorizing the Problems: Our Roadmap

At this point, we have met the most common family of problems in concurrent programming. To facilitate subsequent learning, we can divide them into three categories:

**Correctness issues** are the baseline and must be eliminated. Data races lead to UB, and race conditions lead to logic errors—these are all "program behaves incorrectly" problems. The tools for eliminating data races are atomic and mutex, and eliminating race conditions also requires careful interface design (making the check and act indivisible). This is the core content of chapters 1 through 3.

**Liveness issues** are more subtle and need to be discovered through analysis and testing. Dead lock means "all threads are stuck," livelock means "threads are running but making no progress," and starvation means "some threads are starved." Solving them requires specific strategies: consistent lock ordering to prevent dead lock, random backoff to prevent livelock, and fair scheduling to prevent starvation. This is covered in chapters 2 and 4.

**Real-time issues** are less prominent in general applications but are crucial in embedded and real-time systems. Priority inversion is the most typical example, requiring operating system support (priority inheritance protocols). If your target platform is an RTOS environment like STM32, chapters 1 through 4 will include discussions of embedded scenarios.

Correctness first, performance second. Eliminate data races and race conditions first, then consider liveness and real-time issues. This order is important—if your program can't even guarantee correctness, talking about dead lock prevention or priority inheritance is meaningless.

## Exercises

### Exercise 1: Reproduce a Data Race

Compile and run the data race example above, running it multiple times to observe the results. Then switch to `std::atomic<int>` and confirm the result stabilizes at 2,000,000. Try increasing the number of threads (four, eight) and observe whether the deviation in the non-atomic version gets larger.

### Exercise 2: Reproduce a Dead Lock

Run the dead lock example above. The program will most likely hang (if it doesn't, try a few more times—triggering a dead lock depends on scheduling timing). Then replace the two `lock_guard` calls with `std::scoped_lock` and confirm the program exits normally.

### Exercise 3: Identify a Race Condition

Does the following code have a race condition? If so, where is the problem?

```cpp
std::map<std::string, int> cache;
std::mutex cache_mutex;

int get_or_compute(const std::string& key)
{
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
    }
    // 锁外计算
    int value = expensive_computation(key);
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[key] = value;
    }
    return value;
}
```

Hint: What happens if two threads simultaneously enter the "computation outside the lock" phase for the same key? The result might not be a bug (the最终 written value is the same), but what if `expensive_computation` has side effects or is very time-consuming? This is the "check-then-act" pattern manifesting in a more subtle form.

## References

- [[intro.races] C++ Standard Draft — eel.is](https://eel.is/c++draft/intro.races)
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
- [Multi-threaded executions and data races — cppreference](https://en.cppreference.com/cpp/language/multithread)
- [Dealing with Benign Data Races the C++ Way — Bartosz Milewski](https://bartoszmilewski.com/2014/10/25/dealing-with-benign-data-races-the-c-way/)
- [What Really Happened on Mars? — Mike Jones（Mars Pathfinder 优先级反转案例）](https://research.microsoft.com/en-us/um/people/mbj/mars_pathfinder/what_really_happened_on_mars.html)
