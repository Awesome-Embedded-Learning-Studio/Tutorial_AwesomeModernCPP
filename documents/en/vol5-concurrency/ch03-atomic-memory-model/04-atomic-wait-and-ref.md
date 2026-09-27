---
title: "atomic_wait and atomic_ref"
description: "The wait/notify mechanism and atomic references added in C++20, for building synchronization primitives lighter than busy-waiting"
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
cpp_standard: [20]
prerequisites:
  - "A Deep Dive into Memory Ordering"
related:
  - "Fences and Compiler Barriers"
  - "Atomic Operation Patterns"
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/04-atomic-wait-and-ref.md
  source_hash: 9d830f85af3452e48e8bb91a26d5fa83fcf346015b7c504ebda8b83f61aaba0d
  translated_at: '2026-09-26T08:02:42+00:00'
  engine: anthropic
  token_count: 8000
---

# atomic_wait and atomic_ref

In the past few articles we have been talking about `std::atomic`'s operation set, memory ordering, and fences—all tools for "how to safely read and write shared variables." But there is one scenario we have not touched: thread A modifies an atomic variable, and thread B has to wait for that change to happen before it can continue. Before C++20 we had only two options—busy-wait (a spin loop that calls `load()` over and over to check), or pull in a heavyweight mutex + condition_variable. The former wastes CPU; the latter costs microsecond-scale context-switch overhead even when there is no contention.

C++20 offers a third path: `std::atomic<T>::wait()`, `notify_one()`, and `notify_all()`. These three member functions give the atomic variable itself "wait/notify" capability—a thread can block directly on the atomic variable until another thread changes the value and sends a notification. On Linux this is implemented on top of futex under the hood, and on Windows on top of WaitOnAddress; the latency is an order of magnitude lower than condition_variable.

In this article we first take `wait/notify` apart in full—its semantics and the machinery underneath—and then look at another sharp tool introduced alongside it in C++20, `std::atomic_ref<T>`, which lets you apply atomic operations to an existing non-atomic variable without changing the variable's type. Finally we use these two tools together to build a binary semaphore and see how they cooperate in practice.

## wait/notify: The Atomic Variable's Built-In condition_variable

### The Basic Interface

`std::atomic<T>` gained three new member functions in C++20:

```cpp
// Block the current thread until notify_one/notify_all is called
// and the current value differs from old_value (spurious wakeups also return)
void wait(T old_value,
          std::memory_order order = std::memory_order_seq_cst) const noexcept;

// Wake one thread waiting on *this (if any)
void notify_one() noexcept;

// Wake all threads waiting on *this
void notify_all() noexcept;
```

Let's start with the simplest usage scenario—the main thread waiting for a worker thread to finish initialization:

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> ready{false};

void worker()
{
    std::cout << "Worker: initializing...\n";
    // ... do some initialization work ...
    std::cout << "Worker: ready!\n";
    ready.store(true, std::memory_order_release);
    ready.notify_one();  // notify the waiting thread
}

int main()
{
    std::thread t(worker);

    // wait on ready until it is no longer false
    ready.wait(false, std::memory_order_acquire);
    std::cout << "Main: worker is ready, continuing\n";

    t.join();
    return 0;
}
```

The flow here is very intuitive: the main thread calls `ready.wait(false)`, which means "if `ready` currently holds `false`, block; if it is already `true`, return immediately." After the worker finishes its initialization, it first does `store(true, release)`, then calls `notify_one()` to wake the main thread.

### wait's Value Semantics: An Easily Misunderstood Design

The `old_value` parameter of `wait()` is "the old value I expect to see," not "the target value I am waiting for." This design confuses beginners all the time—I am clearly waiting for it to become `true`, so why do I pass `false`?

The reason is that `wait()` works like this internally: first it atomically `load()`s the current value, then compares it against `old_value`. If they are equal, the thread blocks; if not, it returns immediately. This design has one key advantage: **it avoids the TOCTOU (Time of Check to Time of Use) race**. Imagine the interface were designed as `wait_until_equals(target)`—the thread loads the value, finds it differs from target, and prepares to block—but in exactly that gap, another thread changes the value to target. Our thread still goes and blocks, nobody will ever notify it again, and we have a deadlock.

The `old_value` design rules out this problem by construction. Inside `wait()`, "compare the value" and "decide to block" are fused into a single atomic operation—if the value changes between the load and the block, the thread never actually blocks; it immediately notices the value no longer matches and returns. This is a lock-free "compare-and-wait" pattern, semantically identical to the `FUTEX_WAIT` syscall on Linux—and that is no coincidence, because C++'s `wait()` maps directly onto futex.

One more thing to note: `wait()` permits spurious wakeups—`wait()` may return even though nobody called `notify`. That is why `wait()` usually has to live inside a loop:

```cpp
while (flag.load(std::memory_order_acquire) == false) {
    flag.wait(false, std::memory_order_acquire);
}
```

In practice, though, if the value has already changed, `wait()` itself returns immediately (because the value differs from `old_value`), so this loop usually runs just once. Spurious wakeups exist in theory but are extremely rare in mainstream implementations. Still, the standard allows them, so we have to play by its rules.

### notify's Guarantees and Limits

`notify_one()` wakes one thread waiting on the same atomic variable (if there are several, which one gets picked is unspecified). `notify_all()` wakes all waiting threads. Their semantics closely mirror `notify_one`/`notify_all` on a `condition_variable`.

One key guarantee: if, before the `notify` call, another thread has already entered `wait` (has already started blocking), then this `notify` will definitely wake it (or one of them). But if thread A is executing `notify` while thread B has not gotten around to calling `wait` yet—thread B does not miss the notification, because it will first load the value, see that it has already changed, and return immediately instead of blocking. This too is the power of the value-semantics design: once the value has changed, `wait` never blocks in vain.

An easily overlooked detail: `notify_one()` and `notify_all()` do not need to be paired with `wait()` in the same thread. One thread can do `store + notify` while another does `wait + load`—fully decoupled. Just make sure the `store` and the `notify` happen in the right order—store first, then notify—otherwise the waiting thread may wake up and still see the old value (it will loop around and wait again, but the efficiency suffers).

## Under the Hood: From futex to WaitOnAddress

Knowing what happens under the hood helps us set the right performance expectations. `wait/notify` is not magic—on different platforms it maps to different operating-system primitives.

### Linux: futex

On Linux, `std::atomic<T>::wait()` ends up calling the `futex()` syscall. futex is short for "Fast Userspace muTEX", but it does far more than mutexes—at its core it is a kernel interface for "waiting on a userspace address". `futex(addr, FUTEX_WAIT, expected_val, ...)` works like this: atomically compare `*addr` against `expected_val`, and if they are equal, suspend the current thread. `futex(addr, FUTEX_WAKE, 1, ...)` wakes one thread waiting on `addr`.

That is almost exactly the semantics of `std::atomic::wait(old_value)`. The standard library implementation typically maintains an internal waiter table that maps atomic variables' addresses onto futex wait queues. The details of this table determine how efficient `notify` is—a table that is too large has few hash collisions but high memory usage; a table that is too small lets different atomic variables share the same slot, causing `notify` to wake unrelated threads by mistake. libstdc++ uses a fixed-size hash table, mapping addresses to slots.

futex block/wake latency sits at the microsecond scale—faster than the mutex + condition_variable combo, but slower than a pure userspace spin. So the sweet spot for `wait/notify` is: waits that are not too short (not worth busy-waiting) and not too long (not worth a heavyweight synchronization primitive).

### Windows: WaitOnAddress

On Windows, the corresponding primitives are `WaitOnAddress`, `WakeByAddressSingle`, and `WakeByAddressAll`, whose semantics mirror futex almost exactly: `WaitOnAddress(addr, &expected, sizeof(T), INFINITE)` blocks while `*addr == expected`; `WakeByAddressSingle(addr)` wakes one waiter; `WakeByAddressAll(addr)` wakes them all.

### The Fallback Path

On platforms that support neither futex nor WaitOnAddress (certain embedded RTOSes, for example), the standard library falls back to an implementation built on `std::mutex` + `std::condition_variable`. That means `wait/notify` is no more efficient than condition_variable there—but at least the interface stays uniform.

## Flag Synchronization Without Busy-Waiting

With `wait/notify` we can write synchronization code that neither burns CPU nor drags in mutex overhead. A classic pattern is the "stop flag"—a background thread checks a flag in a loop, and the main thread sets the flag and notifies it to exit:

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
        // do some periodic work
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

In this example `notify_one()` is not actually required—the worker checks the flag every 500 ms and will eventually notice the change. But if we drop the `sleep_for` and let the worker truly busy-wait (running short tasks at high frequency, say), then `notify_one()` becomes critical—it wakes the worker immediately and cuts the exit latency.

The more typical pattern is to have the waiting side `wait` directly instead of polling:

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

In this version the `waiter` thread blocks right at `signal.wait(0)` and consumes no CPU at all. When `notifier` changes the value to 42 and calls `notify_one()`, `waiter` wakes immediately. Compared with busy-waiting this saves power; compared with condition_variable it saves code.

## std::atomic_ref<T>: Putting Atomic Operations on Non-Atomic Variables

### Why atomic_ref Is Needed

`std::atomic<T>` forces you to decide at the point of declaration whether a variable is atomic—once you declare it as `std::atomic<int>`, every access path through it is atomic. But real-world projects have plenty of situations where that is not allowed. The most typical one: you need atomic operations on elements of an existing array, but the array's type is already locked down (it may be defined by a third-party library, or it may have to stay compatible with a C interface), so you cannot change it to `std::atomic<int>[]`.

`std::atomic_ref<T>`, introduced in C++20, solves exactly this problem. It lets you apply atomic operations to a variable without changing the variable's original type. You can think of it as an "atomic view"—it does not own the data; it merely provides an atomic way of looking at it.

### Basic Usage

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    int value = 0;  // a plain int, not std::atomic<int>

    // create an atomic_ref pointing at value
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

    std::cout << "value = " << value << "\n";  // reliably prints 2000000
    return 0;
}
```

Note that `value` is a plain `int`, but when accessed through `std::atomic_ref<int>`, `fetch_add` is atomic—no data race. `t1` and `t2` each increment `value` a million times concurrently, and the final result settles reliably at 2000000.

### The Operation Set: Almost Identical to std::atomic<T>

`std::atomic_ref<T>`'s interface is almost exactly the same as `std::atomic<T>`'s—`load()`, `store()`, `exchange()`, `compare_exchange_weak/strong()`, `fetch_add()` (for integral and pointer types), and so on are all supported. The memory-order parameters are exactly the same too. The only difference: `atomic_ref` does not own data; it is just a reference to an existing variable.

### Restrictions and Constraints

The design of `std::atomic_ref` brings with it several constraints you must follow; violating them is undefined behavior.

First, and most critical: **the lifetime of the referenced object must outlast the lifetime of every `atomic_ref` instance**. This is the same lifetime rule as for ordinary references—if the object is already destroyed and you are still accessing it through an `atomic_ref`, that is a textbook dangling reference. In real code, this means you must not let an `atomic_ref` live longer than the variable it refers to.

Second: **once an `atomic_ref` to an object has been created, until all `atomic_ref` instances are destroyed, that object may only be accessed through `atomic_ref` (or `std::atomic`)**. In other words, you cannot mix atomic and non-atomic access paths. If thread A does a `fetch_add` through an `atomic_ref` while thread B reads and writes the raw variable directly—that is a data race, undefined behavior. The logic behind this constraint is clear: `atomic_ref` needs every access to the variable to go down an atomic path, otherwise consistency cannot be guaranteed.

Third: **every `atomic_ref` instance on the same object must use the same alignment requirement**. `std::atomic_ref<T>` has a static member `required_alignment` that gives the minimum alignment requirement. If atomic operations on some platform need special alignment (64-bit atomic operations on ARM requiring 8-byte alignment, for example), then every `atomic_ref` referring to the same object must honor that alignment.

Fourth: `std::atomic_ref` is copy-constructible—copying produces a new `atomic_ref` instance referring to the same object, and it can also be constructed directly from the referenced object. All `atomic_ref` instances referring to the same object share the atomic-operation guarantees; having "one more reference" does not break consistency.

### A Typical Use Case: Atomic Access to Array Elements

The most common scenario for `atomic_ref` is atomic operations on elements of an array or container. For example, a global statistics array where several threads each update the counter they are responsible for:

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

// global statistics array of plain int—may need direct access from C code or a serialization library
constexpr int kNumCounters = 4;
int counters[kNumCounters] = {};

void update_counter(int index, int times)
{
    // create the atomic_ref inside the function to increment counters[index] atomically and safely
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

Each thread touches only the counter it is responsible for—atomically, through an `atomic_ref`. Since the different counters have no dependencies between them, `memory_order_relaxed` is enough. Notice also that the `atomic_ref` is created and destroyed entirely inside the `update_counter` function—its lifetime is strictly shorter than that of the `counters` array, which satisfies the lifetime constraint.

## In Practice: A Binary Semaphore Built on atomic_wait

Now let's put our `wait/notify` and `atomic_ref` knowledge to work and implement a complete binary semaphore. The semantics of a binary semaphore: it starts at 0 (or 1); `acquire()` takes the value from 1 down to 0 (waiting if the value is 0); `release()` takes the value from 0 up to 1 (doing nothing if it is already 1). C++20 already provides `std::binary_semaphore`, but building one ourselves helps a lot in understanding how `wait/notify` works.

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
        // set flag to true first, then notify the waiters
        // if it is already true, do nothing
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
        // fast path: if it is already true, the CAS succeeds outright
        bool expected = true;
        if (flag_.compare_exchange_strong(
                expected, false,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return;
        }

        // slow path: flag is false, we have to wait
        // wait(false) means "block if the value is false"
        while (flag_.load(std::memory_order_acquire) == false) {
            flag_.wait(false, std::memory_order_acquire);
        }

        // after waking up, try to acquire (several waiters may compete)
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

Let's walk through this code block by block.

The logic of `release()` is simple: use a CAS to flip `flag_` from `false` to `true`. If `flag_` is already `true` (the semaphore was already released), the CAS fails and we do nothing—exactly the semantics of a binary semaphore. After a successful CAS we call `notify_one()` to wake one waiting `acquire()`. Note that the store uses `memory_order_release`—this guarantees that all writes before `release()` become visible to the thread that gets woken.

`acquire()` has two phases. The fast path uses a CAS to flip `flag_` from `true` to `false`; if the current value really is `true`, it returns successfully right away—no blocking, no syscall, purely userspace work. If `flag_` is `false` (the semaphore has not been released), we enter the slow path: block on `wait(false)` until another thread calls `release()`, flipping it to `true` and notifying. Once woken, we still have to CAS again—because multiple threads may wake at the same time (a `notify_all` scenario, or spurious wakeups), and only one of them can acquire successfully.

Next, let's run a simple producer-consumer test with this semaphore:

```cpp
int main()
{
    constexpr int kNumItems = 10;
    BinarySemaphore sem_produced(false);  // notifies the consumer once the producer is done
    BinarySemaphore sem_consumed(true);   // notifies the producer once the consumer is done

    int shared_data = 0;

    std::thread producer([&]() {
        for (int i = 1; i <= kNumItems; ++i) {
            sem_consumed.acquire();   // wait for the consumer to finish the previous item
            shared_data = i;
            std::cout << "Produced: " << i << "\n";
            sem_produced.release();   // notify the consumer there is new data
        }
    });

    std::thread consumer([&]() {
        for (int i = 1; i <= kNumItems; ++i) {
            sem_produced.acquire();   // wait for the producer to produce
            std::cout << "Consumed: " << shared_data << "\n";
            sem_consumed.release();   // notify the producer it may continue
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

In this test the producer and consumer take turns—the producer writes data and calls `release(sem_produced)`; the consumer does `acquire(sem_produced)`, reads, then `release(sem_consumed)`, forming an alternating produce-consume cycle. `sem_consumed` starts at `true` (passed through the constructor), meaning the producer can start immediately at the beginning—no need to wait for the consumer to consume first.

## Comparison with std::binary_semaphore

C++20 provides a standard `std::binary_semaphore` (defined in the `<semaphore>` header), which does almost exactly what the one we implemented above does. So when should you use the standard library version, and when do you need to roll your own?

The strength of the standard `std::binary_semaphore` is its clean interface—`acquire()` and `release()` are all you need, with no internal implementation to worry about. It is built on futex/WaitOnAddress too, so there is no performance difference. If all you need is a plain semaphore, just use the standard library version.

But `std::atomic::wait/notify` gives you more flexibility. The semaphore is just one of the synchronization primitives it can express—you can also use it to implement events, one-shot latches, even a stripped-down condition variable. When your synchronization logic does not happen to match semaphore semantics, using `wait/notify` directly is more natural than shoehorning a semaphore in. On top of that, `wait/notify` acts directly on the atomic variable with no extra synchronization object, which can make it a better fit in memory-constrained settings (embedded systems, for example).

## Summary

In this article we took apart two new C++20 atomic tools in full. `std::atomic<T>::wait()`/`notify_one()`/`notify_all()` give atomic variables "wait/notify" capability—a thread can block directly on the atomic variable and be woken when the value changes. The value-semantics design (passing "the old value I expect" rather than "the target value I wait for") rules out the TOCTOU race by construction; underneath, it maps to futex on Linux and WaitOnAddress on Windows, with latency an order of magnitude lower than condition_variable.

`std::atomic_ref<T>` addresses the long-standing pain of "applying atomic operations to existing non-atomic variables". Its interface is almost identical to `std::atomic<T>`'s, but it introduces strict lifetime constraints: the reference must not outlive the object, and while any `atomic_ref` exists, non-atomic access paths are off limits.

Finally, we used `wait/notify` to implement a complete binary semaphore and saw the full flow of the fast path (the CAS succeeds outright) and the slow path (block and wait, then compete after waking). This pattern will come back again and again in the upcoming article on atomic operation patterns.

In the next article we move on to "atomic operation patterns"—classic patterns such as SeqLock, Double-Checked Locking, reference counting, and publish-subscribe flags, putting together every tool we picked up in ch03.

## Exercises

### Exercise 1: Notifying Multiple Waiters

Write a program that creates 4 waiting threads, each calling `wait(0)` on the same `std::atomic<int>`. After the main thread sleeps for 1 second, change the value to 1 and call `notify_all()`. Observe whether all 4 threads are woken. Then switch to `notify_one()` and observe how many threads are woken. Note: because spurious wakeups are possible, even `notify_one()` may wake more than one thread—but you will most likely observe exactly one waking up.

### Exercise 2: atomic_ref and Arrays

Create a `std::vector<int>` containing 8 elements. Start 4 threads; each thread uses `std::atomic_ref<int>` to increment two different elements of the vector one million times each. When they finish, verify that every element has the correct value. Be careful to pick non-overlapping element indices to avoid races.

### Exercise 3: Improving the Binary Semaphore

Our `BinarySemaphore::acquire()` has a potential problem in the slow path: after being woken by `notify_one()`, if the CAS fails (another waiter got there first), the thread goes back into `wait`. But before waiting again, it needs to confirm that the value has indeed gone back to `false`—otherwise it may block forever. Analyze whether the current implementation handles this scenario correctly; if not, pinpoint the problem and fix it.

Hint: consider this timeline—threads A and B are woken at the same time; A's CAS succeeds first (`true -> false`), while B's CAS fails (the value is already `false`). What should B do now? `flag_` is `false`, B calls `wait(false)`, and at that point no thread will ever call `release()` again—will B block forever?

> 💡 The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); look under `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [std::atomic::wait — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/wait)
- [std::atomic_ref — cppreference](https://en.cppreference.com/cpp/atomic/atomic_ref)
- [Implementing C++20 atomic waiting in libstdc++ — Red Hat Developer](https://developers.redhat.com/articles/2022/12/06/implementing-c20-atomic-waiting-libstdc)
- [ogiroux/atomic_wait — Sample Implementation (GitHub)](https://github.com/ogiroux/atomic_wait)
- [Synchronization with Atomics in C++20 — Modernes C++](https://www.modernescpp.com/index.php/synchronization-with-atomics-in-c-20/)
- [Atomic References with C++20 — Modernes C++](https://www.modernescpp.com/index.php/atomic-ref/)
