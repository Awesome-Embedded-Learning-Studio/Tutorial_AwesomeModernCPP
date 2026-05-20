---
title: Thread-Safe Queue
description: Building a closeable, timeout-supporting bounded blocking queue with
  `mutex` + `condition_variable`
chapter: 4
order: 1
tags:
- host
- cpp-modern
- intermediate
- mutex
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- condition_variable 与等待语义
related:
- 线程安全容器设计
- SPSC 与 MPMC 队列
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/01-thread-safe-queue.md
  source_hash: 1fa1f6a0bfae90d0f8b6e903d234908048aab0502d588fac75059a8d1322e184
  translated_at: '2026-05-20T04:41:50.333341+00:00'
  engine: anthropic
  token_count: 5320
---
# Thread-Safe Queues

In the previous article on `condition_variable`, we built a simplified ``BoundedQueue``—one with ``push`` and ``pop``, capable of blocking and notifying. To be honest, it looked pretty decent at the time. But if you drop it straight into production code, I'd bet money it'll break within two days: How do we gracefully shut down the queue? What happens if a producer thread crashes while a consumer is blocked on ``pop``? What if we don't want to wait indefinitely and just want to try popping a single element? What if we want to cancel the wait from the outside?

Until we address these issues, this queue is nothing more than a teaching toy. In this article, we'll transform it from a toy into a genuinely usable component—adding a shutdown mechanism, timed `try_push` / `try_pop`, C++20 `stop_token` integration, and backpressure strategies when the queue is full. We'll take it step by step, building each new capability on top of the last, so you can clearly see the reasoning behind every design decision. But first, let's solidify the foundation.

## Starting Point: A Working BoundedQueue

Let's bring over the queue we wrote in the `condition_variable` article as our starting point for today:

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

The core logic of this version is sound. The two condition variables (``not_full_`` and ``not_empty_``) each manage their own waiters and notifiers, and the predicate-based ``wait`` guards against spurious wakeups and lost wakeups. But if you think about it carefully, it has three fatal flaws: First, both ``push`` and ``pop`` can block indefinitely—if a producer never pushes, the consumer waits forever, and vice versa. Second, there is no shutdown mechanism—when the queue reaches the end of its lifetime, if threads are still blocked on ``wait``, they will never wake up, and the program simply deadlocks. Third, there is no timeout capability—callers cannot give up waiting after a specified duration.

If you leave these three issues unresolved and use this queue to write server code, you're essentially sitting on a ticking time bomb. Let's defuse them one by one.

## Step 1: Shut It Down — The Right Way to Close a Queue

A shutdown mechanism is the most important non-functional requirement of a thread-safe queue, bar none. Picture a typical producer-consumer scenario: multiple producers push tasks into a queue, and multiple consumers pop and execute them. When the program needs to exit—whether it's a graceful shutdown, receiving a SIGTERM, or some exception occurring—we want a clear shutdown flow: producers stop pushing new tasks, consumers finish processing the remaining tasks in the queue, and then everyone exits gracefully. If we can't even "power off," using this queue will always make you feel uneasy.

The shutdown semantics need careful design; it's not as simple as setting a ``closed_ = true`` and calling it a day. We need a ``closed_`` flag to indicate whether the queue is closed, which affects the behavior of ``push`` and ``pop``. The rule for ``push`` is straightforward: once the queue is closed, all new pushes should be rejected because no one will be consuming the data anymore. The rule for ``pop`` is more subtle: after closing, if there are still elements in the queue, consumers should be able to drain them all until the queue is empty; once empty, ``pop`` should no longer block but instead return a "queue empty and closed" signal. This drain semantics is crucial—if draining isn't allowed, any unprocessed tasks left in the queue when it closes are simply lost.

Alright, with the semantics clear, let's use an enum to represent the operation results:

```cpp
enum class QueueResult {
    kSuccess,
    kClosed,
    kTimeout
};
```

Next, we add the ``closed_`` flag to the queue and modify the predicate logic for ``push`` and ``pop``:

```cpp
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity), closed_(false)
    {}

    // 关闭队列。调用后 push 会失败，pop 会 drain 剩余元素后失败
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        // 唤醒所有正在等待的线程，让它们检查 closed_ 标志
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    QueueResult push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 谓词：队列不满且未关闭时可以 push
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
        // 谓词：队列不空，或者队列已关闭且已空
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });

        if (queue_.empty()) {
            // 队列空 + closed_ 为 true = drain 完成
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

That's a fair amount of code, so let's break down the key details. First, look at the ``close()`` method—it sets ``closed_ = true`` under the protection of the lock, then releases the lock, and uses ``notify_all()`` to wake up all waiting threads. You might wonder why we don't just notify inside the lock. Technically, we could, but ``notify_all`` doesn't need to be executed inside the lock (the standard allows notifying outside the lock). Moving the notify outside the lock reduces one unnecessary lock contention: awakened threads don't have to wait for the closing thread to release the lock before they can immediately try to acquire it. And why use ``notify_all`` instead of ``notify_one``? Because shutting down is a global event—all waiting producers and consumers need to be woken up. If we only use ``notify_one``, waking up one thread at a time, the other threads would still be waiting foolishly, requiring the awakened thread to ``notify`` the next one... This chain is too fragile, and the latency is unpredictable. ``notify_all`` is the standard approach for shutdown scenarios.

Now let's look at the predicate for ``push``. Previously it was ``queue_.size() < capacity_``, and now we've added ``|| closed_``. This means ``wait`` will return in two situations: either the queue is no longer full, or the queue is closed. After it returns, we check ``closed_``—if it's ``true``, it means the queue is already closed, so we shouldn't push and directly return ``kClosed``. Note the order of checks here: we check ``closed_`` first, then decide whether to proceed. This guarantees that no new elements enter the queue after it's closed.

The predicate for ``pop`` is similar: ``!queue_.empty() || closed_``. After ``wait`` returns, we check ``queue_.empty()``—if the queue is empty, there's nothing to fetch regardless of the ``closed_`` state, so we return ``kClosed``. If the queue is not empty, we continue popping even if ``closed_`` is ``true``—this is the drain semantics: after closing, consumers are allowed to fully consume all remaining elements.

You might have noticed a subtle detail: after ``push`` returns, we check ``closed_``, but after ``pop`` returns, we check ``queue_.empty()`` instead of ``closed_``. Why the asymmetry? Because the semantics are different: the only reason a push is rejected is that the queue is closed (push won't block when the queue isn't full), whereas a pop fails because the queue is empty (regardless of whether it's closed). When the queue is not empty after closing, pop should continue to retrieve the remaining elements; only when the queue is empty after closing should pop report failure. So pop uses ``queue_.empty()`` as the criterion for "is there anything left to fetch"—this more accurately reflects the intent of pop than directly checking ``closed_``.

## Step 2: Don't Wait Forever — Timed try_push and try_pop

The shutdown mechanism solves the "graceful exit" problem, which is great. But there's another class of scenarios it can't handle: callers don't want to block indefinitely; they just want to try the operation for a certain amount of time and give up if it times out. For example, a network service wants to push a request into a queue, but if the queue is full and there's no space after waiting 100 milliseconds, it would rather drop the request than block—response latency is more fatal than dropping a request or two. This is where timed ``try_push`` and ``try_pop`` come in.

We implement this directly using ``wait_for``, which is naturally suited for this "wait a bit and try" scenario:

```cpp
template <typename T>
class BoundedQueue {
public:
    // ... 前面的方法不变 ...

    template <typename Rep, typename Period>
    QueueResult try_push(T value,
                         const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_full_.wait_for(lock, timeout, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (!ok) {
            // 超时了，谓词仍然为 false
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

The predicate version of ``wait_for`` returns ``bool``—if the predicate is ``true``, it returns ``true`` (whether it was notified or the condition happened to be satisfied right before the timeout), and if it times out and the predicate is still ``false``, it returns ``false``. We leverage this return value to distinguish between three situations: timeout (``!ok``, return ``kTimeout``), closed (``ok`` but ``closed_`` is ``true``, return ``kClosed``), and success.

There's a design choice here worth mentioning: why do we check ``!ok`` before checking ``closed_``? Because if it timed out, we no longer need to care about the ``closed_`` state—what the caller cares about is "I didn't succeed within the given time," and the specific reason (queue full or queue closed) no longer matters to them. Of course, you could do it the other way around—if your business scenario needs to distinguish between "timeout" and "closed," just adjust the order of checks. There's no single right answer here; it depends on what information you want to pass to the caller.

## Step 3: Make It Cancellable — C++20 stop_token Integration

``try_push`` and ``try_pop`` solve the "I don't want to wait too long" problem, but there's another scenario they can't handle: actively canceling the wait from the outside. C++20 introduced the ``std::stop_token`` / ``std::stop_source`` / ``std::jthread`` trio, providing a standard mechanism for cooperative cancellation. Can we make the queue's ``pop`` operation support ``stop_token``—so that when an external stop is requested, the blocking ``pop`` is immediately awakened without waiting for a timeout or for data to appear in the queue?

The answer is yes, but there's a prerequisite: we need to use ``std::condition_variable_any`` instead of ``std::condition_variable``. The reason is that C++20 added a new ``wait`` overload to ``condition_variable_any`` that accepts a ``stop_token``—when a stop is requested, ``wait`` is automatically awakened. ``std::condition_variable`` doesn't have this overload because its coupling to ``unique_lock<mutex>`` is too deep; adding stop_token support would require modifying its internal implementation, so the standards committee chose to provide this feature only on the more general ``condition_variable_any``. In other words, if you want stop_token, you have to accept the slightly heavier overhead of ``condition_variable_any``.

Let's see how to integrate it. To highlight the core logic, here's a standalone simplified version—keeping only the stop_token-related pop and the minimal context it needs:

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

    // 支持 stop_token 的 pop：外部请求停止时返回 false
    bool pop(T& value, std::stop_token stoken)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // condition_variable_any 的 stop_token 重载
        bool ok = cv_.wait(lock, stoken, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok) {
            // stop 被请求了，谓词还没满足
            return false;
        }

        if (queue_.empty()) {
            // 队列关闭且已空
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
    // 使用 condition_variable_any 以支持 stop_token
    std::condition_variable_any cv_;
};
```

You'll notice that compared to the previous versions, the most core change in this code is exactly one thing: the condition variable was swapped from ``std::condition_variable`` to ``std::condition_variable_any``. The latter's interface is fully compatible with the former, but it additionally supports pairing with ``stop_token``—the trade-off is a slightly heavier internal implementation (it needs an additional internal mutex to manage the wait queue), but in the vast majority of scenarios, this overhead is completely negligible.

Then there's the semantics of ``cv_.wait(lock, stoken, pred)``. It waits until ``pred()`` is ``true``, or a stop is requested on ``stoken``. It returns ``true`` to indicate the predicate was satisfied, and ``false`` to indicate a stop was requested and the predicate was not satisfied. If a stop is requested but the predicate happens to also be satisfied, it returns ``true``—meaning the predicate takes priority over the stop. This makes perfect sense: if what you were waiting for has already arrived, there's no need to abandon it because of a stop.

On the consumer side, using it in conjunction with ``std::jthread`` feels very natural. ``jthread`` is a thread class newly introduced in C++20, and its biggest difference from ``std::thread`` is its built-in stop_token support and automatic join semantics—upon destruction, it automatically requests a stop and waits for the thread to finish, so you never need to manually join again:

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

    // 生产者
    for (int i = 0; i < 100; ++i) {
        queue.push(i);
    }

    // 优雅关闭：先关闭队列，再请求停止
    queue.close();
    consumer.request_stop();

    // jthread 析构时自动 join
    return 0;
}
```

``jthread`` automatically passes its internal ``stop_token`` to the thread function upon construction—as long as the first parameter of the function signature is a ``std::stop_token``. The consumer passes this ``stop_token`` into ``pop``. When the main thread calls ``request_stop()``, the blocking ``pop`` is awakened and returns ``false``, causing the consumer loop to exit.

> One point worth emphasizing: here we do both ``close()`` and ``request_stop()``. ``close()`` ensures producers stop pushing new elements, and ``request_stop()`` ensures consumers won't wait indefinitely on an empty queue. Both are indispensable—if you only close without stopping, consumers might still be waiting foolishly in ``pop`` for one last element (if the queue is already empty); if you only stop without closing, producers might still be pushing data into a queue that nobody is consuming. Only by combining both do we get a complete graceful exit.

## Step 4: What to Do When the Queue Is Full — Backpressure Strategies

Up to this point, our approach to handling a full queue has been "block and wait"—the producer blocks in ``push`` until a consumer pops an element to free up space. This is the simplest strategy, but it's not the only one. In certain scenarios, blocking the producer is inappropriate or even dangerous. Imagine a high-throughput network service receiving tens of thousands of requests per second. If the downstream processing speed can't keep up and the queue fills up, blocking the producer thread means the service's receiving threads deadlock, and all new connections time out—this isn't "a bit slow," the entire service goes down. What we need here is **backpressure**—letting the producer feel the downstream pressure and make a conscious response, rather than just waiting foolishly.

There are three common backpressure strategies. The first is block-and-wait, which is our existing implementation, suitable for scenarios where the producer can tolerate latency. The second is drop newest—when the queue is full, simply discard the incoming element, suitable for scenarios where data loss is acceptable, such as log aggregation or metric reporting. The third is drop oldest—when the queue is full, evict the oldest element in the queue to make room for the new one, suitable for "only care about the latest data" scenarios, such as sliding windows for real-time monitoring.

Let's take drop newest as an example and implement a ``push_or_drop``. Its semantics are simple: if the queue isn't full, enqueue normally; if it's full, discard immediately and never block:

```cpp
// 如果队列满了就丢弃，不阻塞
// 返回 true 表示成功入队，false 表示被丢弃
bool push_or_drop(T value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_) {
        return false;
    }

    if (queue_.size() >= capacity_) {
        // 队列满，丢弃
        return false;
    }

    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

You'll notice that there's no need for ``condition_variable`` waiting here—we simply acquire the lock, check the capacity, and if it's full, return ``false``. This operation has O(1) time complexity, never blocks, and the producer can never get stuck. After getting ``false``, the caller can decide whether to retry, discard, or fall back to degradation logic—this is much more flexible than blocking and waiting.

If you need the drop oldest strategy, the logic is slightly modified—just kick out the oldest element:

```cpp
bool push_or_evict_oldest(T value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_) {
        return false;
    }

    if (queue_.size() >= capacity_) {
        // 踢掉最老的元素
        queue_.pop();
    }

    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

This kind of "strategized" design is very common in real-world projects—the queue itself provides multiple push modes, letting callers choose the appropriate strategy based on their business scenario. You could also template the strategy or parameterize it with an enum, letting the queue decide backpressure behavior at compile time or runtime. How you choose depends on whether your business logic dictates "rather drop than stall" or "rather stall than drop"—the author has encountered both requirements in actual projects.

## Correctness with Multiple Producers and Multiple Consumers

All of our implementations so far naturally support MPMC (Multiple Producers, Multiple Consumers) scenarios—because all access to shared state (``queue_``, ``closed_``) is protected by ``mutex_``. So there's no need to worry about "correctness" here. But "correct" and "efficient" are two different things; let's look at what pitfalls you'll encounter in real MPMC scenarios.

The most obvious issue is lock contention. As the number of producers and consumers increases, all threads compete for the same mutex—at any given moment, only one thread can operate on the queue, while the others wait for the lock. In high-throughput scenarios, this mutex becomes a bottleneck, and the time spent waiting in line for the lock might exceed the time actually doing work. We'll discuss strategies to reduce contention like sharded locks and fine-grained locks in detail in the next article; for now, just be aware that this problem exists.

Another easily overlooked issue is the fairness of ``notify_one``. ``notify_one`` wakes up "one" thread from the wait queue, but which specific thread depends on the operating system's scheduling policy—usually FIFO (first to wait, first to wake), but the standard doesn't guarantee this. In extreme cases, certain consumers might always be skipped, leading to starvation. If you need strict fairness, you need to implement it at the application level, for example using a ticket lock or round-robin dispatch.

There's also a correctness detail worth mentioning: the choice between ``notify_one`` and ``notify_all``. In ``push``, we use ``notify_one`` to wake one consumer, and in ``pop``, we use ``notify_one`` to wake one producer. This is optimal in SPSC (Single Producer, Single Consumer) and low-contention MPMC scenarios—only waking one person avoids the thundering herd effect. But in high-contention scenarios, ``notify_one`` can lead to a variant of the thundering herd problem: one ``notify_one`` wakes a consumer, but after acquiring the lock, that consumer finds the queue has already been emptied by another consumer, so it has to go back to waiting. This kind of "spurious wakeup" happens frequently under high contention. Ironically, in this scenario, ``notify_all`` might actually be better—although it wakes more threads, at least one thread will successfully complete its operation. However, this optimization requires benchmarking against your specific load pattern; there's no one-size-fits-all answer.

## Exception Safety: An Easily Overlooked Corner

Finally, let's talk about a topic that's easily overlooked but will send your blood pressure through the roof if it goes wrong: exception safety. In our previous implementations, we assumed by default that ``queue_.push(std::move(value))`` wouldn't throw exceptions—but what if the move constructor of ``T`` throws? What if the copy constructor of ``T`` throws?

The good news is that ``std::queue``'s ``push`` provides a strong exception guarantee: if ``push`` throws an exception, the queue's state remains unchanged (the element won't be added). So in our ``push`` method, if ``queue_.push(std::move(value))`` throws, the ``unique_lock`` destructor automatically releases the mutex, ``notify_one`` won't be called (because the exception skips over it), and the queue's state is exactly the same as before calling ``push``—which is exactly the behavior we want.

But there's a more hidden problem lurking in ``pop``: what if the move assignment operator of ``T`` (on the ``value = std::move(queue_.front())`` line) throws an exception? At this point, the element is still in the queue (``queue_.front()`` returns a reference), but the assignment to ``value`` failed. The result is that the element remains in the queue, but the caller didn't get the value—the next ``pop`` will pop the same element again. This isn't necessarily a bug (it depends on the semantics of ``T``), but if ``T``'s move assignment isn't ``noexcept``, you need to carefully consider this edge case.

If ``T`` is a standard type like ``int``, ``std::string``, or ``std::unique_ptr``, their move operations are all ``noexcept``, so there's nothing to worry about. But if you're storing custom types, it's best to ensure their move operations are ``noexcept``—the simplest way is to add ``static_assert`` to the queue's template constraints, letting the compiler enforce this for you:

```cpp
static_assert(std::is_nothrow_move_constructible_v<T>,
              "T must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<T>,
              "T must be nothrow move assignable");
```

This way, if you accidentally store a type that can throw, the compiler will catch it at compile time, rather than crashing at runtime on some obscure code path.

By the way, ``wait`` itself is reliable in terms of exception safety. The C++ standard guarantees that if ``wait`` receives a signal while waiting but the predicate is still ``false`` (a spurious wakeup), it will re-wait without leaking the lock. If ``wait`` exits due to an exception (an extreme case), the lock is correctly released. So we don't need to worry extra about the exception safety of the condition variable's ``wait``.

## Complete Implementation: Putting It All Together

At this point, we've discussed the shutdown mechanism, timed operations, stop_token integration, and backpressure strategies. Now let's integrate all of these features together and present a complete, ready-to-use ``BoundedBlockingQueue``:

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

    // === 基本操作 ===

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

    // === 超时操作 ===

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

    // === stop_token 可取消操作 (C++20) ===

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

    // === 背压策略 ===

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

    // === 管理 ===

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
    std::condition_variable_any cv_any_;  // 给 stop_token 用的
};
```

You might notice that here we maintain both ``not_full_`` and ``not_empty_`` (``condition_variable``), as well as ``cv_any_`` (``condition_variable_any``). The basic ``push``/``pop`` use the former (more efficient), while the ``stop_token`` version of ``pop`` uses the latter (supports stop_token). This is a practical compromise: code that doesn't need stop_token takes the high-performance path, and code that needs stop_token takes the general-purpose path. The best of both worlds, each taking what it needs.

## Summary

In this article, we started from the teaching-version ``BoundedQueue`` in the condition_variable article and step by step transformed it into a production-grade ``BoundedBlockingQueue``. We added four key capabilities in sequence: a shutdown mechanism (``close()`` rejecting new pushes, allowing drain pops), timed try_push/try_pop (``wait_for`` for non-blocking attempts), stop_token integration (the C++20 overload of ``condition_variable_any`` for cooperative cancellation), and backpressure strategies (``push_or_drop`` providing a non-blocking drop mode).

None of these capabilities exist in isolation—the shutdown mechanism relies on ``notify_all`` to wake all waiting threads, timed operations rely on the ``QueueResult`` enum to distinguish failure reasons, and the stop_token version of pop needs to work with ``close()`` to achieve a complete graceful exit. These designs, combined together, form a thread-safe queue that can be used directly in real-world projects.

Of course, this queue still has performance bottlenecks under high-contention scenarios—all threads share a single mutex, so throughput can't scale. In the next article, we'll discuss strategies like sharded locks, fine-grained locks, and copy-on-write to reduce contention, with the core idea being "make fewer threads fight over the same lock."

## Exercises

### Exercise 1: Bounded Blocking Queue with Shutdown Test

Write a multi-threaded test to verify the correctness of the shutdown mechanism: start 3 producer threads and 2 consumer threads. Each producer pushes 100 elements, and each consumer pops until it receives ``kClosed``. After all producers finish, call ``close()``, and verify that the consumers end up having consumed exactly 300 elements (no losses, no duplicates), and that all threads exit normally.

Hint: Use an ``std::atomic<int>`` to count the total number of elements fetched by consumers, and after all threads join, check that it equals 300.

### Exercise 2: Correctness Verification of Timed Pop

Create a queue with a capacity of 5 and don't push any elements. Start a consumer thread that calls ``try_pop`` with a 200ms timeout, and verify that it returns ``kTimeout``. Then push one element into the queue and call ``try_pop`` with a 200ms timeout again, verifying that it returns ``kSuccess``. Use ``std::chrono`` to measure the actual elapsed time of both operations, confirming that the wait time of the timed version is within the expected range.

### Exercise 3: Canceling Pop with stop_token

Use ``std::jthread`` to create a consumer, passing in the ``stop_token`` version of ``pop``. Have the main thread sleep for 100ms and then call ``request_stop()``, verifying that the consumer thread is awakened in ``pop`` and exits normally. Then try a different order: first ``close()`` the queue, then ``request_stop()``, and observe the consumer's behavior—if there are still elements in the queue, the consumer should drain them all before exiting.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit ``code/volumn_codes/vol5/ch04-concurrent-data-structures/``.

## References

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable_any -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any)
- [std::stop_token -- cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [std::jthread -- cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4 & 6](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [Why does std::condition_variable not support std::stop_token? -- StackOverflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
