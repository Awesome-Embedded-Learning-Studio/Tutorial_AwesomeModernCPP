---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the wait/notify mechanism of condition variables, and understand
  spurious wakeups, predicate usage, and lost wakeups.
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
title: Condition Variable and Wait Semantics
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/03-condition-variable.md
  source_hash: 2e432f7a88b1f27d0e6d65634b5e7d76b66348d35e6facc5930d470112af5246
  translated_at: '2026-06-24T01:07:09.765881+00:00'
  engine: anthropic
  token_count: 3155
---
# Condition Variables and Wait Semantics

In the previous post, we discussed mutexes and RAII locks—covering how to protect critical sections and how to avoid deadlocks. However, one problem remains unsolved: what if a thread needs to "wait for a specific condition to become true" before proceeding? Relying solely on a mutex isn't enough. The most naive approach is to write a loop that repeatedly locks, checks the condition, unlocks, and sleeps for a short while if the condition isn't met—this is known as **busy-waiting** or **polling**. While it works, it wastes CPU cycles, and tuning the "sleep duration" is difficult: too short wastes CPU, and too long results in sluggish responsiveness.

`std::condition_variable` is the standard library's answer. It provides a "wait-notify" mechanism: Thread A can **wait** on a condition variable, and Thread B can **notify** the condition variable after changing the state, waking up the waiting thread. This mechanism is far more efficient than polling because the waiting thread is suspended by the OS, consuming zero CPU time, and is only rescheduled upon notification. However, using condition variables comes with subtle pitfalls—spurious wakeups, lost wakeups, and predicate correctness—which are the real focus of this article.

## std::condition_variable vs. std::condition_variable_any

The C++ Standard Library provides two condition variable classes, defined in the `<condition_variable>` header. `std::condition_variable` is the primary choice and works exclusively with `std::unique_lock<std::mutex>`. `std::condition_variable_any` is a more generic version that can work with any lock type satisfying the *Lockable* requirement—such as `std::shared_lock` or custom lock wrappers. The trade-off is that `condition_variable_any` often has a heavier internal implementation (potentially using additional internal mutexes or dynamic allocation), so in most scenarios, we prioritize `std::condition_variable`. Unless stated otherwise, "condition variable" in this text refers to `std::condition_variable`.

The core API of a condition variable is quite concise, consisting of three groups of operations: the `wait` family (`wait`, `wait_for`, `wait_until`) for waiting for notifications, `notify_one()` to wake a single waiting thread, and `notify_all()` to wake all waiting threads. Let's break them down one by one.

## wait(): The Most Basic Wait

Let's look at a simple example. Suppose we have a boolean flag `ready`. The main thread sets it, and a worker thread waits for it to become `true`:

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock);  // 释放锁，进入等待；被唤醒时重新获取锁
    std::cout << "Worker: proceeding after wakeup\n";
    // lock 在此处析构时释放 mtx
}

int main()
{
    std::thread t(worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();

    t.join();
    return 0;
}
```

Let's break down a few key details here. First, the behavior of `cv.wait(lock)` happens in three steps: Step one, atomically release the mutex associated with `lock` and add the current thread to the condition variable's wait queue; Step two, the thread is suspended and enters a blocked state, consuming no CPU; Step three, when notified (or experiencing a spurious wakeup), the thread is rescheduled, reacquires the mutex, and `wait` returns. Note that "atomically releasing the mutex and joining the wait queue" is crucial—it guarantees there is no gap between releasing the mutex and starting to wait, so a notification cannot be missed during this window (we will discuss this in detail later).

Second, after `wait` returns, the current thread **holds the mutex again**. This means the caller of `wait` can safely access the shared state protected by the mutex after `wait` returns, without needing to lock again. This is also why `wait` requires a `unique_lock` instead of a raw mutex—the ownership of the `unique_lock` is transferred out and back in during `wait`, so the entire lifetime management is automatic.

However, the code above has a serious problem. Did you spot it? The worker thread continues execution immediately after `wait` returns, but it **never checks the value of `ready`**. What if this wakeup was spurious? What if the notification was sent before the worker called `wait`? The program's behavior becomes unpredictable. These are the two core issues we will discuss next.

## Spurious Wakeups: Why `wait` Must Be Used with a Predicate

A **spurious wakeup** refers to a thread returning from `wait` without receiving a `notify_one()` or `notify_all()` call. This is not a bug, nor a quality of implementation issue—both the POSIX standard and the C++ standard explicitly permit this behavior. Why? The reason lies in the underlying implementation of condition variables.

On Linux, `std::condition_variable` is implemented based on the `futex` (fast user-space mutex) system call. The internal state of a condition variable is typically tracked by an atomic counter to keep count of waiters and notifiers. To implement `wait` and `notify` efficiently, the implementation uses a "scatter-gather" strategy: `notify` only needs to increment the counter and wake one waiting futex, while `wait` must atomically decrement the counter and check for pending notifications. Under certain boundary conditions—for example, if a `notify_all` just woke up a batch of threads that haven't had time to recheck the internal state—the kernel might wake up extra threads. After weighing implementation efficiency against semantic strictness, the POSIX standard committee chose to allow spurious wakeups—this allows condition variables to be implemented with lighter-weight kernel primitives without requiring precise one-to-one mapping for every notification.

The practical consequence is: if you write `cv.wait(lock)` and `wait` returns, you **cannot assume** someone called `notify`. You must recheck the waiting condition after `wait` returns. The standard practice is to put `wait` inside a `while` loop:

```cpp
std::unique_lock<std::mutex> lock(mtx);
while (!ready) {
    cv.wait(lock);
}
// ready == true，安全地继续执行
```

The logic of this code is: check the condition first, and if it is not met, `wait`. After `wait` returns, check again, looping until the condition holds. This makes spurious wakeups harmless—even if a spurious wakeup occurs, the loop checks `ready` again, finds it is still `false`, and continues to `wait`.

The C++ standard library encapsulates this pattern into a more convenient overload: **`wait` with a predicate**:

```cpp
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, [] { return ready; });
// 这里 ready 一定为 true
```

The semantics of `cv.wait(lock, pred)` are equivalent to `while (!pred()) { cv.wait(lock); }`, but it can be more efficient than a handwritten loop—because the standard allows implementations to use optimized waiting strategies on certain platforms (such as using the bit-aware functionality of `futex` on Linux). To summarize in one sentence: **Always use the predicate overload of `wait`, and never use the version without one**. This is not advice; it is a rule.

Looking back at our previous example, the correct implementation should look like this:

```cpp
void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });
    // 到达这里时，ready 一定为 true，且 lock 被持有
    std::cout << "Worker: proceeding after condition met\n";
}
```

## Lost Wakeup: The Disaster of Notifying Before Waiting

Spurious wakeup means "waking up without a notification," whereas **lost wakeup** is the exact opposite—"a notification was sent, but no one received it." This occurs because the notification is sent before the `wait` call.

Let's construct a scenario where a wakeup is lost:

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker()
{
    // 假设 worker 线程在这里被调度延迟了
    // 主线程先执行了 notify_one()
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::unique_lock<std::mutex> lock(mtx);
    // 如果这里用不带谓词的 wait，就会永远阻塞！
    cv.wait(lock, [] { return ready; });
    std::cout << "Worker: condition met\n";
}

int main()
{
    std::thread t(worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();  // 此时 worker 还没开始 wait

    t.join();  // 等待 worker（带谓词版本不会死锁）
    return 0;
}
```

In this example, the main thread calls `notify_one()` before the `worker` calls `wait`. If we were using the raw `wait(lock)` without a predicate, this notification would be lost forever—the condition variable does not "store" notifications for later retrieval. However, because we used the predicate version `wait(lock, []{ return ready; })`, the worker thread checks the value of `ready` (which is now `true`) immediately upon waking and proceeds without requiring any further notification. This is another major advantage of the predicate-based `wait`: it guards against both spurious wakeups and missed wakeups.

However, the more fundamental strategy to prevent missed wakeups is to ensure that the "check condition-wait" and "modify condition-notify" sequences are protected by the **same mutex**. When the waiting thread holds the mutex to check the condition, the notifying thread cannot simultaneously modify the condition. Conversely, when the notifying thread holds the mutex to modify the condition, the waiting thread cannot have already passed the condition check without having started `wait`. This is why `wait` requires a `unique_lock` to be passed—it is not just to release the lock during the wait, but to ensure the synchronization relationship between waiting and notification.

## wait_for() and wait_until(): Timed Waits

Sometimes we do not want to wait indefinitely—for example, in cases of network request timeouts, user cancellation, or periodic status checks. `wait_for` and `wait_until` provide semantics for waiting with a timeout.

`wait_for(lock, duration, pred)` waits for a specified duration. `wait_until(lock, time_point, pred)` waits until a specified time point. Both support predicate and non-predicate versions (again, prefer the predicate version). The predicate version returns `bool`, indicating whether the predicate is `true` (this could be due to a notification or a timeout, but it returns `true` only if the predicate evaluates to `true`). The non-predicate version returns `std::cv_status`, which can be `no_timeout` (notified or spurious wakeup) or `timeout` (timed out).

Let's look at a practical example: we want to wait for a task to complete, but only for a maximum of five seconds:

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

std::mutex mtx;
std::condition_variable cv;
bool task_done = false;

void long_task()
{
    std::this_thread::sleep_for(std::chrono::seconds(3));  // 模拟耗时操作
    {
        std::lock_guard<std::mutex> lock(mtx);
        task_done = true;
    }
    cv.notify_one();
}

int main()
{
    std::thread t(long_task);

    std::unique_lock<std::mutex> lock(mtx);
    bool success = cv.wait_for(lock, std::chrono::seconds(5),
                                [] { return task_done; });

    if (success) {
        std::cout << "Task completed within timeout\n";
    } else {
        std::cout << "Task timed out after 5 seconds\n";
        // 注意：t 还在运行，需要决定如何处理
    }

    lock.unlock();
    t.join();  // 无论超时与否，最终都要 join
    return 0;
}
```

The predicate version of `wait_for` is essentially implemented as a loop: every time it wakes up (whether due to a notification or a spurious wakeup), it checks the predicate. If the predicate is `true`, it returns `true`; if a timeout occurs and the predicate is still `false`, it returns `false`. Note that returning `false` does not imply that a notification will never arrive—it simply means the condition was not met within the specified duration. You need to design the logic for handling timeouts based on your specific business requirements.

The usage of `wait_until` is similar, except that it accepts an absolute time point (a `time_point` type from `std::chrono`), rather than a relative duration. This is more convenient for scenarios where you need to "complete before a specific deadline"—you don't need to calculate `now + duration` yourself; just pass the deadline directly. However, be aware that system clock adjustments can affect the accuracy of `system_clock`, so if you care about monotonicity, prefer using `steady_clock`.

## Producer-Consumer Pattern: Bounded Queue

The most classic application scenario for condition variables is the Producer-Consumer Pattern. Let's implement a complete bounded blocking queue—producers push data into the queue and block if it is full, while consumers fetch data and block if it is empty. This example combines the use of mutexes, the condition variable wait-notify mechanism, and predicate predicates.

First, let's define the basic structure of the queue:

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <thread>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity)
    {}

    // 生产者调用：向队列放入元素，满了就阻塞等待
    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    // 消费者调用：从队列取出元素，空了就阻塞等待
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
    std::condition_variable not_full_;   // 队列不满时通知生产者
    std::condition_variable not_empty_;  // 队列不空时通知消费者
};
```

Let's break down this implementation step by step. Internally, the queue maintains two condition variables: `not_full_` for the producer to wait (waiting when full, notified when a consumer consumes), and `not_empty_` for the consumer to wait (waiting when empty, notified when a producer produces). This design with two condition variables is more precise than using a single one—it avoids unnecessary wakeups: producers only wake up consumers (via `not_empty_`), and consumers only wake up producers (via `not_full_`), keeping concerns separate.

The logic for the `push` method is: first acquire the mutex, then wait with a predicate for the queue to not be full. When `wait` returns, we are guaranteed that `queue_.size() < capacity_` (because the predicate is `true`), so we can safely push. After pushing, we call `not_empty_.notify_one()` to notify one waiting consumer. The logic for `pop` is symmetrical: wait for the queue to be non-empty, extract the element, and notify the producer.

Note that the lock is still held when we call notify in both `push` and `pop`. This is fine, and is sometimes even an optimization. The notify operation itself does not wait for a response; it simply moves threads from the condition variable's wait queue to the mutex's wait queue. The awakened thread can only acquire the lock and continue execution after the current thread releases the lock (when the `unique_lock` destructor runs). Therefore, holding the lock during notification makes no difference to correctness, but on some platforms, notifying while holding the lock can reduce one unnecessary context switch.

Now, let's use this queue:

```cpp
int main()
{
    constexpr std::size_t kQueueCapacity = 10;
    BoundedQueue<int> queue(kQueueCapacity);

    // 生产者线程
    std::thread producer([&queue]() {
        for (int i = 1; i <= 20; ++i) {
            queue.push(i);
            std::cout << "Produced: " << i << "\n";
        }
    });

    // 消费者线程
    std::thread consumer([&queue]() {
        for (int i = 1; i <= 20; ++i) {
            int value = queue.pop();
            std::cout << "Consumed: " << value << "\n";
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

The queue capacity is 10, and the producer needs to generate 20 elements, so it will inevitably become full—the producer blocks at the 11th element and can only continue after the consumer has removed an element. The consumer's pace depends on the producer's output speed—if the producer can't keep up, the consumer waits in `pop`. The two threads coordinate their pace through the condition variable in this way.

## Choosing Between `notify_all` and `notify_one`

In the bounded queue example above, we used `notify_one()`—which wakes only one waiting thread at a time. However, in certain scenarios, we need `notify_all()` to wake all waiting threads. The choice depends on the "nature of the condition change."

`notify_one()` is suitable for scenarios where "each notification allows only one thread to proceed." The producer-consumer queue is a typical example—each `push` only needs to wake one consumer to fetch the item. Waking multiple consumers is meaningless (since only one element is available, the others would find nothing and go back to sleep). The advantage of `notify_one()` is reducing unnecessary wakeups: it only wakes one thread, while the others continue to sleep, saving the overhead of context switches.

`notify_all()` is suitable for scenarios where "a condition change might satisfy the condition for multiple waiting threads simultaneously." A classic example is **thread pool shutdown**: when you set a `shutdown` flag and call `notify_all()`, all threads waiting for tasks need to wake up to check this flag and then exit. Another example is the **barrier pattern**—where all threads need to wait for a certain condition to be met before proceeding together, so everyone must be notified when the condition changes.

A common misconception is that `notify_all` is always safe, so one should always use it. It is true that `notify_all` is no less "correct" than `notify_one`—all waiting threads will eventually wake up and check the condition. However, the performance difference is significant: if 10 threads are waiting, `notify_all` wakes all 10. They will then compete for the same mutex, but ultimately only 1 will acquire the lock and pass the condition check, while the other 9 made a wasted trip. Therefore, "use `notify_one` if you can, avoid `notify_all`" is a reasonable performance optimization principle—provided you are sure the notification only relates to one waiting thread.

## `std::condition_variable_any`: Generic Condition Variables

So far, we have been using `std::condition_variable`, which only accepts `std::unique_lock<std::mutex>`. However, sometimes we might need to pair it with other lock types—such as `std::shared_lock<std::shared_mutex>` (which we will cover in detail in the next article). This is where `std::condition_variable_any` comes in.

Its interface is completely consistent with `std::condition_variable`, except that the templated `wait` can accept any lock that satisfies the Lockable requirement. There is almost no learning curve; you can simply replace `condition_variable` with `condition_variable_any`. What's the cost? Its internal implementation usually requires an additional mutex to protect the internal wait queue (because `condition_variable` can leverage the internal structure of `unique_lock<mutex>` for optimization, whereas `condition_variable_any` doesn't know the internal implementation of the external lock). Consequently, its performance is slightly inferior. If your scenario only requires `unique_lock<std::mutex>`, you should stick with `condition_variable`.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Thread-Safe Countdown Timer

Implement a `CountdownEvent` class that behaves like C#'s `ManualResetEvent` or Java's `CountDownLatch`. It has an internal counter initialized to N. Threads can call `wait()` to block until the counter reaches zero, while other threads call `signal()` to decrement the counter by 1. When the counter reaches 0, all waiting threads should be woken up.

Requirements:

- Use `std::mutex` and `std::condition_variable`.
- Use the predicate version of `wait()`.
- In `signal()`, consider whether to use `notify_one()` or `notify_all()`.

Hint: The moment the counter changes from 1 to 0, the condition is satisfied for all threads blocked on `wait()` simultaneously—this is a typical scenario for `notify_all()`.

### Exercise 2: Extend Bounded Queue with `try_pop_for`

Based on the `BoundedQueue` in this article, add a `try_pop_for(duration)` method: attempt to pop an element from the queue within a specified time. If successful before the timeout, return `std::optional<T>` containing the value; if it times out, return `std::nullopt`.

Hint: Use the predicate version of `wait_for` and check the return value to determine if it was a timeout or success. Note whether the thread is safe after a timeout return—since `optional`'s `nullopt` explicitly tells the caller "nothing was retrieved," the caller can decide whether to retry or give up.

### Exercise 3: Reproduce a Lost Wakeup

Write a program that intentionally constructs a "notify before wait" timing. Use `wait` without a predicate and observe if the program blocks permanently (it likely will, depending on scheduling). Then, add the predicate to `wait` and confirm that even if the notification is sent first, the program exits normally. The purpose of this exercise is to let you experience the danger of lost wakeups firsthand, and understand why the predicate `wait` is essential.

## References

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable::wait -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [Condition variable -- Wikipedia (POSIX standard discussion on spurious wakeups)](https://en.wikipedia.org/wiki/Monitor_(synchronization)#Condition_variables)
- [Why do spurious wakeups happen? -- StackOverflow](https://stackoverflow.com/questions/8594591/why-does-pthreads-cond-wait-have-spurious-wakeups)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
