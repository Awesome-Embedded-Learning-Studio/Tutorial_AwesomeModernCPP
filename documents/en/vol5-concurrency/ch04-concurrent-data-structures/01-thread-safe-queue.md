---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: Build a closable, timeout-capable bounded blocking queue with mutex
  and condition_variable
difficulty: intermediate
order: 1
platform: host
prerequisites:
- condition_variable and Wait Semantics
reading_time_minutes: 26
related:
- Thread-Safe Container Design
- SPSC and MPMC Queues
tags:
- host
- cpp-modern
- intermediate
- mutex
title: Thread-Safe Queue
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/01-thread-safe-queue.md
  source_hash: c8b13dfd90f2c492983ae8c68cc7c289d526135ea150f10328a132f41f8a8320
  translated_at: '2026-09-26T08:13:08+00:00'
  engine: anthropic
  token_count: 4300
---
# Thread-Safe Queue

Last time, in the `condition_variable` article, we wrote a simplified `BoundedQueue`—it had `push` and `pop`, it could block, it could notify. Honestly, it felt pretty respectable when we finished it. But if you dropped it straight into production code, I'd bet you'd hit trouble within two days: how does the queue shut down gracefully? What if a producer thread crashes while a consumer is blocked in `pop`? What if I don't want to wait indefinitely, and only want to try fetching one element? What if I want to cancel the wait from the outside?

Until these questions are answered, this queue is just a teaching toy. In this article we'll upgrade it from teaching toy to genuinely usable component—adding a close mechanism, `try_push`/`try_pop` with timeouts, C++20 `stop_token` integration, and a backpressure strategy for when the queue is full. We'll go step by step, each step adding one capability on top of the previous one, so you can clearly see where every design decision comes from. No rushing ahead, though—first we solidify the foundation.

## Starting Point: A Working BoundedQueue

First, let's carry over the queue we wrote in the `condition_variable` article as today's starting point:

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity)
    {}

    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return value;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};
```

The core logic of this version is fine. The two condition variables (`not_full_` and `not_empty_`) each mind their own waiters and notifiers, and the predicate-based `wait` guards against spurious wakeups and lost wakeups. But think it through and you'll find three fatal flaws. First, both `push` and `pop` can block indefinitely—if producers never push, consumers wait forever, and vice versa. Second, there is no close mechanism at all—when the queue's lifetime ends, any threads still blocked on `wait` will never wake up, and the program simply hangs. Third, there is no timeout capability—callers cannot give up waiting after a specified time.

Leave these three problems unsolved and take this queue into server code, and you're basically sitting on a time bomb. Let's defuse them one by one.

## Step 1: Shut It Down—The Right Way to Close a Queue

The close mechanism is the single most important non-functional requirement of a thread-safe queue, bar none. Picture a typical producer-consumer scenario: multiple producers drop tasks into the queue, and multiple consumers take tasks out and execute them. When the program needs to exit—whether it's a normal shutdown, a received SIGTERM, or something going wrong—we want a clean shutdown flow: producers stop submitting new tasks, consumers finish processing whatever is left in the queue, and then everyone exits gracefully. If your queue can't even "power off", the longer you use it the less you'll trust it.

The semantics of closing deserve careful design; it's not as simple as setting `closed_ = true` and calling it done. We need a `closed_` flag to indicate whether the queue is closed, and it affects the behavior of both `push` and `pop`. The rule for `push` is fairly simple: once the queue is closed, all new pushes should be rejected, because nobody will come along to consume that data anymore. The rule for `pop` is subtler: after closing, if the queue still holds elements, consumers should be able to take them all (drain) until the queue is empty; once it's empty, `pop` should no longer block but should return a signal meaning "queue is empty and closed". These drain semantics matter a great deal—if draining weren't allowed, every task still sitting unprocessed in the queue at close time would be lost.

All right, the semantics are settled. We'll use an enum to represent operation results:

```cpp
enum class QueueResult {
    kSuccess,
    kClosed,
    kTimeout
};
```

Next we add the `closed_` flag to the queue and modify the predicate logic of `push` and `pop`:

```cpp
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity), closed_(false)
    {}

    // Close the queue. After this call push fails, and pop fails after draining the remaining elements
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        // Wake up all waiting threads so they can check the closed_ flag
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    QueueResult push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // Predicate: push can proceed when the queue is not full and not closed
        not_full_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    QueueResult pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // Predicate: the queue is not empty, or the queue is closed and empty
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });

        if (queue_.empty()) {
            // Empty queue + closed_ == true = drain complete
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};
```

That's a fair amount of code, so let's unpack what's going on inside. First, `close()`: it sets `closed_ = true` under the protection of the lock, then releases the lock, then wakes all waiting threads with `notify_all()`. You might ask why we don't notify while still holding the lock. Technically we could, but `notify_all` itself doesn't need to run under the lock (the standard permits notifying outside it), and moving the notify out of the lock avoids one needless round of lock contention: the awakened threads can immediately contend for the lock instead of waiting for the closing thread to release it first. And why `notify_all` rather than `notify_one`? Because closing is a global event—every waiting producer and consumer needs to be woken. With `notify_one` alone, only one thread wakes each time while the rest keep sleeping, and the awakened thread would then have to `notify` the next one... that chain is far too fragile, and its latency is uncontrolled. `notify_all` is the standard practice for the close scenario.

Now the `push` predicate. It used to be `queue_.size() < capacity_`; now it has `|| closed_` appended. That means `wait` returns in two situations: either the queue is no longer full, or the queue has been closed. Once it returns, we check `closed_`—if it's `true`, the queue is closed, we should not push, and we return `kClosed` directly. Note the order of the checks: check `closed_` first, then decide whether to operate. This guarantees that no new element enters the queue after it closes.

The `pop` predicate is similar: `!queue_.empty() || closed_`. After `wait` returns we check `queue_.empty()`—if the queue is empty, there is nothing to fetch no matter what `closed_` says, so we return `kClosed`. If the queue is not empty, we keep fetching even when `closed_` is `true`—that is exactly the drain semantics: after closing, consumers are still allowed to consume all remaining elements.

You may have noticed a subtle detail: after `wait` returns, `push` checks `closed_`, but `pop` checks `queue_.empty()` rather than `closed_`. Why the asymmetry? Because the semantics differ: the only reason a push is rejected is that the queue is closed (push never blocks while the queue is not full), whereas a pop fails because the queue is empty (closed or not). After closing, while the queue is still non-empty, pop should keep taking out the remaining elements; only once the closed queue is empty should pop report failure. So pop uses `queue_.empty()` as the criterion for "is there anything left to fetch"—which reflects pop's intent more precisely than consulting `closed_` directly.

## Step 2: Refusing to Wait Forever—try_push and try_pop with Timeouts

The close mechanism solves "graceful exit"—good. But there is another class of scenario it can't help with: the caller doesn't want to block indefinitely, just attempt the operation within a time budget and give up on timeout. Say a network service wants to stuff a request into the queue, but if the queue has been full for 100 milliseconds with no room in sight, it would rather drop that request than block—response latency is deadlier than losing the odd request. That's when you need `try_push` and `try_pop` with timeouts.

We implement them directly with `wait_for`, which is a natural fit for this "wait a bit and try" kind of scenario:

```cpp
template <typename T>
class BoundedQueue {
public:
    // ... the earlier methods are unchanged ...

    template <typename Rep, typename Period>
    QueueResult try_push(T value,
                         const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_full_.wait_for(lock, timeout, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (!ok) {
            // Timed out; the predicate is still false
            return QueueResult::kTimeout;
        }

        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    template <typename Rep, typename Period>
    QueueResult try_pop(T& value,
                        const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_empty_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok) {
            return QueueResult::kTimeout;
        }

        if (queue_.empty()) {
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }
};
```

The predicate overload of `wait_for` returns a `bool`—`true` if the predicate holds (whether it was notified, or the condition happened to be met just before the deadline), and `false` if the timeout expired with the predicate still `false`. We use that return value to distinguish three outcomes: timeout (`!ok`, return `kTimeout`), closed (`ok` but `closed_` is `true`, return `kClosed`), and success.

One design choice here deserves a mention: why check `!ok` before `closed_`? Because once the operation has timed out, we no longer need to care about the state of `closed_`—what the caller cares about is "my operation did not succeed within the given time", and the specific reason (queue full or queue closed) no longer matters to them. Of course, you can flip it around—if your use case needs to tell "timeout" from "closed", just adjust the order of the checks. There is no single right answer here; it depends on which information you want to convey to the caller.

## Step 3: Making It Cancellable—C++20 stop_token Integration

`try_push` and `try_pop` solve "I don't want to wait too long", but there is yet another scenario they can't cover: actively cancelling the wait from the outside. C++20 introduced the `std::stop_token` / `std::stop_source` / `std::jthread` trio, providing a standard mechanism for cooperative cancellation. Can we make the queue's `pop` operation support a `stop_token`—so that when an external stop is requested, a blocked `pop` wakes up immediately, without waiting for a timeout and without waiting for data?

The answer is yes, but with one precondition: `std::condition_variable_any` must replace `std::condition_variable`. The reason is that C++20 added a `wait` overload to `condition_variable_any` that accepts a `stop_token`—when a stop is requested, `wait` is awakened automatically. `std::condition_variable` has no such overload; it is coupled too deeply with `unique_lock<mutex>`, and adding stop_token support would have required changing its internals, so the standards committee chose to provide the feature only on the more general `condition_variable_any`. In other words: if you want stop_token, you accept the slightly heavier overhead of `condition_variable_any`.

Let's see how to integrate it. To keep the core logic in the spotlight, here is a standalone simplified version—only the stop_token-aware pop and the minimal context it needs:

```cpp
#include <stop_token>
#include <condition_variable>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity), closed_(false)
    {}

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    // pop with stop_token support: returns false when an external stop is requested
    bool pop(T& value, std::stop_token stoken)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // the stop_token overload of condition_variable_any
        bool ok = cv_.wait(lock, stoken, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok) {
            // stop was requested; the predicate was never satisfied
            return false;
        }

        if (queue_.empty()) {
            // queue is closed and empty
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        cv_.notify_one();
        return true;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_;
    mutable std::mutex mutex_;
    // condition_variable_any is used here to support stop_token
    std::condition_variable_any cv_;
};
```

Compared with the earlier versions, you'll find the single most essential change is this: the condition variable went from `std::condition_variable` to `std::condition_variable_any`. The latter's interface is fully compatible with the former, but it additionally supports pairing with `stop_token`—at the cost of a slightly heavier internal implementation (it needs an extra internal mutex to manage the wait queue), which in the vast majority of scenarios is entirely negligible.

Then there is the semantics of `cv_.wait(lock, stoken, pred)`. It waits until either `pred()` is `true`, or a stop is requested on `stoken`. It returns `true` when the predicate is satisfied, and `false` when a stop was requested while the predicate was unsatisfied. If the predicate happens to hold at the moment the stop is requested, it returns `true`—that is, the predicate takes precedence over stop. Which is reasonable: if what you were waiting for has already arrived, there is no point abandoning it because of a stop.

On the consuming side, pairing this with `std::jthread` is very natural. `jthread` is the thread class newly introduced in C++20; its biggest difference from `std::thread` is built-in stop_token support and automatic join semantics—the destructor requests a stop on its own and waits for the thread to finish, so you never have to join manually again:

```cpp
#include <thread>
#include <iostream>

int main()
{
    BoundedQueue<int> queue(16);

    std::jthread consumer([&](std::stop_token stoken) {
        int value;
        while (queue.pop(value, stoken)) {
            std::cout << "Consumed: " << value << "\n";
        }
        std::cout << "Consumer exiting (stop requested or queue closed)\n";
    });

    // Producer
    for (int i = 0; i < 100; ++i) {
        queue.push(i);
    }

    // Graceful shutdown: close the queue first, then request a stop
    queue.close();
    consumer.request_stop();

    // jthread joins automatically on destruction
    return 0;
}
```

When `jthread` is constructed, it automatically passes its internal `stop_token` to the thread function—as long as the first parameter of the function signature is `std::stop_token`. The consumer forwards this `stop_token` into `pop`; when the main thread calls `request_stop()`, the blocked `pop` wakes up and returns `false`, and the consumer loop exits right there.

> One point deserves emphasis: here we did both `close()` and `request_stop()`. `close()` guarantees producers no longer submit new elements, and `request_stop()` guarantees consumers don't wait indefinitely on an empty queue. Neither can be omitted—close without stop, and the consumer may still be waiting foolishly in `pop` for one last element (if the queue is already empty); stop without close, and producers may still be stuffing data into a queue nobody consumes. The two working together is what makes a complete graceful exit.

## Step 4: When the Queue Is Full—Backpressure Strategies

Up to now, our answer to a full queue has always been "block and wait"—the producer blocks in `push` until a consumer takes an element and frees up space. That is the simplest strategy, but not the only one. In some scenarios, blocking the producer is inappropriate or even dangerous. Picture a high-throughput network service receiving tens of thousands of requests per second: if downstream processing can't keep up and the queue fills up, a blocked producer thread means the service's receiving thread seizes entirely and every new connection times out—that's not "a bit slow", that's the whole service down. What we need in that situation is **backpressure**—letting the producer sense the pressure from downstream and respond deliberately, instead of waiting foolishly.

There are three common backpressure strategies. The first is blocking wait—what we already have—suitable for scenarios where the producer can afford the latency. The second is drop newest: when the queue is full, the newly arriving element is simply discarded, suitable for scenarios where losing data is acceptable, such as log aggregation or metrics reporting. The third is drop oldest: when the queue is full, the oldest element in the queue is evicted to make room for the new one, suitable for "only the most recent data matters" scenarios, such as the sliding window of real-time monitoring.

Let's take drop newest as an example and implement a `push_or_drop`. Its semantics are simple: if the queue isn't full, enqueue as usual; if it's full, discard outright; never block:

```cpp
// Drop the value if the queue is full; never block
// Returns true if successfully enqueued, false if dropped
bool push_or_drop(T value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_) {
        return false;
    }

    if (queue_.size() >= capacity_) {
        // Queue is full; drop
        return false;
    }

    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

Notice that no `condition_variable` waiting happens here—just take the lock, check the capacity, and return `false` when full. The operation's time complexity is O(1), it never blocks, and the producer can never get stuck. On receiving `false`, the caller can decide to retry, drop, or take a degraded path—far more flexible than blocking wait.

If you need the drop-oldest strategy, a small tweak to the logic—evicting the oldest element—does the job:

```cpp
bool push_or_evict_oldest(T value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_) {
        return false;
    }

    if (queue_.size() >= capacity_) {
        // Evict the oldest element
        queue_.pop();
    }

    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

This "strategy-oriented" design is very common in real projects—the queue itself offers multiple push modes, letting callers pick the strategy that fits their use case. You can also turn the strategy into a template parameter or parameterize it with an enum, letting the queue decide its backpressure behavior at compile time or at runtime. Which one to pick depends on whether your business says "rather lose data than stall" or "rather stall than lose data"—we've run into both needs in real projects.

## Correctness in Multi-Producer, Multi-Consumer Scenarios

All of our implementations so far natively support MPMC (Multiple Producers, Multiple Consumers) scenarios, because every access to the shared state (`queue_`, `closed_`) happens under the protection of `mutex_`. So "correctness" is nothing to worry about here. But "correct" and "efficient" are two different things—let's look at the pitfalls you'll step into in real MPMC scenarios.

The most obvious problem is lock contention. As the number of producers and consumers grows, every thread competes for the same mutex—at any given moment only one thread can operate on the queue while the rest wait for the lock. Under high throughput, this mutex becomes the bottleneck; the time everyone spends queuing for the lock may exceed the time actually spent doing work. The next article discusses sharded locks, fine-grained locks, and other contention-reducing strategies in detail—for now, just knowing the problem exists is enough.

Another easily overlooked problem is the fairness of `notify_one`. `notify_one` wakes "one" thread from the wait queue, but exactly which thread depends on the operating system's scheduling policy—usually FIFO (first waited, first woken), but the standard does not guarantee it. In extreme cases, certain consumers may always be skipped, resulting in starvation. If you need strict fairness, you have to implement it at the application layer—for example with a ticket lock or round-robin dispatch.

One more correctness detail is worth mentioning: the choice of `notify_one` vs `notify_all`. In `push` we use `notify_one` to wake one consumer; in `pop` we use `notify_one` to wake one producer. This is optimal for SPSC (single producer, single consumer) and low-contention MPMC—only one party is woken, avoiding the thundering herd effect. Under heavy contention, though, `notify_one` can cause a variant of the thundering-herd problem: a `notify_one` wakes one consumer, but that consumer acquires the lock only to find the queue has already been emptied by another consumer, so it has to go back to waiting. Such "futile wakeups" happen frequently under high contention. Ironically, in that scenario `notify_all` may actually be better—sure, it wakes more threads, but at least one of them will manage to operate successfully. That said, tuning this requires benchmarking against the concrete workload pattern; there is no universally correct answer.

## Exception Safety: An Easily Overlooked Corner

Finally, let's talk about a topic that is easy to overlook but sends your blood pressure through the roof when it actually bites: exception safety. In the implementations above, we silently assumed `queue_.push(std::move(value))` cannot throw—but what if `T`'s move constructor throws? What if `T`'s copy constructor throws?

The good news is that `std::queue`'s `push` provides the strong exception guarantee: if `push` throws, the queue's state is unchanged (no element gets added). So in our `push` method, if `queue_.push(std::move(value))` throws, the `unique_lock` destructor releases the mutex automatically, `notify_one` is not called (the exception skipped it), and the queue is in exactly the state it was in before `push` was called—which is precisely the behavior we want.

But a sneakier problem hides in `pop`: what if `T`'s move assignment operator (in the line `value = std::move(queue_.front())`) throws? At that point the element is still in the queue (`queue_.front()` returns a reference), but the assignment into `value` has failed. The result is that the element remains in the queue while the caller got no value—the next `pop` will fetch the same element again. This is not necessarily a bug (it depends on `T`'s semantics), but if `T`'s move assignment is not `noexcept`, you need to think this edge case through carefully.

If `T` is one of the standard types—`int`, `std::string`, `std::unique_ptr`—their move operations are all `noexcept`, so there is nothing to worry about. But if you want to store custom types, you'd best ensure their move operations are `noexcept`—the simplest way is to add `static_assert`s to the queue's template constraints and let the compiler keep watch for you:

```cpp
static_assert(std::is_nothrow_move_constructible_v<T>,
              "T must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<T>,
              "T must be nothrow move assignable");
```

That way, if you accidentally store a throwing type, the compiler stops you on the spot at compile time, instead of waiting until runtime to crash on some bizarre code path.

Incidentally, `wait` itself is reliable with respect to exception safety. The C++ standard guarantees: if `wait` receives a signal while waiting but the predicate is still `false` (a spurious wakeup), it goes back to waiting and does not leak the lock. And if `wait` exits due to an exception (an extreme case), the lock is properly released. So condition-variable `wait` requires no extra care from us on the exception-safety front.

## The Complete Implementation: Putting It All Together

At this point we have discussed the close mechanism, timed operations, stop_token integration, and backpressure strategies. Now let's fold all of these features together and produce a complete `BoundedBlockingQueue` you can take and use directly:

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stop_token>
#include <type_traits>

enum class QueueResult {
    kSuccess,
    kClosed,
    kTimeout
};

template <typename T>
class BoundedBlockingQueue {
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "T must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable");

public:
    explicit BoundedBlockingQueue(std::size_t capacity)
        : capacity_(capacity), closed_(false)
    {}

    // === Basic operations ===

    QueueResult push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    QueueResult pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });

        if (queue_.empty()) {
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    // === Timed operations ===

    template <typename Rep, typename Period>
    QueueResult try_push(T value,
                         const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_full_.wait_for(lock, timeout, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (!ok) {
            return QueueResult::kTimeout;
        }
        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    template <typename Rep, typename Period>
    QueueResult try_pop(T& value,
                        const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_empty_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok) {
            return QueueResult::kTimeout;
        }
        if (queue_.empty()) {
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    // === stop_token-cancellable operations (C++20) ===

    bool pop(T& value, std::stop_token stoken)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = cv_any_.wait(lock, stoken, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok || queue_.empty()) {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();

        if (queue_.size() < capacity_) {
            not_full_.notify_one();
        }
        return true;
    }

    // === Backpressure strategies ===

    bool push_or_drop(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (closed_ || queue_.size() >= capacity_) {
            return false;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    // === Management ===

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
        cv_any_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::condition_variable_any cv_any_;  // for the stop_token variant
};
```

You may notice that this keeps `not_full_` and `not_empty_` (`condition_variable`) alongside `cv_any_` (`condition_variable_any`). The basic `push`/`pop` use the former (more efficient), while the `stop_token` version of `pop` uses the latter (stop_token support). This is a pragmatic compromise: code that doesn't need stop_token takes the high-performance path, and code that does takes the general-purpose path. Best of both worlds—each caller gets what it needs.

## Summary

In this article we started from the teaching-grade `BoundedQueue` of the `condition_variable` article and, step by step, turned it into a production-grade `BoundedBlockingQueue`. We added four key capabilities in turn: the close mechanism (`close()` rejects new pushes and allows draining pops), `try_push`/`try_pop` with timeouts (`wait_for` implements non-blocking attempts), stop_token integration (the C++20 `condition_variable_any` overload implements cooperative cancellation), and backpressure strategies (`push_or_drop` provides a non-blocking drop mode).

None of these capabilities stands alone—the close mechanism relies on `notify_all` to wake all waiting threads, the timed operations rely on the `QueueResult` enum to distinguish failure reasons, and the stop_token version of pop must work with `close()` to achieve a complete graceful exit. Combined, these designs form a thread-safe queue that can be used directly in real projects.

Of course, this queue still has a performance bottleneck under heavy contention—all threads share a single mutex, so throughput can't scale. In the next article we'll discuss sharded locks, fine-grained locks, copy-on-write, and other strategies for reducing contention; the core idea is simply "make fewer threads fight over the same lock".

## Exercises

### Exercise 1: A Bounded Blocking Queue Shutdown Test

Write a multi-threaded test that verifies the close mechanism: start 3 producer threads and 2 consumer threads; each producer pushes 100 elements, and each consumer pops until it receives `kClosed`. Call `close()` after all producers finish, and verify that the consumers ultimately consumed exactly 300 elements (none lost, none duplicated) and that all threads exit normally.

Hint: use a `std::atomic<int>` to count the total number of elements the consumers fetched, and check that it equals 300 after all threads are joined.

### Exercise 2: Verifying the Correctness of Timed pop

Create a queue with capacity 5 and push nothing into it. Start a consumer thread that calls `try_pop` with a 200ms timeout, and verify that it returns `kTimeout`. Then push one element into the queue, call `try_pop` with a 200ms timeout again, and verify that it returns `kSuccess`. Measure the actual duration of both calls with `std::chrono`, and confirm the timed-out call's wait falls within the expected range.

### Exercise 3: Cancelling pop with stop_token

Use `std::jthread` to create a consumer and pass a `stop_token` into the `stop_token`-aware `pop`. After the main thread sleeps for 100ms, call `request_stop()`, and verify that the consumer thread is awakened inside `pop` and exits normally. Then try the other order: `close()` the queue first, then `request_stop()`, and observe the consumer's behavior—if elements remain in the queue, the consumer should finish draining them before exiting.

> 💡 The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); browse to `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## References

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable_any -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any)
- [std::stop_token -- cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [std::jthread -- cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4 & 6](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [Why does std::condition_variable not support std::stop_token? -- StackOverflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
