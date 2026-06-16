---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the condition variable's wait/notify mechanism, and understand
  spurious wakeups, predicate usage, and the lost wakeup problem.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- mutex 与 RAII 锁
reading_time_minutes: 18
related:
- 读写锁与 shared_mutex
- 线程安全队列
tags:
- host
- cpp-modern
- intermediate
- mutex
- 异步编程
title: condition_variable and Wait Semantics
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/03-condition-variable.md
  source_hash: a89b45f907b2e767bbea20d33049c7d22c6db0af463c53daa985e27a20f4a703
  translated_at: '2026-06-16T04:03:41.061985+00:00'
  engine: anthropic
  token_count: 3155
---
# Condition Variables and Wait Semantics

In the previous post, we discussed mutexes and RAII locks—covering how to protect critical sections and avoid deadlocks. However, one problem remains unsolved: what if a thread needs to "wait for a condition to become true" before continuing? A mutex alone isn't enough. The most naive approach is a loop that repeatedly locks, checks the condition, unlocks, and sleeps for a short while before trying again—this is known as **busy-waiting** or **polling**. While it works, it wastes CPU cycles, and tuning the "sleep duration" is difficult: too short wastes CPU, too long results in sluggish response.

`std::condition_variable` is the standard library's answer. It provides a "wait-notify" mechanism: Thread A can **wait** on a condition variable, and Thread B can **notify** the condition variable after changing the state, waking the waiting thread. This mechanism is far more efficient than polling because the waiting thread is suspended by the OS, consuming no CPU time until it is rescheduled upon notification. However, condition variables have some subtle pitfalls—spurious wakeups, lost wakeups, and predicate usage—which are the real focus of this post.

## std::condition_variable vs. std::condition_variable_any

The C++ standard library provides two condition variable classes in the `<condition_variable>` header. `std::condition_variable` is the primary choice, designed to work exclusively with `std::unique_lock<std::mutex>`. `std::condition_variable_any` is a more general version that works with any lock type satisfying the *Lockable* requirement—such as `std::shared_mutex` or custom lock wrappers. The tradeoff is that `condition_variable_any` usually has a heavier internal implementation (potentially using extra internal mutexes or dynamic allocation), so in most scenarios, we prefer `std::condition_variable`. Unless stated otherwise, "condition variable" refers to `std::condition_variable`.

The core API is concise, consisting of three groups of operations: the `wait()` series (`wait`, `wait_for`, `wait_until`) for waiting, `notify_one` to wake a single waiting thread, and `notify_all` to wake all waiting threads. Let's break them down one by one.

## wait(): The Basic Wait

Let's look at a simple example. Suppose we have a flag `ready`, where the main thread sets it and a worker thread waits for it to become `true`:

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker() {
    std::unique_lock<std::mutex> lock(mtx);
    // Wait until ready is true
    cv.wait(lock, [&] { return ready; });
    std::cout << "Worker is running\n";
}

int main() {
    std::thread t(worker);

    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    } // Release lock before notifying
    cv.notify_one();

    t.join();
}
```

There are key details to examine here. First, `cv.wait` behavior involves three steps:

1. Atomically release the associated mutex and add the current thread to the condition variable's wait queue.
2. Suspend the thread (blocked state), consuming no CPU.
3. Upon notification (or spurious wakeup), the thread is rescheduled, reacquires the mutex, and `wait` returns.

The "atomic release and enqueue" is crucial—it ensures no gap exists between releasing the mutex and starting to wait, preventing notifications from being missed in that gap (discussed in detail later).

Second, after `wait` returns, the current thread **holds the mutex again**. This means the caller can safely access shared state protected by the mutex immediately after `wait` returns, without additional locking. This is why `wait` requires a `unique_lock`—the lock's ownership is transferred out and back in during `wait`, managing the lifecycle automatically.

However, the code above has a serious flaw. Did you spot it? The worker thread continues execution immediately after `wait` returns, but it **never checks the value of `ready`**. If this was a spurious wakeup? If the notification was sent before the worker called `wait`? The behavior becomes unpredictable. This leads us to the two core issues discussed next.

## Spurious Wakeups: Why wait Must Use a Predicate

A **spurious wakeup** occurs when a thread returns from `wait` without receiving a `notify_one` or `notify_all` call. This isn't a bug or a quality issue—both POSIX and C++ standards explicitly allow this. Why? It lies in the underlying implementation.

On Linux, `condition_variable` is implemented using the `futex` (Fast Userspace muTEX) system call. Internal state is usually tracked by an atomic counter. To efficiently implement `wait` and `notify`, a "scatter-gather" strategy is used: `notify_one` only increments the counter and wakes one futex, while `wait` must atomically decrement the counter and for pending notifications. Under certain boundary conditions—like a `notify_all` waking a batch of threads that haven't rechecked internal state yet—the kernel might wake extra threads. The POSIX standards committee, weighing implementation efficiency against semantic strictness, chose to allow spurious wakeups. This allows condition variables to be implemented with lighter kernel primitives without requiring precise one-to-one mapping for every notification.

The consequence is: if you write `wait` and it returns, you **cannot assume** someone called `notify`. You must recheck the condition after `wait` returns. The standard practice is to wrap `wait` in a `while` loop:

```cpp
std::unique_lock<std::mutex> lock(mtx);
while (!ready) {
    cv.wait(lock);
}
```

The logic is: check the condition; if not met, `wait`. When `wait` returns, check again, looping until the condition holds. This renders spurious wakeups harmless—even if woken spuriously, the loop rechecks `ready`, finds it `false`, and waits again.

The C++ standard library encapsulates this pattern in a convenient overload: **`wait` with a predicate**:

```cpp
cv.wait(lock, [&] { return ready; });
```

The semantics of `wait(lock, pred)` are equivalent to `while (!pred()) wait(lock);`, but it may be more efficient than a manual loop—implementations can use optimized wait strategies on certain platforms (like `futex`'s bit-aware features on Linux). In short: **always use the predicate version of `wait`, never the version without one**. This isn't advice; it's a rule.

Looking back at our example, the correct way is:

```cpp
cv.wait(lock, [&] { return ready; });
```

## Lost Wakeups: The "Notify Before Wait" Disaster

Spurious wakeups are "waking without a notify," while **lost wakeups** are the opposite—"notified but no one received it." This happens when the notification is sent before `wait`.

Let's construct a lost wakeup scenario:

```cpp
// Main thread
{
    std::lock_guard<std::mutex> lock(mtx);
    ready = true;
}
cv.notify_one();

// Worker thread (starts slightly later)
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, [&] { return ready; }); // Returns immediately because ready is true
```

In this example, the main thread calls `notify_one` before the worker calls `wait`. If we used the raw `wait` without a predicate, the notification would be lost forever—the condition variable doesn't "store" notifications. However, because we used the predicate version, the worker thread checks `ready` upon waking (which is now `true`) and proceeds without needing a notification. This is another huge advantage of the predicate `wait`: it guards against both spurious and lost wakeups.

More fundamentally, the strategy to prevent lost wakeups is ensuring "check-wait" and "modify-notify" use the **same mutex** for protection. When the waiting thread holds the mutex to check the condition, the notifying thread cannot modify it simultaneously; conversely, when the notifying thread holds the mutex to modify the condition, the waiting thread cannot have passed the check but not yet started `wait`. This is why `wait` requires a `unique_lock`—it's not just for releasing the lock during the wait, but for ensuring synchronization between waiting and notification.

## wait_for() and wait_until(): Timed Waits

Sometimes we don't want to wait indefinitely—like a network request timeout, a user cancellation, or a periodic state check. `wait_for` and `wait_until` provide waiting semantics with timeouts.

`wait_for` waits for a specific duration. `wait_until` waits until a specific time point. Both support predicate and non-predicate versions (prefer the predicate version). The predicate version returns `bool`, indicating if the predicate is `true` (notified or timeout, but only returns `true` if the predicate is satisfied). The non-predicate version returns `cv_status`, which can be `no_timeout` (notified or spurious wakeup) or `timeout` (timed out).

Here's a practical example: waiting for a task to complete, but for a maximum of 5 seconds:

```cpp
std::mutex mtx;
std::condition_variable cv;
bool task_done = false;

void worker() {
    // Simulate work
    std::this_thread::sleep_for(std::chrono::seconds(3));
    {
        std::lock_guard<std::mutex> lock(mtx);
        task_done = true;
    }
    cv.notify_one();
}

int main() {
    std::thread t(worker);

    std::unique_lock<std::mutex> lock(mtx);
    if (cv.wait_for(lock, std::chrono::seconds(5), [&] { return task_done; })) {
        std::cout << "Task completed\n";
    } else {
        std::cout << "Timeout waiting for task\n";
    }

    t.join();
}
```

The predicate version of `wait_for` is essentially a loop internally: every time it wakes (notification or spurious), it checks the predicate. If `true`, it returns `true`; if it times out and the predicate is still `false`, it returns `false`. Note that returning `false` doesn't mean a notification will never arrive—just that the condition wasn't met within the specified time. Handling logic after a timeout depends on your business requirements.

`wait_until` is similar but accepts an absolute time point (a template parameter of `Clock` and `Duration` in `std::chrono`), rather than a relative duration. This is more convenient for "complete before a deadline" scenarios—you don't calculate `duration`, just pass a deadline. Note that system clock adjustments can affect `wait_until` accuracy, so if you care about monotonicity, prefer `wait_for` with a steady clock.

## Producer-Consumer Pattern: Bounded Queue

The classic use case for condition variables is the Producer-Consumer Pattern. Let's implement a complete bounded blocking queue—producers push data, blocking if full; consumers pop data, blocking if empty. This example combines mutexes, condition variables, and predicates.

First, define the basic queue structure:

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

    void push(T value) {
        std::unique_lock<std::mutex> lock(mtx_);
        // Wait until there is space
        not_full_.wait(lock, [&] { return queue_.size() < capacity_; });

        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        // Wait until queue is not empty
        not_empty_.wait(lock, [&] { return !queue_.empty(); });

        T value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return value;
    }

private:
    std::queue<T> queue_;
    size_t capacity_;
    std::mutex mtx_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};
```

Let's break this down. The queue maintains two condition variables: `not_full_` for producers (wait if full, notify when consumed), and `not_empty_` for consumers (wait if empty, notify when produced). This dual-condition design is more precise than a single variable—it avoids unnecessary wakeups: producers only wake consumers (`notify_one` on `not_empty_`), consumers only wake producers (`notify_one` on `not_full_`).

The `push` logic: acquire mutex, wait with predicate until not full. When `wait` returns, we guarantee `size < capacity_` (predicate is `true`), so we can safely push. After pushing, call `notify_one` on `not_empty_` to wake a waiting consumer. The `pop` logic is symmetric: wait until not empty, take element, notify producer.

Note that in `push` and `pop`, we call `notify` while holding the lock. This is fine and sometimes an optimization. `notify` doesn't wait for a response; it just moves threads from the condition variable queue to the mutex queue. The awakened thread can only acquire the lock and proceed after the current thread releases it (via `unique_lock` destructor). So holding the lock during `notify` makes no difference to correctness, but on some platforms, it reduces an unnecessary context switch.

Now, let's use this queue:

```cpp
int main() {
    BoundedQueue<int> queue(10);

    std::thread producer([&] {
        for (int i = 0; i < 20; ++i) {
            queue.push(i);
            std::cout << "Produced: " << i << "\n";
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < 20; ++i) {
            auto val = queue.pop();
            if (val) {
                std::cout << "Consumed: " << *val << "\n";
            }
        }
    });

    producer.join();
    consumer.join();
}
```

Capacity is 10, producing 20 elements, so it will fill up—the producer blocks at the 11th element until the consumer makes space. The consumer's pace depends on the producer—if the producer lags, the consumer waits in `pop`. The two threads coordinate their pace via the condition variables.

## Choosing Between notify_all and notify_one

In the bounded queue example, we used `notify_one`—waking only one waiting thread. However, some scenarios require `notify_all` to wake everyone. The choice depends on the "nature of the condition change."

`notify_one` fits scenarios where "one notification lets one thread continue." The producer-consumer queue is typical—each `push` only needs to wake one consumer to take one item; waking multiple is pointless (only one item available, others go back to sleep). `notify_one` reduces unnecessary wakeups and context switches.

`notify_all` fits scenarios where "a condition change might satisfy multiple waiting threads simultaneously." A classic example is **thread pool shutdown**: when you set a `stop` flag and call `notify_all`, all threads waiting for tasks need to wake up, check the flag, and exit. Another is the **barrier pattern**—all threads must wait for a condition, then proceed together, requiring notifying everyone.

A common misconception is that `notify_all` is always safe. While `notify_all` is no less "correct" than `notify_one`—all threads eventually wake and check—the performance difference is significant. If 10 threads are waiting, `notify_all` wakes all 10. They compete for the same mutex, but only one passes the check; the other 9 wasted their time. So "use `notify_one` unless you must" is a valid optimization principle—provided you know the notification relates to only one waiter.

## std::condition_variable_any: The Generic Condition Variable

So far, we've used `std::condition_variable`, which only accepts `std::unique_lock<std::mutex>`. Sometimes we need other lock types—like `std::shared_mutex` (detailed in the next post). This is where `std::condition_variable_any` comes in.

Its interface is identical to `condition_variable`, but the templated `wait` accepts any lock satisfying the *Lockable* requirement. Usage is straightforward—just swap `condition_variable` for `condition_variable_any`. The cost? Its internal implementation usually needs an extra mutex to protect the internal wait queue (because `condition_variable` can leverage `std::mutex` internals for optimization, while `condition_variable_any` doesn't know the external lock's internals). Thus, it performs slightly worse. If you only need `std::mutex`, stick with `std::condition_variable`.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), specifically in `demo/condition_variable`.

## Exercises

### Exercise 1: Thread-Safe Countdown Latch

Implement a `CountdownLatch` class, similar to C#'s `CountdownEvent` or Java's `CountDownLatch`. It has an internal counter initialized to N. Threads can call `wait` to block until the counter reaches zero, while other threads call `count_down` to decrement it. When the counter hits zero, all waiting threads should wake.

Requirements:

- Use `std::condition_variable` and `std::mutex`.
- `wait` must use the predicate version.
- In `count_down`, consider whether to use `notify_one` or `notify_all`.

Hint: When the counter transitions from 1 to 0, all threads blocked on `wait` satisfy their condition simultaneously—this is a classic `notify_all` scenario.

### Exercise 2: Extend Bounded Queue with try_pop_for

Extend the `BoundedQueue` from this article by adding a `try_pop_for` method: try to pop an element within a specified time. If successful, return `std::optional<T>` containing the value; if timed out, return `std::nullopt`.

Hint: Use the predicate version of `wait_for` and check the return value to determine success or timeout. Note that after a timeout, the thread is safe—because `try_pop_for`'s return value explicitly tells the caller "nothing was popped," allowing the caller to decide whether to retry or abort.

### Exercise 3: Reproduce Lost Wakeup

Write a program that intentionally forces a "notify before wait" sequence. Use the raw `wait` (no predicate) and observe if the program hangs permanently (likely, depending on scheduling). Then, add the predicate to `wait` and confirm that even if the notification comes first, the program exits normally. The goal is to experience the danger of lost wakeups firsthand and understand why the predicate `wait` is mandatory.

## References

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable::wait -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [Condition variable -- Wikipedia (Spurious wakeup POSIX discussion)](https://en.wikipedia.org/wiki/Monitor_(synchronization)#Condition_variables)
- [Why do spurious wakeups happen? -- StackOverflow](https://stackoverflow.com/questions/8594591/why-does-pthreads-cond-wait-have-spurious-wakeups)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
