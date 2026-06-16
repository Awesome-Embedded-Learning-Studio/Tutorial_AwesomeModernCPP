---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 'Identify the most common bugs in concurrent programming: data races,
  race conditions, deadlocks, livelocks, starvation, and priority inversion.'
difficulty: beginner
order: 2
platform: host
prerequisites:
- 为什么需要并发
reading_time_minutes: 15
related:
- mutex 与 RAII 守卫
- std::atomic 原子操作
tags:
- host
- cpp-modern
- beginner
- atomic
- mutex
title: Concurrency Fundamentals
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/02-concurrency-problems.md
  source_hash: 84a7ab0e56750f1ae181056343577374fe728cc961d122f21acb75ad1073b2ca
  translated_at: '2026-06-16T04:03:01.395769+00:00'
  engine: anthropic
  token_count: 2610
---
# Fundamental Concurrency Issues

In the previous post, we discussed "why we need concurrency" and established a basic framework for judgment. But knowing *why* isn't enough; we also need to know exactly what goes wrong in concurrent code. Frankly speaking, the headache of concurrency bugs isn't about how complex they are—it's that they are **unpredictable**. A multi-threaded program might run perfectly on your machine a hundred thousand times, only to crash in a customer's environment at 3 AM. You look at the dump, and it makes absolutely no sense!

These issues actually have well-defined concepts. Let's list them simply: data race, race condition, dead lock, live lock, starvation, and priority inversion. For each issue, we will provide code examples—both broken and fixed versions. Our goal is not to memorize definitions, but to cultivate an intuition: when you see a piece of multi-threaded code, you can quickly identify where the potential problems lie.

## Data Race: Undefined Behavior per the C++ Standard

This is the most important section of the entire volume. If you remember only one thing from this article, make it this: **a data race is Undefined Behavior (UB) in the C++ standard**. Not "might go wrong," not "result is indeterminate," but full-blown UB—meaning the compiler has the right to do *anything* when a data race occurs, including but not limited to returning incorrect results, crashing, or appearing to function normally while harboring hidden dangers.

### What the C++ Standard Says

The C++ standard ([intro.races]) defines a data race as: when two threads access the same memory location, at least one access is a write, and there is no happens-before relationship between them, it constitutes a data race. Any data race results in undefined behavior.

Why does the standard have to be so strict? Hans Boehm (one of the main designers of the C++ memory model) explained the reason in an article: if data races were allowed to have any defined semantics (like "might read an old value"), many compiler optimizations would have to be prohibited. Because compilers need to perform instruction reordering, loop transformations, constant propagation, and other optimizations on single-threaded code, and these optimizations can change the results of data races in a multi-threaded environment. The standard chose to define data races as UB specifically to not limit compiler optimization capabilities—the price is that programmers must ensure their programs are data-race-free.

### A Minimal Data Race Example

```cpp
#include <thread>
#include <iostream>

int counter = 0;

void increment() {
    for (int i = 0; i < 1000000; ++i) {
        counter++;  // Data race here!
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);

    t1.join();
    t2.join();

    std::cout << "Final counter: " << counter << "\n"; // Expect 2000000, but often less
    return 0;
}
```

`counter++` looks like a single statement, but at the machine level, it is a three-step operation: "read → add → write". When two threads execute this sequence simultaneously, this can happen: Thread A reads `counter=100`, Thread B also reads `counter=100`, Thread A writes `101`, Thread B also writes `101`—an increment is lost. In a loop of a million iterations, this loss happens frequently, and the final result is far less than the expected 2,000,000.

### Fix: Use `std::atomic`

The most direct fix is to change `int counter` to `std::atomic<int> counter`:

```cpp
#include <thread>
#include <iostream>
#include <atomic>

std::atomic<int> counter = 0;

void increment() {
    for (int i = 0; i < 1000000; ++i) {
        counter++;  // Atomic operation
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);

    t1.join();
    t2.join();

    std::cout << "Final counter: " << counter << "\n"; // Always 2000000
    return 0;
}
```

`std::atomic` guarantees that `counter++` is atomic—no intermediate state will be visible to other threads. We will dive deeper into `memory_order` and other memory ordering options in the chapter on atomic operations. For now, just know: `std::atomic` eliminates data races.

Additionally, using a `mutex` to protect the critical section can also eliminate data races. For more complex critical section logic, a mutex is often more appropriate. Choosing between atomic and mutex depends on how complex your critical section is—if it's just a simple counter, atomic is lighter; if the critical section involves coordinated modification of multiple variables, a mutex is safer and clearer.

## Race Condition: Logic-Level Competition

Race condition and data race are often used interchangeably, but they are not the same concept. A data race is a definition at the C++ standard level (two unsynchronized conflicting accesses), while a race condition is a broader concept: **the program's output depends on the execution order of threads**, and that order is nondeterministic.

A classic example of a race condition is the "check-then-act" pattern:

```cpp
std::vector<int> vec;
std::mutex vec_mutex;

void safe_push(int value) {
    std::lock_guard<std::mutex> lock(vec_mutex);

    if (vec.size() < 100) {          // Check
        vec.push_back(value);        // Act
    }
}
```

Even if we use `std::mutex` to protect `vec` (thus avoiding data race), this function still has a race condition: two threads might simultaneously pass the `vec.size() < 100` check, and then both execute `vec.push_back(value)`, causing the vector to actually hold more than 100 elements. The problem isn't conflicting memory accesses, but rather a time window between "check" and "act" where another thread can step in and change state.

The key to the fix is to make "check" and "act" an indivisible atomic operation—we will detail how to achieve this in the mutex chapter.

We can summarize the relationship between the two like this: a data race is always a race condition (because the result depends on the interleaving order), but a race condition is not necessarily a data race (even with correct synchronization primitives, logic can still race). Eliminating data races is a baseline requirement; eliminating race conditions requires more careful interface design.

## Deadlock: The Eternal Wait

Deadlock is likely the most well-known concurrency bug. Its definition is: two or more threads wait for resources held by each other, causing all threads to be unable to proceed. (When I was writing about operating systems, I encountered this every day—move it! just move a bit!)

For a deadlock to occur, four conditions must be met simultaneously (known as the Coffman conditions):

1. Mutual Exclusion (a resource can only be held by one thread at a time)
2. Hold and Wait (a thread holds at least one resource while waiting for others)
3. No Preemption (resources cannot be forcibly taken)
4. Circular Wait (there exists a cycle of threads waiting for each other).

As long as one of these conditions is broken, a deadlock cannot occur. Unfortunately, in actual code, these four conditions are often very easily satisfied simultaneously.

Let's reproduce a minimal deadlock!

```cpp
#include <thread>
#include <mutex>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1_func() {
    std::lock_guard<std::mutex> lock1(mtx_a);
    // Simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::lock_guard<std::mutex> lock2(mtx_b); // Deadlock might happen here
}

void thread2_func() {
    std::lock_guard<std::mutex> lock1(mtx_b);
    // Simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::lock_guard<std::mutex> lock2(mtx_a); // Deadlock might happen here
}

int main() {
    std::thread t1(thread1_func);
    std::thread t2(thread2_func);

    t1.join();
    t2.join();
    return 0;
}
```

If thread1 acquires `mtx_a` while thread2 acquires `mtx_b`, both get stuck—thread1 waits for `mtx_b` (held by thread2), thread2 waits for `mtx_a` (held by thread1), and neither lets go.

### Fix: Unified Locking Order

The most practical deadlock prevention strategy is **unified locking order**: all code that needs to acquire multiple locks must acquire them in the same order. If both thread1 and thread2 lock A first and then B, a deadlock is impossible—because only one thread can acquire A first, and the other will wait on A, preventing it from waiting for A while holding B.

C++17 provides `std::scoped_lock`, which can acquire multiple mutexes at once using a deadlock-avoidance algorithm (internally trying different acquisition orders):

```cpp
void thread1_func() {
    std::scoped_lock lock(mtx_a, mtx_b); // Acquires both without deadlock
    // ...
}

void thread2_func() {
    std::scoped_lock lock(mtx_b, mtx_a); // Order doesn't matter for scoped_lock
    // ...
}
```

`std::scoped_lock` uses a strategy similar to "try and back off": it attempts to acquire all locks in some order; if an acquisition fails, it releases acquired locks and retries. This is a way to avoid deadlock but doesn't guarantee fairness. We will discuss various deadlock prevention strategies in more depth in the mutex chapter.

## Livelock: Busy Waiting

Livelock is the opposite of deadlock: threads aren't stuck, the CPU is spinning, but the program just isn't making progress.

A typical scenario is "polite yielding"—two threads meet on a narrow bridge, each backs up to let the other pass, then they move forward simultaneously, meet again, back up again... In code, this often happens in retry-based locking strategies: after a conflict, both sides back off and retry, but the rhythm of backing off is too synchronized, causing every retry to collide again.

Let's look at a simplified code snippet:

```cpp
#include <thread>
#include <mutex>
#include <atomic>

std::mutex mtx;
std::atomic<bool> conflict_detected(false);

void worker() {
    for (int attempts = 0; attempts < 100; ++attempts) {
        if (mtx.try_lock()) {
            // Do work
            mtx.unlock();
            return;
        }
    }
    // If we are here, we failed too many times (Livelock symptom)
}

int main() {
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
    return 0;
}
```

The problem with this code is: if the execution rhythm of two threads happens to align, they will constantly yield to each other. Of course, in actual execution, due to scheduling uncertainty, they will likely eventually enter the critical section (hence the code uses a limited retry as a fallback), but the risk of livelock is real.

How to solve it? The idea is to introduce **random backoff**—don't retry immediately after a conflict, but wait for a random time before trying again. This makes it hard for the threads' rhythms to stay synchronized. This idea is also common in network protocols; for example, Ethernet's CSMA/CD relies on random backoff to resolve channel conflicts.

## Starvation: Never Getting a Turn

Starvation is different from deadlock: deadlock is where all threads are stuck, starvation is where some threads are "starved"—it wants the resource, but never gets a turn, while other threads run and eat as they please.

The most common scenario is unfair scheduling policies. For example, if a read-write lock always prioritizes read locks, then under continuous read requests, a writer thread might wait forever for an opportunity—this is "writer starvation". Similarly, if a thread pool's task queue uses priority scheduling, low-priority tasks might never get scheduled.

The core idea for solving starvation is to introduce **fairness**. The specific method depends on the scenario: a read-write lock can use a write-priority strategy, a task queue can use round-robin or priority aging, and lock implementations can use fair locks like ticket locks. Fairness usually sacrifices some throughput—after all, fair scheduling strategies are more conservative than greedy ones—but it is a necessary price to guarantee stable system operation.

## Priority Inversion: When High Priority Is Blocked by Low Priority

Priority inversion is a subtle but hugely impactful problem. Any embedded folks here? You've all used RTOS, right? I bet you've all memorized the standard answers better than anyone else! The most classic case is the 1997 NASA **Mars Pathfinder** Mars probe—the real-time system on the probe would reset as it ran. The ground team investigated for a good while before discovering that priority inversion was the culprit: a high-priority bus management task was indirectly blocked by a low-priority meteorology task, causing the system to reboot repeatedly.

Let's break down this process. Suppose there are three tasks: `High`, `Medium`, `Low`, with priorities decreasing in that order. `Low` first acquires a lock and is using it. At this point, `Medium` becomes ready; it has higher priority, so it preempts `Low`. Immediately after, `High` also becomes ready—it has the highest priority, but it needs the lock held by `Low`, so it has to block and wait. The problem is, `Low` has already been preempted by `Medium`, so it doesn't get a chance to run, and naturally can't release the lock. The result is: `High`, the highest priority task, is indirectly blocked by `Low`, which has a lower priority. This isn't a code error, but a structural flaw in the scheduling mechanism itself.

Back to the C++ side, `std::mutex` has no concept of priority, and the standard library doesn't manage scheduling policies, so on general platforms you usually don't need to worry about this. But if you run C++ on an RTOS (like FreeRTOS, ThreadX), priority inversion is an unavoidable issue. The most common solution is **priority inheritance**—when `Low` holds a lock needed by `High`, temporarily boost `Low`'s priority to match `High`'s. This way `Medium` can't preempt it, `Low` can release the lock as soon as possible, and `High` doesn't have to wait indefinitely. The POSIX thread library provides `pthread_mutexattr_setprotocol` with `PTHREAD_PRIO_INHERIT` to enable this mechanism, and mainstream RTOSes basically all support similar operations.

## Categorizing Problems: Our Roadmap

At this point, we have met the most common family of issues in concurrent programming. To facilitate future learning, we divide them into three categories:

**Correctness issues** are the baseline and must be eliminated. Data races lead to UB, and race conditions lead to logic errors—these are "program behavior is incorrect" issues. Tools to eliminate data races are atomic and mutex; eliminating race conditions also requires careful interface design (making check and act indivisible). This is the core content of chapters 1 through 3.

**Liveness issues** are more subtle and require analysis and testing to discover. Deadlock is "all threads stuck", livelock is "threads running but no progress", starvation is "some threads are starved". Solving them requires specific strategies: unified lock order prevents deadlock, random backoff prevents livelock, fair scheduling prevents starvation. This is covered in chapters 2 and 4.

**Real-time issues** are less prominent in general applications, but are crucial in embedded and real-time systems. Priority inversion is the most typical example, requiring operating system support (priority inheritance protocol). If your target platform is an RTOS environment like STM32, chapters 1 through 4 will include discussions of embedded scenarios.

Correctness first, then performance. Eliminate data races and race conditions first, then consider liveness and real-time issues. This order is important—if your program can't even guarantee correctness, discussing deadlock prevention or priority inheritance is meaningless.

## Exercises

### Exercise 1: Reproduce a Data Race

Compile and run the data race example above. Run it multiple times and observe the results. Then switch to `std::atomic<int>` and confirm the result stabilizes at 2,000,000. Try increasing the number of threads (4, 8) and observe if the deviation in the non-atomic version is larger.

### Exercise 2: Reproduce a Deadlock

Run the deadlock example above. The program will most likely get stuck (if it doesn't, try a few more times—deadlock triggering depends on scheduling timing). Then replace the two `std::lock_guard`s with `std::scoped_lock` and confirm the program exits normally.

### Exercise 3: Identify a Race Condition

Does the following code have a race condition? If so, where is the problem?

```cpp
std::mutex map_mutex;
std::map<int, int> cache;

int get_value(int key) {
    {
        std::lock_guard<std::mutex> lock(map_mutex);
        if (cache.count(key)) return cache[key];
    }

    // Expensive calculation outside the lock
    int value = expensive_calculation(key);

    {
        std::lock_guard<std::mutex> lock(map_mutex);
        cache[key] = value;
        return value;
    }
}
```

Hint: If two threads enter the "calculation outside lock" phase for the same key simultaneously, what happens? The result might not be a bug (the final written value is the same), but what if `expensive_calculation` has side effects or is very time-consuming? This is a reflection of "check-then-act" in a more hidden form.

## References

- [[intro.races] C++ Standard Draft — eel.is](https://eel.is/c++draft/intro.races)
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
- [Multi-threaded executions and data races — cppreference](https://en.cppreference.com/cpp/language/multithread)
- [Dealing with Benign Data Races the C++ Way — Bartosz Milewski](https://bartoszmilewski.com/2014/10/25/dealing-with-benign-data-races-the-c-way/)
- [What Really Happened on Mars? — Mike Jones (Mars Pathfinder Priority Inversion Case)](https://research.microsoft.com/en-us/um/people/mbj/mars_pathfinder/what_really_happened_on_mars.html)
