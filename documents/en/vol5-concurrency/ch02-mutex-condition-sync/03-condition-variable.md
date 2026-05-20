---
title: condition_variable and Wait Semantics
description: Master the wait/notify mechanism of condition variables, and understand
  spurious wakeups, predicate patterns, and lost wakeups.
chapter: 2
order: 3
tags:
- host
- cpp-modern
- intermediate
- mutex
- 异步编程
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- mutex 与 RAII 锁
related:
- 读写锁与 shared_mutex
- 线程安全队列
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/03-condition-variable.md
  source_hash: 55180d919c132785b2b2ff56ff781820c74bc4d65ba0ea80520055cc1e8f4327
  translated_at: '2026-05-20T04:36:55.732540+00:00'
  engine: anthropic
  token_count: 3161
---
# condition_variable and Wait Semantics

In the previous article, we discussed mutex and RAII locks—covering how to protect a critical section and how to avoid dead lock. But one problem remains unsolved: if a thread needs to "wait for a condition to become true" before continuing, how do we achieve this with just a mutex? The most naive approach is to write a loop that repeatedly locks, checks the condition, unlocks, and sleeps for a short while before trying again—this is known as **busy-wait** or **polling**. It works, but the cost is wasted CPU cycles, and the "sleep duration" parameter is hard to tune: too short wastes CPU, too long makes the response sluggish.

`std::condition_variable` is the standard library's answer. It provides a "wait-notify" mechanism: Thread A can **wait** on a condition variable, and Thread B can **notify** the condition variable after changing the condition, waking up the waiting thread. This mechanism is far more efficient than polling because the waiting thread is suspended by the operating system, consuming no CPU time, until it is notified and rescheduled. However, using condition variables comes with some very subtle pitfalls—spurious wakeups, lost wakeups, and predicate writing—these are the real focus of this article.

## std::condition_variable and std::condition_variable_any

The C++ standard library provides two condition variable classes, defined in the `<condition_variable>` header. `std::condition_variable` is the primary one; it can only be used with `std::unique_lock<std::mutex>`. `std::condition_variable_any` is a more general version that can work with any lock type satisfying the Lockable requirements—such as `std::shared_mutex` or a custom lock wrapper. The trade-off is that `std::condition_variable_any`'s internal implementation is typically heavier (possibly using an additional internal mutex or dynamic allocation), so in most scenarios we prefer `std::condition_variable`. Unless otherwise stated, "condition variable" in the rest of this article refers to `std::condition_variable`.

The core API of a condition variable is very concise, with only three groups of operations: the `wait` series (`wait`, `wait_for`, `wait_until`) for waiting on notifications, `notify_one` to wake up one waiting thread, and `notify_all` to wake up all waiting threads. Let's break them down one by one.

## wait(): The Most Basic Wait

Let's start with the simplest example. Suppose we have a flag `ready`, which the main thread sets and the worker thread waits to become `true`:

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

There are a few key details to unpack here. First, the behavior of `cv.wait(lock)` happens in three steps: Step one, atomically release the mutex associated with `lock` and add the current thread to the condition variable's wait queue; Step two, the thread is suspended and enters a blocked state, consuming no CPU; Step three, when notified (or spuriously woken up), the thread is rescheduled, re-acquires the mutex, and `wait` returns. Note that "atomically releasing the mutex and joining the wait queue" is crucial—it guarantees there is no gap between releasing the mutex and starting to wait, so a notification cannot be missed in that gap (we will discuss this in detail later).

Second, after `wait` returns, the current thread **holds the mutex again**. This means the caller of `cv.wait(lock)` can safely access the shared state protected by the mutex after `wait` returns, without needing to lock again. This is also why `wait` requires a `std::unique_lock<std::mutex>` to be passed in rather than a bare mutex—the ownership of `lock` is transferred out and then transferred back during `wait`, and the entire lifetime management is automatic.

But the code above has a serious problem. Did you spot it? The worker thread continues executing directly after `wait` returns, but it **completely fails to check the value of `ready`**. What if this wakeup is spurious? What if the notification was sent before the worker called `wait`? The program's behavior becomes unpredictable. These are the two core problems we will discuss next.

## Spurious Wakeups: Why wait Must Be Used with a Predicate

A **spurious wakeup** means a thread returns from `wait` without having received a `notify_one` or `notify_all` call. This is not a bug, nor a quality-of-implementation issue—both the POSIX standard and the C++ standard explicitly allow this behavior. Why? The reason lies in the underlying implementation of condition variables.

On Linux, `std::condition_variable` is implemented based on the `futex` (fast user-space mutex) system call. The internal state of a condition variable typically uses an atomic counter to track the number of waiters and notifiers. To efficiently implement `notify_one` and `notify_all`, the condition variable implementation adopts a "scatter-gather" strategy: `notify_one` only needs to increment the counter and wake one waiting futex, while `wait` needs to atomically decrement the counter and check for unprocessed notifications. Under certain boundary conditions—for example, if a `notify_all` just woke up a batch of threads that haven't had time to re-check the internal state—the kernel might wake up extra threads. After weighing implementation efficiency against semantic strictness, the POSIX standards committee chose to allow spurious wakeups—this way, condition variables can be implemented with lighter-weight kernel primitives without needing an exact one-to-one mapping for every notification.

The practical consequence is: if you call `wait` and it returns, you **cannot assume** someone called `notify_one`. You must re-check the wait condition after `wait` returns. The standard approach is to put `wait` inside a while loop:

```cpp
std::unique_lock<std::mutex> lock(mtx);
while (!ready) {
    cv.wait(lock);
}
// ready == true，安全地继续执行
```

The logic here is: check the condition first, and if it's not met, call `wait`; after `wait` returns, check again, looping until the condition is true. This makes spurious wakeups harmless—even if spuriously woken up, the loop will check `ready` again, find it's still `false`, and continue to `wait`.

The C++ standard library encapsulates this pattern into a more convenient overload: **`wait` with a predicate**:

```cpp
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, [] { return ready; });
// 这里 ready 一定为 true
```

The semantics of `wait(lock, pred)` are equivalent to `while (!pred()) wait(lock);`, but it can be more efficient than a hand-written loop—because the standard allows implementations to use more optimized waiting strategies on certain platforms (such as using the bit-aware features of `futex` on Linux). To sum it up in one sentence: **always use `wait` with a predicate, never use the version without one**. This is not a suggestion; it is a rule.

Looking back at our earlier example, the correct way to write it is:

```cpp
void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });
    // 到达这里时，ready 一定为 true，且 lock 被持有
    std::cout << "Worker: proceeding after condition met\n";
}
```

## Lost Wakeups: The Disaster of Notifying Before Waiting

Spurious wakeups mean "waking up without a notification," while a **lost wakeup** is the exact opposite—"notified but nobody received it." This happens when the notification is sent before `wait` is called.

Let's construct a lost wakeup scenario:

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

In this example, the main thread calls `notify_one` before the worker thread calls `cv.wait(lock)`. If we were using a bare `wait` without a predicate, this notification would be lost forever—the condition variable does not "store" notifications for you to pick up later. But because we used the predicate version `wait(lock, ...)`, the worker thread checks the value of `ready` (which is already `true`) upon waking up, passes the check directly, and doesn't need anyone to notify it. This is another huge advantage of `wait` with a predicate: it simultaneously guards against both spurious wakeups and lost wakeups.

However, a more fundamental strategy to prevent lost wakeups is to ensure that "check condition-wait" and "modify condition-notify" are protected by **the same mutex**. When the waiting thread holds the mutex to check the condition, the notifying thread cannot simultaneously modify the condition; conversely, when the notifying thread holds the mutex to modify the condition, the waiting thread cannot have already passed the condition check but not yet started `wait`. This is why `wait` requires a `std::unique_lock<std::mutex>` to be passed in—it's not just to release the lock during the wait, but more importantly to ensure the synchronization relationship between waiting and notifying.

## wait_for() and wait_until(): Timed Waits

Sometimes we don't want to wait indefinitely—such as a network request timeout, a user action cancellation, or a periodic state check scenario. `wait_for` and `wait_until` provide wait semantics with a timeout.

`wait_for` waits for a specified duration. `wait_until` waits until a specified time point. Both support a predicate version and a bare version (again, prefer the predicate version). The predicate version returns `bool`, indicating whether the predicate is `true` (it might have been notified or it might have timed out, but it only returns `true` if the predicate is `true`). The bare version returns a `cv_status`, which could be `no_timeout` (notified or spuriously woken up) or `timeout` (timed out).

Let's look at a practical example: we want to wait for a task to complete, but for at most 5 seconds:

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

The internal implementation of the predicate version of `wait_for` is essentially a loop: each time it wakes up (whether due to a notification or a spurious wakeup), it checks the predicate, and if it's `true`, it returns `true`; if it times out and the predicate is still `false`, it returns `false`. Note that returning `false` does not mean a notification will never arrive—it just means the condition was not satisfied within the specified time. The handling logic after a timeout needs to be designed according to your business requirements.

The usage of `wait_until` is similar, except it accepts an absolute time point (a template parameter of type `Clock::time_point` in `std::chrono`), rather than a relative duration. This is more convenient in scenarios where you need to "complete before a certain deadline"—you don't need to calculate `duration` yourself; just pass a deadline in directly. However, be aware that system clock adjustments might affect the accuracy of `wait_until`, so if you care about clock monotonicity, prefer `wait_for`.

## Producer-Consumer Pattern: Bounded Queue

The most classic application scenario for condition variables is the Producer-Consumer Pattern. Let's write a complete bounded blocking queue—producers push data into the queue and block when full; consumers pop data from the queue and block when empty. This example comprehensively uses the wait-notify mechanism and predicate writing of mutex and condition_variable.

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

Let's break down this implementation step by step. The queue internally maintains two condition variables: `not_full` for producers to wait on (wait when full, notify when someone consumes), and `not_empty` for consumers to wait on (wait when empty, notify when someone produces). This dual-condition-variable design is more precise than a single condition variable—it avoids unnecessary wakeups: producers only wake consumers (`notify_one` on `not_empty`), and consumers only wake producers (`notify_one` on `not_full`), each managing their own.

The logic of the `push` method is: first acquire the mutex, then use `wait` with a predicate to wait until the queue is not full. When `wait` returns, we are guaranteed that the queue is not full (because the predicate is `true`), so we can safely push. After pushing, we call `notify_one` on `not_empty` to notify one waiting consumer. The logic of the `pop` method is symmetric: wait until the queue is not empty, take out the element, and notify the producer.

Note that in `push` and `pop`, the lock is still held when we call notify—this is fine, and sometimes it's even an optimization. Notify itself doesn't need to wait for a response from the other side; it simply moves threads from the condition variable's wait queue to the mutex's wait queue. The woken threads can only acquire the lock and continue executing after the current thread releases the lock (when `lock` is destructed). So whether you hold the lock during notify makes no difference for correctness, but on certain platforms, notifying while holding the lock can reduce an unnecessary context switch.

Now let's use this queue:

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

The queue capacity is 10, and the producer wants to produce 20 elements, so it will inevitably become full in the middle—the producer blocks at the 11th element and can only continue after the consumer has taken elements away. The consumer's pace depends on the producer's output speed—if the producer can't keep up, the consumer will wait in `pop`. The two threads coordinate their pacing through the condition variable like this.

## Choosing Between notify_all and notify_one

In the bounded queue example above, we used `notify_one`—waking up only one waiting thread each time. But in certain scenarios, we need `notify_all` to wake up all waiting threads. Which one to choose depends on "the nature of the condition change."

`notify_one` is suitable for scenarios where "each notification only lets one thread continue." The producer-consumer queue is a typical example—each push only needs to wake one consumer to take an item; waking multiple consumers is pointless (there's only one item to take, and the others would fail the check and go back to sleep). The advantage of `notify_one` is reducing unnecessary wakeups: only waking one thread while the others continue to sleep, saving the overhead of context switches.

`notify_all` is suitable for scenarios where "a condition change might satisfy the condition for multiple waiting threads simultaneously." A classic example is **thread pool shutdown**: when you set a `stop` flag and call `notify_all`, all threads waiting for tasks need to wake up, check this flag, and exit individually. Another example is the **barrier** pattern—all threads need to wait until a certain condition is true before continuing together, and when the condition changes, everyone needs to be notified.

A common misconception is that `notify_all` is always safe so we should always use it. Indeed, `notify_all` is no worse than `notify_one` in terms of correctness—all waiting threads will eventually wake up and check the condition. But the performance difference is significant: if 10 threads are waiting, `notify_all` will wake all 10, they will compete for the same mutex, and ultimately only 1 can acquire the lock and pass the condition check, while the other 9 made a wasted trip. Therefore, "use `notify_one` if you can, avoid `notify_all`" is a reasonable performance optimization principle—provided you are certain the notification is only related to one waiting thread.

## std::condition_variable_any: The Generic Condition Variable

So far we've been using `std::condition_variable`, which only accepts `std::unique_lock<std::mutex>`. But sometimes we might need to pair it with other lock types—such as `std::shared_mutex` (which we'll cover in detail in the next article). This is where `std::condition_variable_any` comes in.

Its interface is completely consistent with `std::condition_variable`, except the templated `wait` can accept any lock satisfying the Lockable requirements. There's almost no learning curve—just replace `std::condition_variable` with `std::condition_variable_any`. What's the trade-off? Its internal implementation typically requires an additional mutex to protect the internal wait queue (because `std::condition_variable` can leverage the internal structure of `std::mutex` for optimization, whereas `std::condition_variable_any` doesn't understand the internal implementation of external locks), so it's slightly inferior in performance. If your scenario only requires `std::unique_lock<std::mutex>`, stick with `std::condition_variable`.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Thread-Safe Countdown Latch

Implement a `CountdownLatch` class that behaves similarly to C#'s `CountdownEvent` or Java's `CountDownLatch`. It has an internal counter initialized to N. Threads can call `wait` to block until the counter reaches zero, while other threads call `count_down` to decrement the counter by one. When the counter reaches 0, all waiting threads should be woken up.

Requirements:

- Use `std::mutex` and `std::condition_variable`
- Use the predicate version of `wait`
- In `count_down`, consider whether to use `notify_one` or `notify_all`

Hint: The moment the counter changes from 1 to 0, the conditions of all threads blocked on `wait` are simultaneously satisfied—this is a typical scenario for `notify_all`.

### Exercise 2: Extend the Bounded Queue to Support try_pop_for

Building on the `BoundedQueue` in this article, add a `try_pop_for` method: attempt to pop an element from the queue within a specified time. If successfully popped before timeout, return an `std::optional` containing the value; if timed out, return an empty `std::optional`.

Hint: Use the predicate version of `wait_for`, and check the return value to determine if it timed out or succeeded. Note whether the thread is safe after a timeout return—because `try_pop_for`'s empty `std::optional` explicitly tells the caller "nothing was popped," the caller can decide whether to retry or give up.

### Exercise 3: Reproduce a Lost Wakeup

Write a program that deliberately constructs a timing where "notify happens before wait." Use `wait` without a predicate, and observe whether the program blocks permanently (it most likely will, depending on scheduling). Then add a predicate to `wait` and confirm that even if the notification is sent first, the program can exit normally. The purpose of this exercise is to let you personally experience the danger of lost wakeups, and why the predicate version of `wait` is mandatory.

## Reference Resources

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable::wait -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [Condition variable -- Wikipedia (POSIX standard discussion on spurious wakeups)](https://en.wikipedia.org/wiki/Monitor_(synchronization)#Condition_variables)
- [Why do spurious wakeups happen? -- StackOverflow](https://stackoverflow.com/questions/8594591/why-does-pthreads-cond-wait-have-spurious-wakeups)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
