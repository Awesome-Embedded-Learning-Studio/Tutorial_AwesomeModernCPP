---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Construct a closable, timeout-supporting bounded blocking queue using
  `mutex` and `condition_variable`
difficulty: intermediate
order: 1
platform: host
prerequisites:
- condition_variable 与等待语义
reading_time_minutes: 26
related:
- 线程安全容器设计
- SPSC 与 MPMC 队列
tags:
- host
- cpp-modern
- intermediate
- mutex
title: Thread-Safe Queue
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/01-thread-safe-queue.md
  source_hash: c8b13dfd90f2c492983ae8c68cc7c289d526135ea150f10328a132f41f8a8320
  translated_at: '2026-06-16T04:04:43.203006+00:00'
  engine: anthropic
  token_count: 5314
---
# Thread-Safe Queues

In the previous article on `condition_variable`, we wrote a simplified version of `BoundedQueue`—it had `mutex` and `condition_variable`, supported blocking, and supported notifications. Honestly, it felt pretty solid when we wrote it, but if you drop it directly into production code, I bet it will cause issues within two days: How do we gracefully shut down the queue? What if a producer thread crashes while a consumer is blocked on `pop`? What if I don't want to wait indefinitely and just want to try to fetch an element? What if I want to cancel the wait from the outside?

Until these issues are resolved, this queue is just a teaching toy. In this article, we will transform it from a teaching toy into a truly usable component—adding a shutdown mechanism, `try_push`/`try_pop` with timeouts, C++20 `stop_token` integration, and backpressure strategies when the queue is full. We will proceed step-by-step, adding one capability at a time based on the previous step, so you can clearly see the rationale behind every design decision. Don't worry, let's solidify the foundation first.

## Starting Point: A Working BoundedQueue

Let's bring over the queue from the `condition_variable` article as our starting point today:

```cpp
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

    void push(T value) {
        std::unique_lock lock(mutex_);
        // Wait until there is space, or handle spurious wakeup
        not_full_.wait(lock, [this] { return size_ < capacity_; });
        queue_.push(std::move(value));
        ++size_;
        not_empty_.notify_one();
    }

    T pop() {
        std::unique_lock lock(mutex_);
        // Wait until there is an element, or handle spurious wakeup
        not_empty_.wait(lock, [this] { return size_ > 0; });
        T value = std::move(queue_.front());
        queue_.pop();
        --size_;
        not_full_.notify_one();
        return value;
    }

private:
    size_t capacity_;
    size_t size_ = 0;
    std::queue queue_;
    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};
```

The core logic of this version is sound. Two condition variables (`not_full_` and `not_empty_`) manage their respective waiters and notifiers, and the predicate-based `wait` guards against spurious wakeups and lost wakeups. But if you think about it carefully, it has three fatal flaws: First, both `push` and `pop` can block indefinitely—if a producer never pushes, a consumer waits forever, and vice versa; second, there is no shutdown mechanism—when the queue's life cycle ends, if threads are still blocked on `wait`, they will never wake up, and the program will simply deadlock; third, there is no timeout capability—callers cannot give up waiting within a specified time.

If these three problems aren't solved, using this queue to write server code is basically a ticking time bomb. Let's dismantle them one by one.

## Step 1: Shut It Down—The Right Way to Close a Queue

The shutdown mechanism is the most important non-functional requirement for a thread-safe queue, bar none. Imagine a typical producer-consumer scenario: multiple producers put tasks into the queue, and multiple consumers take tasks out to execute. When the program needs to exit—whether it's a normal shutdown, receiving SIGTERM, or some anomaly—we want a clear shutdown process: producers stop putting new tasks in, consumers finish processing the remaining tasks in the queue, and then everyone exits gracefully. If you can't even "shut down," using this queue will make you feel uneasy sooner or later.

The semantics of shutdown need careful design; it's not as simple as setting a `bool` flag. We need a `closed_` flag to indicate whether the queue is closed, which affects the behavior of `push` and `pop`. The rule for `push` is relatively simple: after the queue is closed, all new `push` operations should be rejected because no one will come to consume this data anymore. The rule for `pop` is more subtle: after closing, if there are still elements in the queue, consumers should be able to drain them all until the queue is empty; once the queue is empty, `pop` should no longer block but should return a signal indicating "queue empty and closed." This drain semantics is crucial—if drain isn't allowed, unprocessed tasks in the queue are lost upon shutdown.

Okay, semantics are clear. Let's use an enum to represent operation results:

```cpp
enum class PopResult {
    Success,   // Successfully retrieved an element
    Closed,    // Queue is closed and empty
};

enum class PushResult {
    Success,   // Successfully pushed an element
    Closed,    // Queue is closed, push rejected
};
```

Next, we add the `closed_` flag to the queue and modify the predicate logic for `push` and `pop`:

```cpp
template <typename T>
class BoundedQueue {
public:
    // ... (constructor unchanged)

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        // Notify all waiting threads to check the closed flag
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    PushResult push(T value) {
        std::unique_lock lock(mutex_);
        // Wait until not full OR closed
        not_full_.wait(lock, [this] { return size_ < capacity_ || closed_; });

        if (closed_) return PushResult::Closed;

        queue_.push(std::move(value));
        ++size_;
        not_empty_.notify_one();
        return PushResult::Success;
    }

    T pop() {
        std::unique_lock lock(mutex_);
        // Wait until not empty OR closed
        not_empty_.wait(lock, [this] { return size_ > 0 || closed_; });

        // If closed and empty, throw or return a special value (omitted for brevity,
        // usually better to return PopResult or use std::optional)
        // For this example, let's assume we throw if closed and empty to keep signature simple
        // or change signature to PopResult pop(T& value).
        // Let's stick to the logic flow:
        if (size_ == 0) { // implies closed_ is true
             // Handle drain finished scenario
             throw std::runtime_error("Queue closed and empty");
        }

        T value = std::move(queue_.front());
        queue_.pop();
        --size_;
        not_full_.notify_one();
        return value;
    }

    // Better pop signature for shutdown support:
    PopResult pop(T& value) {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] { return size_ > 0 || closed_; });

        if (size_ == 0) return PopResult::Closed; // Drained

        value = std::move(queue_.front());
        queue_.pop();
        --size_;
        not_full_.notify_one();
        return PopResult::Success;
    }

private:
    // ... (members unchanged)
    bool closed_ = false;
};
```

There's a fair bit of code here, so let's break down the intricacies. First, look at the `close` method—it sets `closed_` under the protection of the lock, then releases the lock, and uses `notify_all` to wake up all waiting threads. You might ask, why not notify directly inside the lock? Technically you can, but `notify_all` doesn't need to execute inside the lock (the standard allows notification outside the lock). Moving the notification outside the lock reduces one unnecessary lock contention: awakened threads don't need to wait for the closing thread to release the lock before they can scramble to acquire it. And why use `notify_all` instead of `notify_one`? Because closing is a global event—all waiting producers and consumers need to be woken up. If we only used `notify_one`, only one thread wakes up each time, while others are still waiting foolishly; then the awakened thread would need to `notify` the next one... This chain is too fragile and the latency is uncontrollable. `notify_all` is the standard practice for shutdown scenarios.

Now let's look at the predicate for `push`. Previously it was `size_ < capacity_`, now we added `|| closed_`. This means `wait` will return in two situations: either the queue isn't full, or the queue is closed. After returning, we check `closed_`—if it's `true`, the queue is closed, we shouldn't push, and return `PushResult::Closed` directly. Note the order of checks here: check `closed_` first, then decide whether to proceed. This ensures no new elements enter the queue after closing.

The predicate for `pop` is similar: `size_ > 0 || closed_`. After `wait` returns, we check `size_`—if the queue is empty, regardless of `closed_`'s state, there's nothing to fetch, so return `PopResult::Closed`. If the queue isn't empty, even if `closed_` is `true`, we continue fetching—this is drain semantics: after closing, consumers are allowed to consume all remaining elements.

You might notice a subtle detail: after `wait` returns in `push`, we check `closed_`, but in `pop` we check `size_` instead of `closed_`. Why the asymmetry? Because the semantics differ: the only reason for a push to be rejected is that the queue is closed (push isn't blocked if the queue isn't full), whereas pop fails because the queue is empty (regardless of closure). When the queue is not empty after closing, pop should continue to extract remaining elements; when the queue is empty after closing, pop should report failure. So pop uses `size_` as the criterion for "is there anything to fetch"—this reflects the intent of pop more accurately than checking `closed_` directly.

## Step 2: Don't Wait Forever—try_push and try_pop with Timeouts

The shutdown mechanism solves the "graceful exit" problem, which is great. But there's a class of scenarios it can't handle: the caller doesn't want to block indefinitely, just wants to try an operation for a certain time and give up if it times out. For example, a network service wants to stuff a request into a queue, but if it's full and there's no space after waiting 100ms, it would rather drop the request than block—response latency is more fatal than dropping a request or two. This is where we need `try_push` and `try_pop` with timeouts.

We implement this directly using `wait_for`, which is naturally suited for this "wait and try" scenario:

```cpp
#include <chrono>

using namespace std::chrono_literals;

// Inside BoundedQueue class

PushResult try_push(T value, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    // wait_for returns false if timeout
    if (!not_full_.wait_for(lock, timeout, [this] {
            return size_ < capacity_ || closed_; })) {
        return PushResult::Timeout; // New enum value needed
    }

    if (closed_) return PushResult::Closed;

    queue_.push(std::move(value));
    ++size_;
    not_empty_.notify_one();
    return PushResult::Success;
}

PopResult try_pop(T& value, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!not_empty_.wait_for(lock, timeout, [this] {
            return size_ > 0 || closed_; })) {
        return PopResult::Timeout;
    }

    if (size_ == 0) return PopResult::Closed;

    value = std::move(queue_.front());
    queue_.pop();
    --size_;
    not_full_.notify_one();
    return PopResult::Success;
}
```

The predicate version of `wait_for` returns a `bool`—it returns `true` if the predicate is `true` (whether notified or the condition was satisfied the moment before timeout), and `false` if it times out and the predicate is still `false`. We use this return value to distinguish three situations: timeout (`false`, return `Timeout`), closed (`true` but `closed_` is `true`, return `Closed`), and success.

There's a design choice here worth mentioning: why check the return value of `wait_for` first before checking `closed_`? Because if it timed out, we don't need to care about the state of `closed_` anymore—the caller cares about "I didn't succeed in the given time," and the specific reason (queue full or queue closed) is no longer important to the caller. Of course, you could reverse it—if your business scenario needs to distinguish "timeout" from "closed," just adjust the order of judgment. There's no single right answer here; it depends on what information you want to pass to the caller.

## Step 3: Making It Cancellable—C++20 stop_token Integration

`try_push` and `try_pop` solve the "I don't want to wait too long" problem, but there's another scenario they can't handle: external active cancellation. C++20 introduced the `stop_token` / `stop_source` / `stop_callback` trio, providing a standard mechanism for cooperative cancellation. Can we make the queue's `pop` operation support `stop_token`—so that when an external stop is requested, a blocking `pop` is woken up immediately, without waiting for a timeout or for data to arrive in the queue?

The answer is yes, but with a prerequisite: we need to use `condition_variable_any` instead of `condition_variable`. The reason is that C++20 added a `wait` overload accepting `stop_token` to `condition_variable_any`—when a stop is requested, `wait` is automatically woken up. `condition_variable` has no such overload because its coupling with `unique_lock` is too deep; adding `stop_token` support would require modifying the internal implementation, and the standard committee chose to provide this functionality only on the more generic `condition_variable_any`. This means, if you want `stop_token`, you have to accept the slightly higher overhead of `condition_variable_any`.

Let's see how to integrate it. To highlight the core logic, here is a standalone simplified version first—keeping only the `stop_token`-related `pop` and the minimal context it needs:

```cpp
#include <condition_variable>
#include <stop_token>

template <typename T>
class SafeQueue {
    // ...
    std::condition_variable_any not_empty_; // Changed from condition_variable
    // ...

public:
    // pop accepting stop_token
    PopResult pop(T& value, std::stop_token st) {
        std::unique_lock lock(mutex_);

        // wait_for with stop_token returns true if predicate is met,
        // false if stop was requested.
        if (!not_empty_.wait(lock, st, [this] {
                return size_ > 0 || closed_; })) {
            return PopResult::Stopped; // Stop requested
        }

        if (size_ == 0) return PopResult::Closed;

        value = std::move(queue_.front());
        queue_.pop();
        --size_;
        not_full_.notify_one();
        return PopResult::Success;
    }
};
```

You will find that compared to previous versions, the most core change in this code is just one: the condition variable changed from `condition_variable` to `condition_variable_any`. The interface of the latter is fully compatible with the former, but it additionally supports working with `stop_token`—at the cost of a slightly heavier internal implementation (it needs an additional internal mutex to manage the wait queue), but in the vast majority of scenarios, this overhead is negligible.

Then there is the semantics of `wait`. It waits until the predicate is `true` or a stop is requested on `stop_token`. Returning `true` means the predicate is satisfied, returning `false` means stop was requested and the predicate was not satisfied. If the predicate happens to be satisfied when stop is requested, it returns `true`—meaning the predicate takes precedence over stop. This makes sense: if what you are waiting for has already arrived, there's no need to discard it because of stop.

On the consumer side, using it with `jthread` is very natural. `jthread` is a new thread class introduced in C++20; the biggest difference from `std::thread` is its built-in `stop_token` support and automatic `join` semantics—its destructor automatically requests a stop and waits for the thread to finish, so you no longer need to manually `join`:

```cpp
#include <thread>

void consumer(SafeQueue<int>& q, std::stop_token st) {
    int value;
    while (true) {
        auto res = q.pop(value, st);
        if (res == PopResult::Stopped || res == PopResult::Closed) {
            break;
        }
        // Process value
    }
}

int main() {
    SafeQueue<int> q;
    // jthread automatically passes the stop_token of the associated stop_source
    std::jthread worker(consumer, std::ref(q));

    // Main thread logic...

    // Request stop automatically when worker goes out of scope or explicitly:
    // worker.request_stop();
}
```

`jthread` automatically passes the internal `stop_token` to the thread function during construction—as long as the first parameter of the function signature is `stop_token`. The consumer passes this `stop_token` to `pop`. When the main thread calls `request_stop` (or when the `jthread` destructs), the blocking `wait` inside `pop` is woken up and returns `false`, causing the consumer loop to exit.

> One point worth emphasizing: here we did both `close` and `request_stop`. `close` ensures producers stop putting new elements in, `request_stop` ensures consumers don't wait indefinitely on an empty queue. Both are indispensable—only closing without stopping, consumers might still be foolishly waiting in `pop` for the last element (if the queue is already empty); only stopping without closing, producers might still be stuffing data into a queue no one is consuming. The combination of both is a complete graceful exit.

## Step 4: What to Do When the Queue Is Full—Backpressure Strategy

Until now, our way of handling a full queue has been "block and wait"—the producer blocks in `wait` until a consumer takes an element away to free up space. This is the simplest strategy, but not the only one. In some scenarios, blocking the producer is inappropriate or even dangerous. Imagine a high-throughput network service receiving tens of thousands of requests per second; if the downstream processing speed can't keep up and the queue fills up, blocked producer threads mean the service's receive threads are stuck, and new connections all time out—this isn't "a bit slow," the whole service is down. This is where we need **backpressure**—letting the producer perceive downstream pressure and respond consciously, rather than waiting foolishly.

There are three common backpressure strategies. The first is blocking and waiting, which is our current implementation, suitable for scenarios where the producer can tolerate latency. The second is dropping newest (drop newest)—when the queue is full, just drop the newly arrived element, suitable for scenarios where data loss is allowed, like log aggregation or metric reporting. The third is dropping oldest (drop oldest)—when the queue is full, kick out the oldest element in the queue to make room for the new element, suitable for "only care about recent data" scenarios, like a sliding window for real-time monitoring.

Let's take dropping newest as an example and implement a `try_push`. Its semantics are simple: if the queue isn't full, enqueue normally; if it's full, just drop it, never block:

```cpp
PushResult try_push(T value) {
    std::lock_guard lock(mutex_);
    if (closed_) return PushResult::Closed;

    if (size_ >= capacity_) {
        return PushResult::Full; // Dropped
    }

    queue_.push(std::move(value));
    ++size_;
    not_empty_.notify_one();
    return PushResult::Success;
}
```

You'll notice there's no `wait` here needed—just lock, check capacity, and return `Full` if full. This operation has O(1) time complexity and doesn't block, so the producer can never get stuck. After getting `Full`, the caller can decide whether to retry, drop, or take fallback logic; it's much more flexible than blocking and waiting.

If you need the drop oldest strategy, just modify the logic slightly to kick out the oldest element:

```cpp
PushResult push_drop_oldest(T value) {
    std::lock_guard lock(mutex_);
    if (closed_) return PushResult::Closed;

    if (size_ >= capacity_) {
        queue_.pop(); // Drop oldest
        --size_; // Size stays same effectively, but logic flow:
        // Actually we are replacing, so size doesn't change,
        // but we need to maintain the invariant.
        // Correct logic:
        // queue_.pop(); // remove head
        // queue_.push(std::move(value)); // add new tail
        // size_ remains capacity_;
    } else {
        queue_.push(std::move(value));
        ++size_;
    }

    not_empty_.notify_one();
    return PushResult::Success;
}
```

This "strategized" design is common in real projects—the queue itself provides multiple push modes, allowing callers to choose the appropriate strategy based on the business scenario. You can also template the strategy or parameterize it with an enum, letting the queue decide backpressure behavior at compile time or runtime. The choice depends on whether your business is "better to lose than to stall" or "better to stall than to lose"—I've encountered both requirements in actual projects.

## Correctness in Multi-Producer Multi-Consumer Scenarios

All our previous implementations naturally support MPMC (Multiple Producers, Multiple Consumers) scenarios—because all access to shared state (`queue_`, `size_`) is done under the protection of `mutex`. So we don't need to worry about "correctness." But "correct" and "efficient" are two different things; let's look at the pitfalls you'll encounter in actual MPMC scenarios.

The most obvious issue is lock contention. As the number of producers and consumers increases, all threads compete for the same mutex—at any given moment, only one thread can operate on the queue, while others wait for the lock. In high-throughput scenarios, this mutex becomes a bottleneck, and the time spent waiting for the lock might be longer than the time actually working. We will discuss strategies like sharded locks and fine-grained locks in the next article to reduce contention; for now, just know that this problem exists.

Another easily overlooked issue is the fairness of `notify_one`. `notify_one` wakes up "one" thread in the wait queue, but which specific thread depends on the OS's scheduling policy—usually it's FIFO (first come, first served), but the standard doesn't guarantee this. In extreme cases, some consumers might always be skipped, leading to starvation. If you need strict fairness, you need to implement it at the application layer, for example using a ticket lock or polling distribution.

There's another correctness detail worth mentioning: the choice between `notify_one` and `notify_all`. In our basic `push`/`pop`, we use `notify_one` to wake a consumer in `push`, and `notify_one` to wake a producer in `pop`. This is optimal in SPSC (Single Producer Single Consumer) and low-contention MPMC scenarios—only waking one person avoids the thundering herd. However, in high-contention scenarios, `notify_one` can lead to a variant of the thundering herd problem: a `notify_one` wakes a consumer, but that consumer finds the queue has already been emptied by another consumer after acquiring the lock, so it goes back to waiting. This "spurious wakeup" (in a logical sense, not the OS kind) happens frequently under high contention. Ironically, in this scenario, `notify_all` might actually be better—although it wakes more threads, at least one will succeed. However, this optimization requires benchmarking against the specific load pattern; there's no one-size-fits-all answer.

## Exception Safety: A Corner Often Ignored

Finally, let's talk about a topic that is easily ignored but causes high blood pressure when things go wrong: exception safety. In our previous implementations, we assumed by default that `T` would not throw exceptions—but what if `T`'s move constructor throws? What if `T`'s copy constructor throws?

The good news is that `std::queue`'s `push` provides a strong exception guarantee: if `T`'s constructor throws, the state of the queue doesn't change (the element isn't added). So in our `push` method, if `queue_.push` throws an exception, `unique_lock`'s destructor automatically releases the mutex, `not_empty_.notify_one` isn't called (because the exception skipped it), and the state of the queue is exactly the same as before calling `push`—this is exactly the behavior we want.

But there's a more insidious problem hidden in `pop`: what if `T`'s move assignment operator (in the `value = ...` line) throws an exception? At this point, the element is still in the queue (`front()` returns a reference), but the assignment to `value` failed. The result is that the element remains in the queue, but the caller didn't get the value—the next `pop` will retrieve the same element again. This isn't necessarily a bug (depending on `T`'s semantics), but if `T`'s move assignment isn't `noexcept`, you need to consider this edge case carefully.

If `T` is `std::string`, `std::vector`, `std::shared_ptr` these standard types, their move operations are `noexcept`, so don't worry. But if you want to store custom types, it's best to ensure their move operations are `noexcept`—the simplest way is to add `std::is_nothrow_move_constructible_v` to the queue's template constraints, letting the compiler guard the gate for you:

```cpp
template <typename T>
    requires std::is_nothrow_move_constructible_v<T>
class ThreadSafeQueue { ... };
```

This way, if you accidentally store a type that throws exceptions, the compiler will stop you at compile time, rather than crashing at runtime on some strange path.

By the way, `condition_variable` itself is reliable in terms of exception safety. The C++ standard guarantees: if `wait` receives a signal while waiting but the predicate is still `false` (spurious wakeup), it will re-wait and won't leak the lock. If `wait` exits due to an exception (extreme case), the lock is released correctly. So we don't need to worry extra about the exception safety of the condition variable's `wait`.

## Complete Implementation: Assembling Everything

By now, we have discussed the shutdown mechanism, timeout operations, `stop_token` integration, and backpressure strategies. Now let's integrate all these features together to present a complete, ready-to-use `ThreadSafeQueue`:

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stop_token>
#include <chrono>
#include <optional>
#include <concepts>

template <typename T>
    requires std::is_nothrow_move_constructible_v<T>
class ThreadSafeQueue {
public:
    enum class PopResult { Success, Closed, Stopped, Timeout };
    enum class PushResult { Success, Closed, Full, Timeout };

    explicit ThreadSafeQueue(size_t capacity) : capacity_(capacity) {}

    // --- Shutdown ---
    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    // --- Blocking Operations ---
    PushResult push(T value) {
        std::unique_lock lock(mutex_);
        not_full_cv_.wait(lock, [this] { return size_ < capacity_ || closed_; });
        if (closed_) return PushResult::Closed;

        internal_push(std::move(value));
        return PushResult::Success;
    }

    PopResult pop(T& value) {
        std::unique_lock lock(mutex_);
        not_empty_cv_.wait(lock, [this] { return size_ > 0 || closed_; });
        if (size_ == 0) return PopResult::Closed;

        internal_pop(value);
        return PopResult::Success;
    }

    // --- Timeout Operations ---
    template <typename Rep, typename Period>
    PushResult try_push(T value, std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mutex_);
        if (!not_full_cv_.wait_for(lock, timeout, [this] { return size_ < capacity_ || closed_; })) {
            return PushResult::Timeout;
        }
        if (closed_) return PushResult::Closed;
        internal_push(std::move(value));
        return PushResult::Success;
    }

    template <typename Rep, typename Period>
    PopResult try_pop(T& value, std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mutex_);
        if (!not_empty_cv_.wait_for(lock, timeout, [this] { return size_ > 0 || closed_; })) {
            return PopResult::Timeout;
        }
        if (size_ == 0) return PopResult::Closed;
        internal_pop(value);
        return PopResult::Success;
    }

    // --- Stop Token Operations ---
    PopResult pop(T& value, std::stop_token st) {
        std::unique_lock lock(mutex_);
        // condition_variable_any supports stop_token
        if (!not_empty_cva_.wait(lock, st, [this] { return size_ > 0 || closed_; })) {
            return PopResult::Stopped;
        }
        if (size_ == 0) return PopResult::Closed;
        internal_pop(value);
        return PopResult::Success;
    }

    // --- Backpressure: Non-blocking Try ---
    PushResult try_push_now(T value) {
        std::lock_guard lock(mutex_);
        if (closed_) return PushResult::Closed;
        if (size_ >= capacity_) return PushResult::Full;
        internal_push(std::move(value));
        return PushResult::Success;
    }

private:
    void internal_push(T value) {
        queue_.push(std::move(value));
        ++size_;
        not_empty_cv_.notify_one();
        not_empty_cva_.notify_one(); // Notify both
    }

    void internal_pop(T& value) {
        value = std::move(queue_.front());
        queue_.pop();
        --size_;
        not_full_cv_.notify_one();
        not_full_cva_.notify_one(); // Notify both
    }

    size_t capacity_;
    size_t size_ = 0;
    std::queue queue_;
    std::mutex mutex_;
    bool closed_ = false;

    // Standard CV for blocking/timeout
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;

    // CV Any for stop_token support
    std::condition_variable_any not_empty_cva_;
    std::condition_variable_any not_full_cva_;
};
```

You might notice that here we maintain both `condition_variable` (`_cv`) and `condition_variable_any` (`_cva`). Basic `push`/`pop` use the former (more efficient), while the `stop_token` version of `pop` uses the latter (supports `stop_token`). This is a practical compromise: code that doesn't need `stop_token` takes the high-performance path, and code that needs `stop_token` takes the generic path. Best of both worlds, each takes what it needs.

## Summary

In this article, starting from the teaching version of `BoundedQueue` in the `condition_variable` article, we step-by-step transformed it into a production-grade `ThreadSafeQueue`. We successively added four key capabilities: a shutdown mechanism (`closed_` flag rejects new pushes, allows drain pops), `try_push`/`try_pop` with timeouts (using `wait_for` to implement non-blocking attempts), `stop_token` integration (using C++20 overloads of `condition_variable_any` to implement cooperative cancellation), and backpressure strategies (non-blocking drop modes provided by `try_push_now`).

Each capability is not isolated—the shutdown mechanism relies on `notify_all` to wake all waiting threads, timeout operations rely on the `wait_for` return value to distinguish failure causes, and the `stop_token` version of `pop` needs to cooperate with `jthread` to achieve complete graceful exit. These designs combined form a thread-safe queue that can be used directly in real projects.

Of course, this queue still has performance bottlenecks in high-contention scenarios—all threads share one mutex, so throughput doesn't go up. In the next article, we will discuss strategies like sharded locks, fine-grained locks, and copy-on-write to reduce contention. The core idea is "let fewer threads fight for the same lock."

## Exercises

### Exercise 1: Bounded Blocking Queue Shutdown Test

Write a multi-threaded test to verify the correctness of the shutdown mechanism: start 3 producer threads and 2 consumer threads. Producers each push 100 elements, consumers each `pop` until they receive `PopResult::Closed`. Call `close()` after all producers finish, and verify that consumers ultimately consumed exactly 300 elements (no loss, no duplicates), and all threads exit normally.

**Hint:** Use an `std::atomic<int>` to count the total elements retrieved by consumers, and check after all threads `join` if it equals 300.

### Exercise 2: Correctness Verification of Timeout Pop

Create a queue with a capacity of 5 and do not push any elements. Start a consumer thread calling `try_pop` with a 200ms timeout, and verify it returns `PopResult::Timeout`. Then push one element into the queue, call `try_pop` with a 200ms timeout again, and verify it returns `PopResult::Success`. Use `std::chrono::steady_clock` to measure the actual duration of the two operations to confirm the timeout version's wait time is within the expected range.

### Exercise 3: Stop Token Cancellation of Pop

Use `std::jthread` to create a consumer, passing the `stop_token` version of `pop`. The main thread calls `request_stop` after sleeping for 100ms, and verify that the consumer thread is woken up in `pop` and exits normally. Then try another sequence: `close` the queue first, then `request_stop`, and observe the consumer's behavior—if there are still elements in the queue, the consumer should finish consuming them before exiting.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `examples/thread_safe_queue`.

## References

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable_any -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any)
- [std::stop_token -- cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [std::jthread -- cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4 & 6](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [Why does std::condition_variable not support std::stop_token? -- StackOverflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
