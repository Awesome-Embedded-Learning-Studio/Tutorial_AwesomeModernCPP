---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Systematically review the mutex family and RAII lock guards, covering
  the evolution from `lock_guard` to `scoped_lock` and best practices.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 线程所有权与 RAII
reading_time_minutes: 15
related:
- 死锁与锁顺序
- condition_variable 与等待语义
tags:
- host
- cpp-modern
- intermediate
- mutex
- RAII守卫
title: Mutex and RAII Lock
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/01-mutex-and-raii-guards.md
  source_hash: 04b39e9664388f01aa57d869f1713f276df3a6ba7565c60dce941f9d16181c72
  token_count: 3181
  translated_at: '2026-06-15T09:24:57.429078+00:00'
---
# mutex and RAII Locks

In the previous post, we discussed thread ownership and RAII, mastering the lifecycle management of `std::thread` and the concept of scope-based resource control. Now, the question arises: with threads in play, how do they safely share data? We have already seen the power of data races in the concurrency basics post—two threads writing to the same `int` can result in 1,345,687 instead of 2,000,000. The most common solution to data races is the mutex, and the C++ Standard Library provides a whole family of mutexes and accompanying RAII lock guards.

In this post, our goal is clear: first, we will go through the four members of the mutex family—`std::mutex`, `std::recursive_mutex`, `std::timed_mutex`, and `std::shared_mutex`—one by one to understand what problems they solve. Then, we will systematically review three RAII lock guards—`std::lock_guard`, `std::unique_lock`, and `std::scoped_lock`—which are the tools that should actually appear in our daily code. Throughout this process, we will repeatedly emphasize one principle: never manually call `lock()` and `unlock()`.

## std::mutex: The Basic Mutex

`std::mutex` is the standard mutex introduced in C++11, defined in the `<mutex>` header file. It provides only three operations: `lock()`, `unlock()`, and `try_lock()`.

`lock()` is a blocking call—if the mutex is already held by another thread, the current thread blocks and waits until it acquires the lock. `unlock()` releases the lock. `try_lock()` is the non-blocking version—it attempts to acquire the lock, returning `true` on success and `false` on failure, without waiting. These three operations constitute the entire interface of a mutex, simple enough to be suspicious.

Don't rush to think simplicity means no pitfalls. Look at this "hand-crafted" code:

```cpp
std::mutex mtx;
int counter = 0;

void unsafe_increment() {
    mtx.lock();
    // ... do some work ...
    counter++;
    // ... more work that might throw ...
    mtx.unlock();
}
```

This code works under normal paths, but it has several fatal hidden dangers. If an exception is thrown between `mtx.lock()` and `mtx.unlock()` (of course, `counter++` won't throw, but what if you replace `counter` with a complex type, or insert other operations that might throw in between?), `mtx.unlock()` will never be executed. The lock isn't released, and all other threads waiting for this lock block—this isn't strictly a deadlock, but the effect is similar, and it's harder to debug because the program doesn't freeze in an obvious loop, but rather "mysteriously" stops.

A worse scenario involves multiple return paths. If you have three or four `if` branches inside your critical section, you need to write `mtx.unlock()` before each branch. Missing one means a bug. In large codebases, this "manual pairing of lock/unlock" pattern is nearly impossible to guarantee correctness.

There is also a classic pitfall: the same lock being locked twice by the same thread. `std::mutex` does not allow the same thread to lock repeatedly—if you call `lock()` while already holding the lock, the result is undefined behavior (most implementations will deadlock immediately). This is easy to stumble into unknowingly when function call chains are complex:

```cpp
void bad_recursive_call() {
    mtx.lock();
    // ... some logic ...
    bad_recursive_call(); // Oops, deadlock here!
    mtx.unlock();
}
```

So the conclusion is clear: the direct interface of `std::mutex` should not appear in application code. Its design intent is to serve as the underlying cornerstone for RAII wrappers, not for you to manually `lock()`/`unlock()` every day.

## std::recursive_mutex: Allowing Recursive Locking

`std::recursive_mutex` solves the "same thread re-locking" problem mentioned above. It internally maintains a lock counter—the first time a thread locks it, the counter becomes 1; the second time, 2; and so on. Each call to `unlock()` decrements the counter; the lock is only truly released when the counter reaches 0.

```cpp
std::recursive_mutex rec_mtx;

void recursive_function(int n) {
    std::lock_guard<std::recursive_mutex> lock(rec_mtx);
    if (n > 0) {
        recursive_function(n - 1);
    }
}
```

This code is completely legal—`std::recursive_mutex` allows the same thread to lock multiple times. Each recursive call increases the counter, and each return triggers the destructor of the `std::unique_lock` (or `lock_guard`) to decrement the counter. The lock is only truly released when the outermost function returns.

However, `std::recursive_mutex` is often a signal of a design smell. If you need a recursive lock, it's likely because your interface design mixes "functions that need to be called under lock protection" with "internal implementations that don't need locks." A better approach is to extract the "operations under lock protection" into an internal function without locking, and let the outer interface handle the locking. A recursive lock is a crutch; it helps you walk, but you shouldn't rely on it.

## std::timed_mutex: Mutex with Timeout

`std::timed_mutex` adds two timeout-based locking operations to `std::mutex`: `try_lock_for()` and `try_lock_until()`.

`try_lock_for()` accepts a time duration (`std::chrono::duration`), repeatedly attempting to acquire the lock within the specified time, and returns `false` on timeout. `try_lock_until()` accepts an absolute time point (`std::chrono::time_point`), attempting to acquire the lock before the specified moment, and returns `false` on timeout. The difference is similar to "wait for at most 100 milliseconds" versus "wait until 3 PM."

```cpp
std::timed_mutex t_mtx;

void try_update() {
    if (t_mtx.try_lock_for(std::chrono::milliseconds(100))) {
        std::lock_guard<std::timed_mutex> lock(t_mtx, std::adopt_lock);
        // Critical section
    } else {
        // Handle timeout
    }
}
```

`std::recursive_timed_mutex` is a combination of a recursive lock and a timed lock—the same thread can lock multiple times, and it supports `try_lock_for()` and `try_lock_until()`. It is rarely used in actual engineering; just knowing it exists is enough.

A quick reminder: locks with timeouts can have higher overhead on some platforms because they interact with the system clock. If your scenario doesn't require timeout capability, a regular `std::mutex` is sufficient. Don't default to `std::timed_mutex` just "in case."

## std::lock_guard: The Simplest RAII Wrapper

Finally, we arrive at the tools we should actually use. `std::lock_guard` is the lightest weight RAII lock guard introduced in C++11—it calls `lock()` on construction and `unlock()` on destruction. That's it. It doesn't accept `defer_lock`, has no `unlock()` method, and doesn't support movement—it has no extra capabilities, but it is precisely this minimalist design that guarantees you can't use it incorrectly.

```cpp
std::mutex mtx;
void critical_task() {
    std::lock_guard<std::mutex> lock(mtx);
    // Critical section
} // Lock released automatically
```

Note a common mistake beginners make—forgetting to name the `std::lock_guard` variable:

```cpp
// WRONG: Temporary object destroyed immediately!
std::lock_guard<std::mutex>(mtx);
```

An unnamed temporary object is destructed immediately when the statement ends—the lock is released just as soon as it's acquired, which is equivalent to not locking at all. Compilers usually don't warn about this, so remember to name your lock objects.

`std::lock_guard` has a rarely used but worth-knowing constructor option: `std::adopt_lock`. It tells `std::lock_guard`: "The lock is already held by the current thread, just manage the release on destruction, don't lock again." This option is mainly used to cooperate with the `std::lock()` function—first acquire multiple locks simultaneously via `std::lock()`, then hand them over to `std::lock_guard` for management using `std::adopt_lock`. We will see specific usage in the next post when discussing deadlock prevention.

## std::unique_lock: The Flexible but Not Heavy Swiss Army Knife

If `std::lock_guard` is a reliable screwdriver, `std::unique_lock` is a Swiss Army knife. Based on `std::lock_guard`, it adds several key capabilities: deferred locking, manual unlocking, lock ownership transfer, and cooperation with condition variables. Of course, extra capabilities mean extra state—`std::unique_lock` needs to store an "owns lock" flag internally, so the overhead is slightly higher than `std::lock_guard`, but in the vast majority of scenarios, this difference is negligible.

### Basic Usage: As Simple as lock_guard

```cpp
std::mutex mtx;
void task() {
    std::unique_lock<std::mutex> lock(mtx);
    // Critical section
}
```

The most basic usage is exactly the same as `std::lock_guard`: construct to lock, destruct to unlock.

### Deferred Locking: defer_lock

`std::defer_lock` tells `std::unique_lock` not to lock upon construction; we decide when to lock later. This is useful in "conditional locking" scenarios—not all code paths need a lock, but you want to enjoy RAII protection on the paths that do:

```cpp
std::unique_lock<std::mutex> lock(mtx, std::defer_lock);
// ... do some unlocked work ...
if (need_lock) {
    lock.lock();
    // Critical section
}
```

`std::defer_lock` is more commonly used to cooperate with `std::lock` to implement safe multi-lock acquisition—first construct two `std::unique_lock`s with `std::defer_lock`, then use `std::lock` to lock them simultaneously. This pattern will be expanded in the next post.

### Early Unlock: Reducing the Critical Section

`std::unique_lock` allows you to manually call `unlock()` before the scope ends—this is valuable when you need to shrink the critical section. The shorter the lock is held, the shorter the wait time for other threads, and the higher the concurrency:

```cpp
std::vector<int> data;
std::mutex mtx;

void process_data() {
    std::vector<int> local_copy;
    {
        std::unique_lock<std::mutex> lock(mtx);
        local_copy = data; // Copy under lock
        lock.unlock();     // Release early
    }
    // Process local_copy without holding the lock
    // ... heavy computation ...
}
```

This example demonstrates an important pattern: quickly complete necessary data copying under the protection of the lock, then immediately release the lock, and perform subsequent processing outside the lock. `std::lock_guard` cannot unlock early—its design philosophy is "lock lifecycle equals scope lifecycle," with no exceptions.

### Cooperating with Condition Variables

This is the most irreplaceable scenario for `std::unique_lock`. The `wait()` series of functions of `std::condition_variable` require passing in `std::unique_lock`, not `std::lock_guard`. The reason lies in the working mechanism of condition variables: a thread must release the lock when waiting (to allow other threads to enter the critical section and modify the condition), and re-acquire the lock when woken up. The "unlock-then-relock" capability provided by `std::unique_lock` is exactly what condition variables need.

```cpp
std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void wait_for_ready() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });
    // ...
}
```

If you try to swap the `std::unique_lock` inside `cv.wait` with `std::lock_guard`, it won't even compile—the signature of `wait` requires `std::unique_lock`.

### Lock Ownership Transfer

`std::unique_lock` supports move semantics, allowing lock ownership to be transferred between functions. This is useful in some architectural designs—for example, a function acquires a lock and does some initialization work, then transfers the lock ownership to the caller, who is responsible for subsequent critical section operations and final unlocking:

```cpp
std::unique_lock<std::mutex> acquire_and_process() {
    std::unique_lock<std::mutex> lock(mtx);
    // Init logic
    return lock; // Move ownership
}

void consumer() {
    auto lock = acquire_and_process();
    // Continue critical section
}
```

Note that `std::lock_guard` does not support movement—both its copy constructor and move constructor are deleted. If you need to transfer lock ownership, `std::unique_lock` is the only choice.

## std::scoped_lock: C++17 Multi-Lock Deadlock Prevention

`std::scoped_lock` is an RAII lock guard introduced in C++17, designed specifically for multi-lock scenarios. Its constructor can accept any number of mutexes (it also accepts a single mutex), and it uses the deadlock avoidance algorithm provided by `std::lock` to acquire all locks at once, releasing them in reverse order upon destruction.

This feature solves a very real problem. Suppose two threads need to operate on two data structures protected by different mutexes simultaneously. The most naive approach is to nest `std::lock_guard`:

```cpp
// Thread 1
{
    std::lock_guard<std::mutex> lock1(mtx1);
    std::lock_guard<std::mutex> lock2(mtx2);
    // ...
}

// Thread 2
{
    std::lock_guard<std::mutex> lock2(mtx2);
    std::lock_guard<std::mutex> lock1(mtx1);
    // ...
}
```

If Thread 1 grabs `mtx1` while Thread 2 grabs `mtx2`, both sides get stuck—the classic AB-BA deadlock. `std::scoped_lock` solves this in one line:

```cpp
// Both threads
{
    std::scoped_lock lock(mtx1, mtx2);
    // ...
}
```

The internal deadlock avoidance algorithm of `std::scoped_lock` is based on a `std::lock` backoff strategy: try to acquire all locks in a certain order; if a specific lock fails, release the acquired locks and retry in a different order. This algorithm breaks the "hold and wait" condition of the four necessary conditions for deadlock—if acquisition fails, held locks are released, eliminating the situation of "holding one while waiting for another."

`std::scoped_lock` can also be used for a single mutex, in which case it is equivalent to `std::lock_guard`. However, for code clarity, it is still recommended to use `std::lock_guard` for single-lock scenarios—seeing `std::lock_guard` tells you there is only one lock, seeing `std::scoped_lock` implies multiple locks might be involved, which is valuable information for anyone reading the code.

## lock_guard vs unique_lock vs scoped_lock: Selection Guide

Let's compare the core differences of the three RAII lock guards to help you make quick choices in actual development.

The design philosophy of `std::lock_guard` is "simplicity is beauty." It is non-copyable, non-movable, cannot unlock early, and cannot defer locking—these "limitations" are precisely its strengths, because the more restrictions, the smaller the room for error. For 90% of daily scenarios, `std::lock_guard` is sufficient: enter function, construct `std::lock_guard`, operate on shared data, function returns, `std::lock_guard` destructs and releases the lock. The whole process is a straight line with no branches.

`std::unique_lock` fits that 10% of scenarios requiring extra flexibility. The most typical is cooperating with condition variables—this is the core scenario where `std::lock_guard` is irreplaceable. Next is the "copy data first, then unlock early" pattern—moving time-consuming operations outside the lock to reduce hold time. There are also deferred locking and lock ownership transfer, which are used in more complex architectural designs.

The core value of `std::scoped_lock` is deadlock prevention for multi-lock acquisition. Whenever your code needs to hold two or more locks simultaneously, you should use `std::scoped_lock`. If the project has already adopted C++17, using `std::scoped_lock` for single-lock scenarios is also perfectly fine—but in terms of team convention, distinguishing `std::lock_guard` (single lock) and `std::scoped_lock` (multi-lock) helps code readability and maintainability.

## Engineering Principle: Never Manually Call lock()/unlock()

We spent an entire post discussing the mutex family and RAII lock guards, and the core principle to emphasize is only one: never directly call `lock()` and `unlock()` in application code. We have seen the reasons repeatedly throughout the text—managing lock/unlock manually is almost impossible to guarantee correctness in scenarios involving exception paths, multiple return paths, and nested calls, whereas RAII lock guards fundamentally eliminate this entire class of bugs by binding the lock lifecycle to the scope.

This principle is explicitly recorded in the C++ Core Guidelines as CP.20: "Use RAII, never plain `lock()`/`unlock()`." The only exception is `std::adopt_lock`—it accepts an already locked mutex and is only responsible for unlocking upon destruction. But even in this case, the locking action should be done through `std::lock()` or other safe mechanisms, not by manually calling `lock()`.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/examples/vol5/10_mutex_raii.cpp`.

## Run Online

Experience the three RAII lock guards: `lock_guard`, `unique_lock` + `condition_variable`, and `scoped_lock` online:

<OnlineCompilerDemo
  title="mutex and RAII Locks"
  source-path="code/examples/vol5/10_mutex_raii.cpp"
  description="Experience lock_guard counting, unique_lock+CV producer-consumer queue, and scoped_lock multi-lock safe swap"
  allow-run
/>

## Exercises

### Exercise 1: Implement a Thread-Safe Wrapper for stack

Given a `std::stack`, use `std::mutex` and `std::lock_guard` to implement a thread-safe wrapper for it. Requirements: provide `push`, `pop` (returns `T`, returns `std::optional` if empty), `try_pop` (also returns `std::optional`), and `size` interfaces. Hint: Note that `pop` and `try_pop` should not return references—because after unlocking, if the caller accesses the reference, it becomes invalid.

### Exercise 2: Compare Performance of lock_guard and unique_lock

Write a simple benchmark: use 4 threads to increment a shared counter 1,000,000 times each, protected by `std::lock_guard` and `std::unique_lock` respectively. Compare their runtimes—you will find the difference is usually within the noise range, but in extreme scenarios, the extra state maintenance of `std::unique_lock` might manifest as measurable overhead. Think: Under what conditions does this difference become significant?

### Exercise 3: Safely Swap Two Protected Data with scoped_lock

Assume there are two `std::vector<int>`, each protected by a `std::mutex`. Write a `swap_data` function that uses `std::scoped_lock` to acquire both locks simultaneously, then swaps the contents of the two vectors. Verify that calling this function repeatedly in a multi-threaded environment does not cause a deadlock.

## References

- [std::mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/mutex)
- [std::recursive_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/recursive_mutex)
- [std::timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/timed_mutex)
- [std::lock_guard -- cppreference](https://en.cppreference.com/w/cpp/thread/lock_guard)
- [std::unique_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/unique_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.20 -- Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
