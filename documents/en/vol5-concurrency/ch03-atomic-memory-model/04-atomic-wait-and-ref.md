---
title: atomic_wait and atomic_ref
description: C++20's wait/notify mechanism and atomic references, for building lighter-weight
  synchronization primitives than busy-waiting
chapter: 3
order: 4
tags:
- host
- cpp-modern
- advanced
- atomic
difficulty: advanced
platform: host
reading_time_minutes: 20
cpp_standard:
- 20
prerequisites:
- 内存序详解
related:
- fence 与编译器屏障
- 原子操作模式
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/04-atomic-wait-and-ref.md
  source_hash: 9d830f85af3452e48e8bb91a26d5fa83fcf346015b7c504ebda8b83f61aaba0d
  translated_at: '2026-05-20T04:39:24.885857+00:00'
  engine: anthropic
  token_count: 3917
---
# atomic_wait and atomic_ref

In the previous few chapters, we focused on the operation set, memory order, and fence of `std::atomic` — all tools for "how to safely read and write shared variables." But we haven't covered one scenario: Thread A modifies an atomic variable, and Thread B needs to wait for this change before proceeding. Before C++20, we only had two options — either use a spin loop (repeatedly `load`-ing to check), or introduce a heavyweight `mutex` + `condition_variable`. The former wastes CPU cycles, while the latter incurs microsecond-level context switch overhead even in the uncontended case.

C++20 provides a third path: `wait`, `notify_one`, and `notify_all`. These three member functions give atomic variables built-in "wait/notify" capabilities — a thread can block directly on an atomic variable until another thread modifies the value and sends a notification. On Linux, it maps to `futex` under the hood; on Windows, it uses `WaitOnAddress`. The latency is an order of magnitude lower than that of `condition_variable`.

In this chapter, we first break down the semantics and underlying mechanisms of `atomic::wait`, and then look at another powerful tool introduced in C++20: `std::atomic_ref` — which lets us apply atomic operations to existing non-atomic variables without changing their types. Finally, we combine these two tools to build a binary semaphore and see how they work together in practice.

## wait/notify: The Atomic Variable's Built-in "condition variable"

### Basic Interface

`std::atomic` gained three new member functions in C++20:

```cpp
// 阻塞当前线程，直到 notify_one/notify_all 被调用，
// 且当前值与 old_value 不同（伪唤醒也会返回）
void wait(T old_value,
          std::memory_order order = std::memory_order_seq_cst) const noexcept;

// 唤醒一个在 *this 上等待的线程（如果有）
void notify_one() noexcept;

// 唤醒所有在 *this 上等待的线程
void notify_all() noexcept;
```

Let's look at the simplest use case first — the main thread waiting for a worker thread to finish initialization:

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};

void worker()
{
    std::cout << "Worker: initializing...\n";
    // ... 做一些初始化工作 ...
    std::cout << "Worker: ready!\n";
    ready.store(true, std::memory_order_release);
    ready.notify_one();  // 通知等待的线程
}

int main()
{
    std::thread t(worker);

    // 在 ready 上等待，直到它不再是 false
    ready.wait(false, std::memory_order_acquire);
    std::cout << "Main: worker is ready, continuing\n";

    t.join();
    return 0;
}
```

The flow of this code is very intuitive: The main thread calls `flag.wait(false)`, meaning "if `flag`'s current value is `false`, block; if it's already `true`, return immediately." After the worker thread finishes initialization, it first `store(true)`, then calls `flag.notify_one()` to wake up the main thread.

### The Value Semantics of wait: An Easily Misunderstood Design

The parameter `old` in `wait(old)` is the "expected old value," not the "target value we are waiting for." This design often confuses beginners — if I'm waiting for it to become `true`, why do I pass `false`?

The reason is that the internal logic of `wait` works like this: First, it atomically `load`s the current value, then compares it with `old`. If they are equal, the thread enters a blocked state; if not, it returns immediately. This design has a key advantage: **it avoids the TOCTOU (Time of Check to Time of Use) race**. Imagine if the interface were designed as `wait(target)` — a thread `load`s the value, finds it's not equal to `target`, and prepares to block — but in that exact gap, another thread changes the value to `target`. The thread still blocks, and since no one will notify it again, we get a dead lock.

The `wait(old)` design naturally avoids this problem. `wait` internally merges the "compare value" and "decide to block" steps into a single atomic operation — if the value changes between the `load` and the block, the thread doesn't actually block. Instead, it immediately finds that the value is no longer equal and returns right away. This is a lock-free "compare-and-wait" pattern, perfectly consistent with the semantics of the Linux `futex` `FUTEX_WAIT` system call — and this is no coincidence, because C++'s `atomic::wait` maps directly to `futex`.

Additionally, note that `wait` allows for spurious wakeups — even if no one calls `notify_one`, `wait` might return. Therefore, `wait` should typically be used inside a loop:

```cpp
while (flag.load(std::memory_order_acquire) == false) {
    flag.wait(false, std::memory_order_acquire);
}
```

But in many scenarios, if the value has already changed, `wait` will return immediately anyway (because the value is no longer equal to `old`), so this loop usually executes only once. Although spurious wakeups are theoretically possible, they rarely occur in mainstream implementations. However, since the standard allows them, we must comply.

### Guarantees and Limitations of notify

`notify_one` wakes up one thread waiting on the same atomic variable (if there are multiple, which one is chosen is unspecified). `notify_all` wakes up all waiting threads. Their semantics are very similar to `condition_variable`'s `notify_one` and `notify_all`.

A key guarantee is: if another thread has already entered `wait` (and started blocking) before the `notify_one` call, this `notify_one` is guaranteed to wake it (or one of them). But if Thread A is executing `store`, and Thread B hasn't had a chance to call `wait` yet — Thread B won't miss this notification, because it will `load` the value first, find that it has already changed, and return directly without blocking. This is the power of the value semantics design: as long as the value has changed, `wait` won't wait in vain.

An easily overlooked detail: `store` and `notify_one` don't need to be paired with `wait` in the same thread. One thread can `store`, and another can `notify_one`, completely decoupled. But we must ensure the ordering of `store` and `notify_one` is correct — store first, then notify. Otherwise, the waiting thread might be woken up only to still see the old value (although it will loop and wait again, the efficiency suffers).

## Under the Hood: From futex to WaitOnAddress

Understanding the underlying implementation helps us set the right performance expectations. `atomic::wait` isn't magic — it maps to different OS primitives on different platforms.

### Linux: futex

On Linux, `atomic::wait` ultimately calls the `futex` system call. futex stands for "Fast Userspace muTEX," but its capabilities go far beyond mutex — it's essentially a kernel interface for "waiting on a userspace address." `FUTEX_WAIT` works by atomically comparing the value at an address with an expected value; if they are equal, it suspends the current thread. `FUTEX_WAKE` wakes up one thread waiting on that address.

This is almost perfectly consistent with the semantics of `atomic::wait`. The standard library implementation typically maintains an internal waiter table, mapping the address of the atomic variable to a futex wait queue. The details of this table determine the efficiency of `notify_one` — if the table is too large, hash collisions are rare but memory usage is high; if the table is too small, different atomic variables might share the same slot, causing `notify_one` to spuriously wake up unrelated threads. libstdc++'s implementation uses a fixed-size hash table, mapping addresses to slots.

The block/wake latency of futex is on the order of microseconds — faster than the `mutex` + `condition_variable` combination, but slower than a pure userspace spin. Therefore, the best use case for `atomic::wait` is when the wait time is neither too short (not worth spinning) nor too long (not worth using heavyweight synchronization primitives).

### Windows: WaitOnAddress

On Windows, the corresponding primitives are `WaitOnAddress`, `WakeByAddressSingle`, and `WakeByAddressAll`. Their semantics are almost perfectly symmetrical with futex: `WaitOnAddress` blocks when the value equals `old`, `WakeByAddressSingle` wakes one waiter, and `WakeByAddressAll` wakes all.

### Fallback

On platforms that don't support futex or `WaitOnAddress` (such as certain embedded RTOSes), the standard library falls back to a `mutex` + `condition_variable` implementation. This means `atomic::wait` is no more efficient than `condition_variable` on these platforms, but at least the interface is unified.

## Flag Synchronization Without Busy-Waiting

With `atomic::wait`, we can write synchronization code that neither wastes CPU nor introduces `mutex` overhead. A classic pattern is the "stop flag" — a background thread checks the flag in a loop, and the main thread sets the flag and notifies it to exit:

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

std::atomic<bool> stop_flag{false};

void background_worker()
{
    int iteration = 0;
    while (!stop_flag.load(std::memory_order_acquire)) {
        // 做一些周期性工作
        std::cout << "Working... iteration " << ++iteration << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "Worker: received stop signal, exiting\n";
}

int main()
{
    std::thread t(background_worker);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Main: sending stop signal\n";
    stop_flag.store(true, std::memory_order_release);
    stop_flag.notify_one();

    t.join();
    std::cout << "Main: worker joined\n";
    return 0;
}
```

In this example, `notify_one` isn't strictly necessary — the worker thread checks the flag every 500 ms and will eventually notice the change. But if we remove the `sleep_for`, making the worker thread truly busy-wait (e.g., executing short tasks at high frequency), then `notify_one` becomes crucial — it can wake up the worker thread immediately, reducing exit latency.

A more typical usage is to have the waiting side call `wait` directly, instead of polling:

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> signal{0};

void waiter()
{
    std::cout << "Waiter: waiting for signal\n";
    signal.wait(0, std::memory_order_acquire);
    std::cout << "Waiter: signal value = " << signal.load() << "\n";
}

void notifier()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Notifier: sending signal\n";
    signal.store(42, std::memory_order_release);
    signal.notify_one();
}

int main()
{
    std::thread t1(waiter);
    std::thread t2(notifier);
    t1.join();
    t2.join();
    return 0;
}
```

In this version, the `worker` thread blocks directly at `value.wait(0)`, consuming zero CPU. When the `main` thread changes the value to 42 and calls `value.notify_one()`, `worker` is woken up immediately. This saves power compared to busy-waiting and requires less code than `condition_variable`.

## std::atomic_ref<T>: Wrapping Non-Atomic Variables in Atomic Operations

### Why We Need atomic_ref

`std::atomic<T>` requires you to decide at declaration time whether a variable is atomic — once declared as `std::atomic<T>`, all access paths are atomic. But in real-world engineering, many scenarios don't allow this. The most typical case: you need to perform atomic operations on elements of an existing array, but the array's type is already set in stone (perhaps defined by a third-party library, or needing to be compatible with a C interface), and it can't be changed to `std::atomic<T>`.

`std::atomic_ref<T>`, introduced in C++20, solves this problem. It lets you apply atomic operations to a variable without changing its original type. You can think of it as an "atomic view" — it doesn't own the data; it simply provides an atomic access perspective.

### Basic Usage

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    int value = 0;  // 普通 int，不是 std::atomic<int>

    // 创建 atomic_ref，指向 value
    std::atomic_ref<int> ref(value);

    auto increment = [&ref]() {
        for (int i = 0; i < 1000000; ++i) {
            ref.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::thread t1(increment);
    std::thread t2(increment);

    t1.join();
    t2.join();

    std::cout << "value = " << value << "\n";  // 稳定输出 2000000
    return 0;
}
```

Note that the type of `counter` is a plain `int`, but when accessed through `atomic_ref`, `fetch_add` is atomic — there is no data race. `t1` and `t2` simultaneously increment `counter` one million times each, and the final result stably lands at 2,000,000.

### Operation Set: Almost Identical to std::atomic<T>

The interface of `std::atomic_ref<T>` is almost exactly the same as `std::atomic<T>` — `load`, `store`, `exchange`, `compare_exchange_weak`, `compare_exchange_strong`, and `fetch_add` (for integers and pointers) are all supported. The memory order parameters are also identical. The only difference is that `std::atomic_ref<T>` does not own the data; it is merely a reference to an existing variable.

### Restrictions and Constraints

The design of `std::atomic_ref<T>` brings several constraints that must be followed; violating them results in undefined behavior (UB).

The first and most critical rule: **the lifetime of the referenced object must exceed the lifetime of all `std::atomic_ref<T>` instances**. This is the same as the lifetime rules for ordinary references — if the object has already been destroyed and you're still accessing it through an `std::atomic_ref`, that's a classic dangling reference. In practice, this means you cannot let an `std::atomic_ref` outlive the variable it references.

The second rule: **once an `std::atomic_ref` is created for an object, that object can only be accessed through `std::atomic_ref` (or another `std::atomic_ref`) until all `std::atomic_ref` instances are destroyed**. In other words, you cannot mix atomic and non-atomic access paths. If Thread A does a `load` through `std::atomic_ref`, and Thread B reads and writes the raw variable directly — that's a data race, which is undefined behavior (UB). The logic behind this constraint is clear: `std::atomic_ref` needs to know that all accesses to the variable go through the atomic path; otherwise, it cannot guarantee consistency.

The third rule: **all `std::atomic_ref` instances on the same object must use the same alignment requirement**. `std::atomic_ref<T>` has a static member `required_alignment`, indicating the minimum alignment requirement. If atomic operations on certain platforms require special alignment (e.g., 64-bit atomic operations on ARM require 8-byte alignment), then all `std::atomic_ref` instances referencing the same object must respect this alignment.

The fourth rule: `std::atomic_ref` can be copy-constructed — copying produces a new `std::atomic_ref` instance referencing the same object. It can also be constructed directly from the referenced object. All `std::atomic_ref` instances referencing the same object share the atomic operation guarantees; consistency won't be broken just because "there's an extra reference."

### Typical Use Case: Atomic Access to Array Elements

The most common scenario for `std::atomic_ref` is performing atomic operations on elements in an array or container. For example, a global statistics array where multiple threads each update their own counters:

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

// 全局统计数组，类型是普通 int——可能需要被 C 代码或序列化库直接访问
constexpr int kNumCounters = 4;
int counters[kNumCounters] = {};

void update_counter(int index, int times)
{
    // 在函数内部创建 atomic_ref，安全地对 counters[index] 做原子自增
    std::atomic_ref<int> ref(counters[index]);
    for (int i = 0; i < times; ++i) {
        ref.fetch_add(1, std::memory_order_relaxed);
    }
}

int main()
{
    constexpr int kIncrementsPerThread = 1000000;
    std::vector<std::thread> threads;
    threads.reserve(kNumCounters);

    for (int i = 0; i < kNumCounters; ++i) {
        threads.emplace_back(update_counter, i, kIncrementsPerThread);
    }

    for (auto& t : threads) {
        t.join();
    }

    for (int i = 0; i < kNumCounters; ++i) {
        std::cout << "counter[" << i << "] = " << counters[i] << "\n";
    }
    return 0;
}
```

Each thread only accesses the counter it's responsible for — performing atomic operations via `std::atomic_ref`. Since there are no dependencies between different counters, `memory_order_relaxed` is sufficient. Note that the creation and destruction of `std::atomic_ref` both happen inside the `worker` function — its lifetime is strictly shorter than the `counters` array, satisfying the lifetime constraint.

## In Practice: A Binary Semaphore Based on atomic_wait

Now let's put our knowledge of `atomic::wait` and `std::atomic_ref` to use by implementing a complete binary semaphore. The semantics of a binary semaphore are: the initial value is 0 (or 1), `acquire` decrements the value from 1 to 0 (and waits if the value is 0), and `release` increments the value from 0 to 1 (and does nothing if the value is already 1). C++20 already provides `std::binary_semaphore`, but implementing one ourselves helps us understand how `atomic::wait` works.

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

class BinarySemaphore {
public:
    explicit BinarySemaphore(bool initial = false)
        : flag_(initial)
    {}

    void release()
    {
        // 先把 flag 设为 true，再通知等待者
        // 如果已经是 true，什么都不做
        bool expected = false;
        if (flag_.compare_exchange_strong(
                expected, true,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            flag_.notify_one();
        }
    }

    void acquire()
    {
        // 快速路径：如果已经是 true，直接 CAS 成功
        bool expected = true;
        if (flag_.compare_exchange_strong(
                expected, false,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return;
        }

        // 慢速路径：flag 是 false，需要等待
        // wait(false) 表示"如果值是 false 就阻塞"
        while (flag_.load(std::memory_order_acquire) == false) {
            flag_.wait(false, std::memory_order_acquire);
        }

        // 被唤醒后，尝试获取（可能有多个等待者竞争）
        bool exp = true;
        while (!flag_.compare_exchange_weak(
                   exp, false,
                   std::memory_order_acquire,
                   std::memory_order_relaxed)) {
            exp = true;
            flag_.wait(false, std::memory_order_acquire);
        }
    }

private:
    std::atomic<bool> flag_;
};
```

Let's analyze this code block by block.

The logic of `release` is simple: use CAS to change `value` from `0` to `1`. If `value` is already `1` (the semaphore has already been released), CAS fails, and we do nothing — this is exactly the semantics of a binary semaphore. After a successful CAS, we call `notify_one` to wake up one waiting `acquire`. Note that the `store` uses `memory_order_release` — this guarantees that all writes prior to `release` are visible to the woken thread.

`acquire` is split into two phases. The fast path uses CAS to change `value` from `1` to `0`. If the current value is indeed `1`, it succeeds and returns immediately — no blocking, no system calls, purely a userspace operation. If `value` is `0` (the semaphore hasn't been released), it enters the slow path: it blocks on `wait`, until another thread calls `release` to change it to `1` and sends a notification. After being woken up, it needs to CAS again — because multiple threads might be woken up simultaneously (a `notify_all` scenario, or spurious wakeups), and only one can succeed in acquiring.

Next, let's use this semaphore for a simple producer-consumer test:

```cpp
int main()
{
    constexpr int kNumItems = 10;
    BinarySemaphore sem_produced(false);  // 生产者完成后通知消费者
    BinarySemaphore sem_consumed(true);   // 消费者完成后通知生产者

    int shared_data = 0;

    std::thread producer([&]() {
        for (int i = 1; i <= kNumItems; ++i) {
            sem_consumed.acquire();   // 等待消费者消费完上一个
            shared_data = i;
            std::cout << "Produced: " << i << "\n";
            sem_produced.release();   // 通知消费者有新数据
        }
    });

    std::thread consumer([&]() {
        for (int i = 1; i <= kNumItems; ++i) {
            sem_produced.acquire();   // 等待生产者生产
            std::cout << "Consumed: " << shared_data << "\n";
            sem_consumed.release();   // 通知生产者可以继续
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

In this test, the producer and consumer execute alternately — the producer writes data and then `release`s, the consumer `acquire`s, reads the data, and then `release`s, forming an alternating produce-consume cycle. The semaphore's initial value is `1` (passed via the constructor as `1`), meaning the producer can start immediately — it doesn't need to wait for the consumer to consume first.

## Comparison with std::binary_semaphore

C++20 provides the standard `std::binary_semaphore` (defined in the `<semaphore>` header), and its functionality is almost identical to what we implemented above. So when should we use the standard library version, and when do we need to implement our own?

The advantage of the standard library's `std::binary_semaphore` is its clean interface — `acquire` and `release` are all you need, with no need to worry about the internal implementation. It's also based on futex/WaitOnAddress, so there's no performance difference. If your need is simply a semaphore, just use the standard library version.

But `atomic::wait` gives you more flexibility. A semaphore is just one of the synchronization primitives it can implement — you can also use it to implement events, latches, or even simplified condition variables. When your synchronization logic doesn't exactly match semaphore semantics, using `atomic::wait` directly is more natural than forcing a semaphore into the mix. Additionally, `atomic::wait` acts directly on atomic variables without needing extra synchronization objects, which might be more appropriate in memory-constrained scenarios (like embedded systems).

## Summary

In this chapter, we fully broke down two new atomic tools in C++20. `atomic::wait` gives atomic variables "wait/notify" capabilities — a thread can block directly on an atomic variable and be woken up when the value changes. Its value semantics design (passing the "expected old value" rather than the "target value to wait for") naturally avoids TOCTOU races. Under the hood, it maps to futex on Linux and WaitOnAddress on Windows, with latency an order of magnitude lower than `condition_variable`.

`std::atomic_ref`, on the other hand, solves the long-standing pain point of "performing atomic operations on existing non-atomic variables." Its interface is almost identical to `std::atomic<T>`, but it introduces strict lifetime constraints: the reference must not outlive the object, and non-atomic access paths are not allowed while an `std::atomic_ref` exists.

Finally, we used `atomic::wait` to implement a complete binary semaphore, seeing the full handling flow of the fast path (CAS succeeds directly) and the slow path (blocking wait + competing after being woken up). This pattern will appear repeatedly in the upcoming chapter on atomic operation patterns.

In the next chapter, we enter "Atomic Operation Patterns" — SeqLock, Double-Checked Locking, reference counting, publish-subscribe flags, and other classic patterns, comprehensively applying all the tools we learned in ch03.

## Exercises

### Exercise 1: Multiple Waiter Notification

Write a program: create four waiting threads, each calling `wait` on the same `std::atomic<int>`. After the main thread sleeps for one second, it changes the value to 1 and calls `notify_all`. Observe whether all four threads are woken up. Then change it to `notify_one` and observe how many threads are woken up. Note: Due to the possibility of spurious wakeups, even `notify_one` might wake multiple threads — but you will most likely observe only one being woken up.

### Exercise 2: atomic_ref and Arrays

Create a `std::vector<int>` containing eight elements. Start four threads, where each thread uses `std::atomic_ref` to increment two different elements in the vector one million times each. After completion, verify that the value of each element is correct. Be sure to choose non-overlapping element indices to avoid contention.

### Exercise 3: Improving the Binary Semaphore

The `acquire` function in our binary semaphore implementation has a potential issue in the slow path: after being woken up by `notify_one`, if CAS fails (another waiter got there first), the thread will enter `wait` again. But before re-entering `wait`, it needs to confirm that the value has actually returned to `0` — otherwise, it might block forever. Analyze whether the current implementation handles this scenario correctly; if not, point out the problem and fix it.

Hint: Consider this timing — Thread A and Thread B are woken up simultaneously. A succeeds with CAS first (`value` becomes `0`), and B's CAS fails (the value is already `0`). What should B do next? The value of `value` is `0`; B calls `wait(0)`, and at this point no thread will call `notify_one` again — will B block forever?

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [std::atomic::wait — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/wait)
- [std::atomic_ref — cppreference](https://en.cppreference.com/cpp/atomic/atomic_ref)
- [Implementing C++20 atomic waiting in libstdc++ — Red Hat Developer](https://developers.redhat.com/articles/2022/12/06/implementing-c20-atomic-waiting-libstdc)
- [ogiroux/atomic_wait — Sample Implementation (GitHub)](https://github.com/ogiroux/atomic_wait)
- [Synchronization with Atomics in C++20 — Modernes C++](https://www.modernescpp.com/index.php/synchronization-with-atomics-in-c-20/)
- [Atomic References with C++20 — Modernes C++](https://www.modernescpp.com/index.php/atomic-ref/)
