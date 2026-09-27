---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the wait/notify mechanism of condition variables, and understand spurious wakeups, predicate-based waiting, and lost wakeups
difficulty: intermediate
order: 3
platform: host
prerequisites:
- mutex and RAII Locks
reading_time_minutes: 18
related:
- Reader-Writer Locks and shared_mutex
- Thread-Safe Queue
tags:
- host
- cpp-modern
- intermediate
- mutex
- 异步编程
title: condition_variable and Wait Semantics
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/03-condition-variable.md
  source_hash: 2e432f7a88b1f27d0e6d65634b5e7d76b66348d35e6facc5930d470112af5246
  translated_at: '2026-09-26T07:03:14+00:00'
  engine: anthropic
  token_count: 9300
---
# condition_variable and Wait Semantics

In the previous article we talked about mutexes and RAII locks—how to protect a critical section, how to stay out of deadlock. But one problem was left unsolved: what if a thread needs to "wait for some condition to become true" before it can proceed? How do you do that with a mutex alone? The most naive idea is to write a loop that repeatedly locks, checks the condition, and—if it doesn't hold—unlocks, sleeps for a short while, and tries again. That is what we call **busy-waiting** or **polling**. It works, but it burns CPU cycles for nothing, and the "how long to sleep" parameter is notoriously hard to tune: sleep too short and you waste CPU, sleep too long and the response turns sluggish.

`std::condition_variable` is the standard library's answer. It provides a wait-notify mechanism: thread A can **wait** on a condition variable, and thread B, after changing the condition, can **notify** the condition variable to wake up the waiting thread. This mechanism is far more efficient than polling, because a waiting thread is suspended by the operating system and consumes no CPU time—it only gets rescheduled once it is notified. That said, using condition variables comes with some genuinely subtle traps—spurious wakeups, lost wakeups, predicate-based waiting—and those are the real focus of this article.

## std::condition_variable and std::condition_variable_any

The C++ standard library provides two condition variable classes, both defined in the `<condition_variable>` header. `std::condition_variable` is the workhorse: it can only be paired with `std::unique_lock<std::mutex>`. `std::condition_variable_any` is a more general version that pairs with any lock type satisfying the Lockable requirements—for example `std::shared_lock` or a custom lock wrapper. The price is that `condition_variable_any` is usually heavier under the hood (it may use an additional internal mutex or dynamic allocation), so in most situations we reach for `std::condition_variable` first. In the rest of this article, unless stated otherwise, "condition variable" always refers to `std::condition_variable`.

The core API of a condition variable is remarkably lean—just three groups of operations: the `wait` family (`wait`, `wait_for`, `wait_until`) for waiting on notifications, `notify_one()` to wake up one waiting thread, and `notify_all()` to wake up all waiting threads. Let's take them apart one by one.

## wait(): The Most Basic Wait

Let's start with the simplest example. Suppose we have a flag `ready`: the main thread sets it, and a worker thread waits for it to become `true`:

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
    cv.wait(lock);  // releases the lock and enters the wait; reacquires the lock upon wakeup
    std::cout << "Worker: proceeding after wakeup\n";
    // lock releases mtx when it is destroyed here
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

There are several key details to unpack here. First, `cv.wait(lock)` behaves in three steps: step one, it atomically releases the mutex associated with `lock` and adds the current thread to the condition variable's wait queue; step two, the thread is suspended and blocks, consuming no CPU; step three, when a notification arrives (or a spurious wakeup happens), the thread is rescheduled, reacquires the mutex, and only then does `wait` return. Note how important that "atomically release the mutex and join the wait queue" part is—it guarantees there is no gap between releasing the mutex and starting to wait, so a notification cannot slip through that gap and be missed (we'll come back to this in detail later).

Second, after `wait` returns, the calling thread **owns the mutex again**. This means the caller of `wait` can safely access the mutex-protected shared state after `wait` returns, without any extra locking. It is also why `wait` demands a `unique_lock` rather than a bare mutex—the ownership of the `unique_lock` is transferred out and then back during the `wait`, and the whole lifetime is managed automatically.

But the code above has a serious problem. Did you spot it? The worker thread barrels straight on after `wait` returns, yet it **never checks the value of `ready`**. What if this wakeup was spurious? What if the notification was sent before the worker even called `wait`? The program's behavior becomes unpredictable. These are exactly the two core problems we take up next.

## Spurious Wakeups: Why wait Must Be Used with a Predicate

A **spurious wakeup** is when a thread returns from `wait` without any call to `notify_one()` or `notify_all()` having happened. This is not a bug, and not a quality problem of the implementation—both the POSIX standard and the C++ standard explicitly permit this behavior. Why? The answer lies in how condition variables are implemented underneath.

On Linux, `std::condition_variable` is built on the `futex` (fast user-space mutex) system call. The internal state of a condition variable typically uses an atomic counter to track the number of waiters and notifications. To implement `wait` and `notify` efficiently, the implementation follows a "scatter-gather" strategy: `notify` only needs to increment the counter and wake one waiting futex, while `wait` has to atomically decrement the counter and check for outstanding notifications. Under certain boundary conditions—for instance, right after a `notify_all` has woken a batch of threads that have not yet rechecked the internal state—the kernel may wake up a few more threads than strictly necessary. After weighing implementation efficiency against semantic strictness, the POSIX standards committee chose to allow spurious wakeups—this way a condition variable can be built on lighter kernel primitives, without paying for an exact one-to-one mapping on every notification.

The practical consequence: if you wrote `cv.wait(lock)` and `wait` returned, you **cannot assume** that anyone called `notify`. You must recheck the wait condition after `wait` returns. The canonical approach is to put `wait` inside a while loop:

```cpp
std::unique_lock<std::mutex> lock(mtx);
while (!ready) {
    cv.wait(lock);
}
// ready == true, safe to proceed
```

The logic of this code: check the condition first; if it doesn't hold, `wait`; when `wait` returns, check again; keep looping until the condition holds. Now spurious wakeups are harmless—even if the thread wakes up spuriously, the loop rechecks `ready`, sees that it is still `false`, and goes back to waiting.

The C++ standard library wraps this pattern into a more convenient overload: **`wait` with a predicate**:

```cpp
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, [] { return ready; });
// here ready is guaranteed to be true
```

The semantics of `cv.wait(lock, pred)` are equivalent to `while (!pred()) { cv.wait(lock); }`, but it can be more efficient than a hand-written loop—because the standard allows implementations to use a more optimized waiting strategy on some platforms (for example, the bit-aware futex features on Linux). One-sentence summary: **always use the predicate version of `wait`, and never the predicate-less one**. That is not a suggestion; it is a rule.

Looking back at our earlier example, the correct version looks like this:

```cpp
void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });
    // when we get here, ready is guaranteed to be true and lock is held
    std::cout << "Worker: proceeding after condition met\n";
}
```

## Lost Wakeups: The Disaster of Notifying Before Waiting

Spurious wakeups are about waking up without a notification; a **lost wakeup** is the opposite—a notification was sent, but nobody received it. It happens when the notification goes out before the `wait` does.

Let's construct a lost-wakeup scenario:

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
    // suppose the worker thread gets delayed by the scheduler here
    // and the main thread has already executed notify_one()
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::unique_lock<std::mutex> lock(mtx);
    // a predicate-less wait here would block forever!
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
    cv.notify_one();  // at this point the worker hasn't started waiting yet

    t.join();  // wait for the worker (the predicate version won't deadlock)
    return 0;
}
```

In this example, the main thread calls `notify_one()` before the worker calls `wait`. With a bare, predicate-less `wait(lock)`, that notification is lost forever—a condition variable does not "store" notifications for you to collect later. But because we used the predicate version `wait(lock, []{ return ready; })`, the worker thread wakes up, checks the value of `ready` (which is already `true` by then), and sails straight through without anyone notifying it. That is the other huge advantage of predicate-based `wait`: it defends against both spurious wakeups and lost wakeups.

The more fundamental defense against lost wakeups, though, is to make sure that "check condition—then wait" and "modify condition—then notify" are protected by **the same mutex**. While the waiting thread holds the mutex and checks the condition, the notifying thread cannot modify the condition at the same time; conversely, while the notifying thread holds the mutex and modifies the condition, the waiting thread cannot have already passed the condition check without having started `wait`. That is why `wait` takes a `unique_lock`—not merely so the lock can be released during the wait, but to guarantee the synchronization relationship between waiting and notifying.

## wait_for() and wait_until(): Waiting with Timeouts

Sometimes we don't want to wait indefinitely—a network request running out of time, a user cancelling an operation, a periodic status check. `wait_for` and `wait_until` provide wait semantics with timeouts.

`wait_for(lock, duration, pred)` waits for a specified length of time. `wait_until(lock, time_point, pred)` waits until a specified time point. Both come in predicate and bare versions (and again, prefer the predicate version). The predicate version returns a `bool` telling whether the predicate became `true` (the return from waiting may have come from a notification or from a timeout, but it returns `true` only when the predicate holds). The predicate-less version returns `std::cv_status`, which is either `no_timeout` (notified or spuriously woken) or `timeout` (the wait timed out).

Here's a practical example: we want to wait for a task to finish, but at most 5 seconds:

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
    std::this_thread::sleep_for(std::chrono::seconds(3));  // simulate a time-consuming operation
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
        // note: t is still running; you need to decide how to handle it
    }

    lock.unlock();
    t.join();  // timeout or not, we must eventually join
    return 0;
}
```

Internally, the predicate version of `wait_for` is essentially a loop: every time the thread wakes (whether from a notification or a spurious wakeup), it checks the predicate and returns `true` if it holds; if the time runs out and the predicate is still `false`, it returns `false`. Note that a `false` return does not mean no notification will ever arrive—it only means the condition was not satisfied within the allotted time. What to do after a timeout is something you design around your own requirements.

`wait_until` works the same way, except that it takes an absolute time point (a `time_point` type from `std::chrono` as the template parameter) instead of a relative duration. That is more convenient for "finish before this deadline" scenarios—you don't have to compute `now + duration` yourself; just pass the deadline in. One caveat: adjustments to the system clock can affect the precision of `system_clock`, so if you care about monotonic time, prefer `steady_clock`.

## The Producer-Consumer Pattern: A Bounded Queue

The most classic application of condition variables is the producer-consumer pattern. Let's write a complete bounded blocking queue—producers push data into the queue and block when it is full; consumers take data out of the queue and block when it is empty. This example puts together mutexes, the wait-notify mechanism of condition variables, and predicate-based waiting.

First, the basic structure of the queue:

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

    // called by producers: put an element into the queue, blocking while it is full
    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    // called by consumers: take an element from the queue, blocking while it is empty
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
    std::condition_variable not_full_;   // notifies producers when the queue is not full
    std::condition_variable not_empty_;  // notifies consumers when the queue is not empty
};
```

Let's walk through this implementation step by step. The queue maintains two condition variables internally: `not_full_` is where producers wait (they wait while the queue is full and get notified once someone consumes), and `not_empty_` is where consumers wait (they wait while the queue is empty and get notified once someone produces). This two-condition-variable design is more precise than a single condition variable—it avoids unnecessary wakeups: producers wake only consumers (`not_empty_`), consumers wake only producers (`not_full_`), each minding its own side.

The logic of `push` is: acquire the mutex, then use the predicate version of `wait` to wait until the queue is not full. When `wait` returns we are guaranteed `queue_.size() < capacity_` (because the predicate holds), so we can push safely. After the push, call `not_empty_.notify_one()` to wake one waiting consumer. The logic of `pop` is symmetric: wait until the queue is not empty, take the element out, notify a producer.

Notice that `push` and `pop` still hold the lock while notifying—and that is not a problem; sometimes it is even an optimization. `notify` itself does not wait for anyone to respond; it merely moves a thread from the condition variable's wait queue onto the mutex's wait queue. Only after the current thread releases the lock (when the `unique_lock` is destroyed) can the woken thread acquire the lock and carry on. So whether you hold the lock while notifying makes no difference for correctness, but on some platforms notifying under the lock can save one unnecessary context switch.

Now let's put the queue to work:

```cpp
int main()
{
    constexpr std::size_t kQueueCapacity = 10;
    BoundedQueue<int> queue(kQueueCapacity);

    // producer thread
    std::thread producer([&queue]() {
        for (int i = 1; i <= 20; ++i) {
            queue.push(i);
            std::cout << "Produced: " << i << "\n";
        }
    });

    // consumer thread
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

The queue capacity is 10 and the producer wants to produce 20 elements, so it is bound to fill up along the way—the producer blocks on the 11th element and can only continue after the consumer takes one out. The consumer's pace depends on how fast the producer delivers—if the producer cannot keep up, the consumer waits inside `pop`. And that is how the two threads coordinate their rhythm, through the condition variables.

## Choosing Between notify_all and notify_one

In the bounded queue example above we used `notify_one()`—waking exactly one waiting thread each time. But in some scenarios we need `notify_all()` to wake every waiting thread. Which one to pick depends on the nature of the condition change.

`notify_one()` fits scenarios where "each notification lets exactly one thread proceed". The producer-consumer queue is the canonical example—each push only needs to wake one consumer to take the item; waking several consumers would be pointless (there is only one element to take, so the rest would find nothing and go back to waiting). The advantage of `notify_one()` is fewer useless wakeups: only one thread gets up while the others keep sleeping, saving the overhead of context switches.

`notify_all()` fits scenarios where "a change in the condition may let several waiting threads satisfy the condition at once". A classic example is **thread pool shutdown**: once you set a `shutdown` flag and call `notify_all()`, every thread waiting for tasks needs to wake up, check the flag, and exit on its own. Another example is the **barrier** pattern—all threads wait for some condition to hold and then continue together, so the change has to reach everyone.

A common misconception is that `notify_all` is always safe, so you might as well always use it. It is true that `notify_all` is never worse than `notify_one` in terms of correctness—all waiting threads eventually wake up and recheck the condition. But the performance difference is significant: if 10 threads are waiting, `notify_all` wakes all 10; they all contend for the same mutex, only 1 of them gets the lock and passes the condition check, and the other 9 make a wasted trip. So "use `notify_one` rather than `notify_all` whenever you can" is a sound performance principle—provided you are sure the notification concerns only one waiting thread.

## std::condition_variable_any: The General-Purpose Condition Variable

So far we have been using `std::condition_variable`, which only accepts `std::unique_lock<std::mutex>`. But sometimes we need to pair with a different lock type—for instance `std::shared_lock<std::shared_mutex>` (the subject of the next article). That is where `std::condition_variable_any` comes in.

Its interface is exactly the same as `std::condition_variable`; the only difference is that the templated `wait` accepts any lock satisfying the Lockable requirements. There is essentially no learning curve—just replace `condition_variable` with `condition_variable_any` and you are done. The cost? Its implementation typically needs an additional mutex to protect the internal wait queue (because `condition_variable` can exploit the internal structure of `unique_lock<mutex>` for optimizations, while `condition_variable_any` knows nothing about the internals of the external lock), so it performs slightly worse. If your scenario only ever needs `unique_lock<std::mutex>`, do the honest thing and stick with `condition_variable`.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); browse to `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: A Thread-Safe Countdown Event

Implement a `CountdownEvent` class that behaves like C#'s `ManualResetEvent` or Java's `CountDownLatch`. It holds an internal counter with an initial value of N. Threads can call `wait()` to block until the counter reaches zero, and other threads call `signal()` to decrement the counter by 1. When the counter hits zero, all waiting threads should be woken up.

Requirements:

- Use `std::mutex` and `std::condition_variable`
- `wait()` must use the predicate version
- In `signal()`, think about whether `notify_one()` or `notify_all()` is the right call

Hint: the instant the counter goes from 1 to 0, the conditions of every thread blocked in `wait()` become true at the same time—this is the textbook case for `notify_all()`.

### Exercise 2: Extend the Bounded Queue with try_pop_for

Building on this article's `BoundedQueue`, add a `try_pop_for(duration)` method: try to take an element from the queue within the given time. If it succeeds before the timeout, return a `std::optional<T>` containing the value; if the time runs out, return `std::nullopt`.

Hint: use the predicate version of `wait_for` and check the return value to tell timeout from success. Also consider whether the thread is left in a safe state after a timed-out return—because `optional`'s `nullopt` tells the caller plainly that nothing was taken, the caller can decide whether to retry or give up.

### Exercise 3: Reproduce a Lost Wakeup

Write a program that deliberately sets up a "notify first, wait later" ordering. First use the predicate-less `wait` and observe whether the program blocks forever (it very likely will, depending on scheduling). Then add the predicate to `wait` and confirm that the program exits normally even though the notification was sent first. The point of this exercise is to feel the danger of a lost wakeup first-hand, and to understand why the predicate `wait` is mandatory.

## References

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable::wait -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [Condition variable -- Wikipedia (the POSIX standard discussion of spurious wakeups)](https://en.wikipedia.org/wiki/Monitor_(synchronization)#Condition_variables)
- [Why do spurious wakeups happen? -- StackOverflow](https://stackoverflow.com/questions/8594591/why-does-pthreads-cond-wait-have-spurious-wakeups)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
