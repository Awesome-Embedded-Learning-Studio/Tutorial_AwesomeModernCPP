---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 'Recognize the most common concurrency bugs: data races, race conditions, deadlock, livelock, starvation, and priority inversion'
difficulty: beginner
order: 2
platform: host
prerequisites:
- Why We Need Concurrency
reading_time_minutes: 15
related:
- mutex and RAII Locks
- Atomic Operations
tags:
- host
- cpp-modern
- beginner
- atomic
- mutex
title: Fundamental Concurrency Problems
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/02-concurrency-problems.md
  source_hash: 84a7ab0e56750f1ae181056343577374fe728cc961d122f21acb75ad1073b2ca
  translated_at: '2026-09-26T06:17:17+00:00'
  engine: anthropic
  token_count: 3600
---
# Fundamental Concurrency Problems

In the previous article we talked about "Why We Need Concurrency" and built up some basic judgment. But knowing why isn't enough — we also need to know what actually goes wrong in concurrent code. Honestly, the maddening thing about concurrency bugs isn't that they're complicated — it's that they're **unpredictable**. A multithreaded program runs fine a hundred thousand times on your own machine, then ships, and at three in the morning it crashes in a customer's environment — you pull the dump, look at it, and it matches nothing you expected!

The nice thing is that each of these problems does have a crisp, well-defined concept behind it, so we can simply list them up front: data races, race conditions, deadlock, livelock, starvation, and priority inversion. For each one we'll show code — a buggy version and a fixed version. The goal is not to memorize definitions but to build an intuition: look at a piece of multithreaded code, and you can quickly judge where it might go wrong.

## data race: Undefined Behavior According to the C++ Standard

This is the most important section in the entire volume. If you take away just one point from this article, take this one: **a data race is undefined behavior (Undefined Behavior, UB) under the C++ standard**. Not "might go wrong", not "the result is indeterminate" — full-blown UB. It means the compiler is entitled to do literally anything when a data race occurs, including but not limited to returning wrong results, crashing, or looking fine on the surface while quietly burying a hidden hazard.

### What the C++ Standard Says

The C++ standard ([intro.races]) defines a data race as follows: when two threads access the same memory location, at least one of the accesses is a write, and there is no happens-before relationship between them, a data race occurs. Any data race results in undefined behavior.

Why does the standard come down so hard? Hans Boehm (one of the principal designers of the C++ memory model) explained the reasoning in an article: if data races were allowed any well-defined semantics (say, "you might read a stale value"), a great many compiler optimizations would have to be outlawed. Compilers want to reorder instructions, transform loops, and propagate constants for single-threaded code — and in a multithreaded setting those optimizations can change the outcome of a data race. The standard chose to define data races as UB precisely so it would not have to constrain the compiler's ability to optimize — the price being that we programmers must guarantee our programs are free of data races.

### A Minimal data race Example

```cpp
#include <thread>
#include <iostream>

// For any friends here learning microcontrollers: the author has noticed that a lot of people love to just drop a global variable right here
// Of course, being comfortable with a habit is no crime, but in the code below, programming this way is exactly where the trouble starts...
int counter = 0;  // global variable, not atomic

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        ++counter;  // non-atomic write
    }
}

int main()
{
    std::thread t1(increment, 1000000);
    std::thread t2(increment, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter << "\n";
    // expected 2000000; the actual value can be anything: 1345687, 1789234, ...
    return 0;
}
```

`++counter` looks like one statement, but at the machine level it is a three-step sequence: read → add → write. When two threads execute that sequence at the same time, this can happen: thread A reads counter=100, thread B also reads counter=100, thread A writes 101, thread B also writes 101 — one increment is lost. In a loop of a million iterations those losses pile up, and the final result lands far below the expected 2000000.

### The Fix: Use std::atomic

The most direct fix is to change `counter` to `std::atomic<int>`:

```cpp
#include <thread>
#include <iostream>
#include <atomic>

std::atomic<int> counter{0};

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
        // or simply ++counter;
    }
}

int main()
{
    std::thread t1(increment, 1000000);
    std::thread t2(increment, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter.load() << "\n";
    // now reliably prints 2000000
    return 0;
}
```

`std::atomic` guarantees that `fetch_add` is atomic — no intermediate state is ever visible to another thread. We'll go deep into `memory_order_relaxed` and the other memory-ordering options in the atomic-operations chapter later on. For now, all you need to know is: `std::atomic` eliminates data races.

Alternatively, protecting the critical section with a `std::mutex` also eliminates the data race, and for more involved critical-section logic a mutex is often the better fit. Atomic or mutex comes down to how complex your critical section is — for a plain counter, atomic is lighter; when the critical section involves coordinated changes to several variables, a mutex is both safer and clearer.

## race condition: Racing at the Logic Level

"race condition" and "data race" get used interchangeably all the time, but they are not the same concept. A data race is a definition at the level of the C++ standard (two conflicting accesses without synchronization), while a race condition is the broader notion: **the program's output depends on the order in which threads execute**, and that order is not determined.

A classic race condition is the "check-then-act" pattern:

```cpp
#include <thread>
#include <iostream>
#include <vector>

std::vector<int> data;

void add_if_not_full(int value)
{
    if (data.size() < 100) {     // check
        data.push_back(value);   // act
    }
}
```

Even if we protect `push_back` with a `std::mutex` (and thereby have no data race), this function still has a race condition: two threads can pass the `size() < 100` check at the same moment and then both execute `push_back`, leaving the vector holding more than 100 elements. The problem is not whether the memory accesses conflict — it's that a window of time sits between "check" and "act", and other threads can slip in during that window and change the state.

The key to the fix is to make "check" and "act" one indivisible atomic operation — the mutex chapter works out in detail how to accomplish that.

The relationship between the two can be summed up like this: a data race is always a race condition (because the result depends on the interleaving order), but a race condition is not necessarily a data race (even with the right synchronization primitives, the logic can still race). Eliminating data races is the baseline requirement; eliminating race conditions calls for more careful interface design.

## Deadlock: Waiting Forever

Deadlock is probably the most famous concurrency bug of all. The definition: two or more threads each wait for resources held by the others, so that none of them can make any progress. (Back in the author's operating-system-writing days this was a daily sighting — come on, one of you move already!)

For a deadlock to happen, four conditions must hold simultaneously (known as the four Coffman conditions):

1. Mutual exclusion (a resource can be held by only one thread at a time)
2. Hold and wait (a thread holds at least one resource while waiting for others)
3. No preemption (a resource cannot be forcibly taken away)
4. Circular wait (a cycle of threads exists, each waiting on the next)

Break any one of these conditions and deadlock cannot occur. Unfortunately, in real-world code all four are often satisfied together with remarkable ease.

Let's have a minimal deadlock reproduction!

```cpp
#include <thread>
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);  // lock A first
    std::cout << "thread1: locked A, waiting for B\n";
    std::lock_guard<std::mutex> lock_b(mtx_b);  // then lock B
    std::cout << "thread1: locked A and B\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);  // lock B first
    std::cout << "thread2: locked B, waiting for A\n";
    std::lock_guard<std::mutex> lock_a(mtx_a);  // then lock A
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

If thread1 grabs mtx_a at the same instant thread2 grabs mtx_b, both sides are wedged — thread1 waits for mtx_b (held by thread2), thread2 waits for mtx_a (held by thread1), and neither will ever let go.

### The Fix: Consistent Lock Ordering

The most practical deadlock-prevention strategy is **consistent lock ordering**: all code that needs to acquire several locks at once must acquire them in the same order. If both thread1 and thread2 lock A first and then B, deadlock becomes impossible — only one thread can get A first, and the other waits on A; it never waits for A while holding B.

C++17 provides `std::scoped_lock`, which acquires multiple mutexes in one shot and uses a deadlock-avoidance algorithm (internally trying different acquisition orders):

```cpp
#include <thread>
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void worker(int id)
{
    // scoped_lock acquires mtx_a and mtx_b together, avoiding deadlock internally
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

Under the hood, `scoped_lock` follows a `std::try_lock`-style strategy: try to acquire all the locks in some order, and if any acquisition fails, release the ones already held and retry. It avoids deadlock but guarantees nothing about fairness. We'll discuss the various deadlock-prevention strategies in more depth in the mutex chapter later.

## Livelock: Busy but Going Nowhere

A livelock is the exact opposite of a deadlock: the threads aren't stuck, the CPU is spinning, but the program just won't move forward.

The classic scene is "polite yielding" — two threads meet on a narrow bridge, each steps back to let the other cross first, then both advance at the same time, meet again, step back again... In code this shows up constantly in retry-based locking strategies: after a collision both sides back off and retry, but their backoff rhythm is so synchronized that every retry collides again.

Let's look at some simplified code:

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
            // the other side wants in too; I yield
            flag1.store(false);
            continue;
        }
        // enter the critical section
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
            // the other side wants in too; I yield
            flag2.store(false);
            continue;
        }
        // enter the critical section
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

The problem with this code: if the two threads' execution rhythms happen to line up, they will keep yielding to each other over and over. In practice, scheduling nondeterminism means they will most likely reach the critical section eventually (which is why the code caps things with a finite number of retries as a safety net), but the livelock risk is real.

How do we fix it? The idea is to introduce **random backoff** — after a collision, don't retry immediately; wait a random interval before trying again, and the two threads' rhythms will have a hard time staying in sync. The same idea is all over networking protocols: Ethernet's CSMA/CD, for instance, relies on random backoff to resolve channel collisions.

## Starvation: Never Your Turn

Starvation differs from deadlock: in deadlock every thread is stuck; under starvation, certain threads are left "hungry" — they want the resource but it is never their turn, while the other threads keep running and feeding as usual.

The most common scenario is an unfair scheduling policy. Say a read-write lock always grants read locks preferentially: under a steady stream of read requests, the writer thread may never get its chance — that's "writer starvation". Likewise, if a thread pool's task queue uses priority scheduling, low-priority tasks may never make it to the front of the line.

The core idea for solving starvation is to introduce **fairness**, with the concrete technique depending on the scenario: the read-write lock can switch to a writer-preference policy, the task queue can use round-robin or priority aging, and the lock implementation can use a fair lock such as the ticket lock. Fairness usually costs some throughput — a fair scheduling policy is more conservative than a greedy one, after all — but it's the necessary price of keeping a system running stably.

## Priority Inversion: When High Priority Gets Blocked by Low Priority

Priority inversion is a sneaky problem with outsized impact. Any embedded folks in the room? You've all played with RTOSes, and I trust everyone here can recite the interview-script answer better than the next person! The most famous case is NASA's 1997 **Mars Pathfinder** probe — the real-time system on the probe would reset itself mid-run, and the ground team spent quite a while tracking it down before they found priority inversion pulling the strings: a high-priority bus-management task was indirectly wedged by a low-priority meteorological task, and the system kept rebooting.

Let's take the process apart. Suppose there are three tasks — `high_prio_task`, `mid_prio_task`, and `low_prio_task` — with strictly decreasing priority. `low_prio_task` takes a lock first and is busy using it; then `mid_prio_task` becomes ready, has higher priority, and preempts `low_prio_task`. Right after that, `high_prio_task` also becomes ready — it has the highest priority, but it needs the very lock `low_prio_task` holds, so it has no choice but to block and wait. The trouble is that `low_prio_task`, freshly preempted by `mid_prio_task`, never gets to run at all — and so has no way to release the lock. The upshot: `high_prio_task`, the highest-priority task in the system, is indirectly stuck behind `mid_prio_task`, whose priority is lower than its own. No particular line of code is wrong — this is a structural flaw of the scheduling mechanism itself.

Back on the C++ side: `std::mutex` itself has no notion of priority, and the standard library stays out of scheduling policy, so on general-purpose platforms you usually don't have to worry about this. But if you run C++ on an RTOS (FreeRTOS or ThreadX, say), priority inversion is a problem you cannot dodge. The most common remedy is **priority inheritance**: when `low_prio_task` holds the lock `high_prio_task` needs, temporarily raise `low_prio_task`'s priority to match `high_prio_task`'s. Now `mid_prio_task` can no longer outmuscle it, `low_prio_task` can release the lock as quickly as possible, and `high_prio_task` doesn't sit waiting indefinitely. The POSIX thread library offers `pthread_mutexattr_setprotocol` together with `PTHREAD_PRIO_INHERIT` to enable the mechanism, and mainstream RTOSes broadly support something similar.

## Classifying the Problems: Our Roadmap

At this point we've met the most common family of concurrency problems. To make what follows easier to study, let's sort them into three categories:

**Correctness problems** are the baseline and must be eliminated. Data races lead to UB; race conditions lead to logic errors — these are all "the program behaves wrongly" problems. The tools for eliminating data races are atomic and mutex; eliminating race conditions additionally takes careful interface design (making check and act indivisible). That is the core subject of ch01-ch03.

**Liveness problems** are sneakier and have to be ferreted out through analysis and testing. Deadlock is "every thread is stuck"; livelock is "threads are running but making no progress"; starvation is "some threads go hungry". Each calls for its own strategy: consistent lock ordering against deadlock, random backoff against livelock, fair scheduling against starvation. That is the subject of ch02 and ch04.

**Real-time problems** rarely stand out in ordinary applications, but they are critical in embedded and real-time systems. Priority inversion is the most typical example, and it needs operating-system support (the priority inheritance protocol). If your target platform is an RTOS environment such as STM32, ch01-ch04 weave in discussions of embedded scenarios along the way.

Correctness first, performance second. First eliminate data races and race conditions, then worry about liveness and real-time issues. The order matters — if your program can't even guarantee correctness, debating deadlock prevention or priority inheritance is pointless.

## Exercises

### Exercise 1: Reproduce a data race

Compile and run the data race example above and run it several times, watching the results. Then switch to `std::atomic<int>` and confirm that the result settles at 2000000. Try raising the number of threads (4, 8) and see whether the non-atomic version drifts even further off.

### Exercise 2: Reproduce a Deadlock

Run the deadlock example above. The program will most likely hang (if it doesn't, try a few more times — triggering a deadlock depends on scheduling timing). Then replace the two `lock_guard`s with `std::scoped_lock` and confirm that the program exits normally.

### Exercise 3: Spot the race condition

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
    // compute outside the lock
    int value = expensive_computation(key);
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[key] = value;
    }
    return value;
}
```

Hint: what happens if two threads enter the "compute outside the lock" phase for the same key at the same time? The outcome may not be a bug (both threads end up writing the same value), but what if `expensive_computation` has side effects, or is expensive to run? That's "check-then-act" showing up in a sneakier form.

## References

- [[intro.races] C++ Standard Draft — eel.is](https://eel.is/c++draft/intro.races)
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
- [Multi-threaded executions and data races — cppreference](https://en.cppreference.com/cpp/language/multithread)
- [Dealing with Benign Data Races the C++ Way — Bartosz Milewski](https://bartoszmilewski.com/2014/10/25/dealing-with-benign-data-races-the-c-way/)
- [What Really Happened on Mars? — Mike Jones (the Mars Pathfinder priority inversion case)](https://research.microsoft.com/en-us/um/people/mbj/mars_pathfinder/what_really_happened_on_mars.html)
